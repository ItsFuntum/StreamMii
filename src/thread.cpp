#include "thread.hpp"
#include "capture.hpp"
#include "config.hpp"
#include "net.hpp"
#include "utils/logger.h"

#include "lz4.h"
#include <turbojpeg.h>

#include <coreinit/thread.h>
#include <memory/mappedmemory.h>

#include <coreinit/cache.h>

#include <malloc.h>
#include <string.h>


namespace StreamMii {

    static OSThread *networkThread = nullptr;
    static uint8_t *networkStack   = nullptr;

    static bool running = false;

    constexpr uint32_t STACK_SIZE = 64 * 1024;


    static unsigned char *jpegBuffer = nullptr;

    static tjhandle jpegHandle = nullptr;

    static uint32_t frameCounter = 0;

    static uint8_t *compressedBuffer = nullptr;
    static uint8_t *previousFrame    = nullptr;
    static uint8_t *deltaFrame       = nullptr;

    static bool havePrevious = false;

    static const uint8_t sRGBGammaLUT[256] = {
            0x00, 0x0C, 0x15, 0x1C, 0x21, 0x26, 0x2A, 0x2E, 0x31, 0x34, 0x37, 0x3A, 0x3D, 0x3F, 0x42, 0x44,
            0x46, 0x49, 0x4B, 0x4D, 0x4F, 0x51, 0x52, 0x54, 0x56, 0x58, 0x59, 0x5B, 0x5D, 0x5E, 0x60, 0x61,
            0x63, 0x64, 0x66, 0x67, 0x68, 0x6A, 0x6B, 0x6D, 0x6E, 0x6F, 0x70, 0x72, 0x73, 0x74, 0x75, 0x76,
            0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
            0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
            0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA1, 0xA2, 0xA3, 0xA4,
            0xA5, 0xA5, 0xA6, 0xA7, 0xA8, 0xA8, 0xA9, 0xAA, 0xAB, 0xAB, 0xAC, 0xAD, 0xAE, 0xAE, 0xAF, 0xB0,
            0xB0, 0xB1, 0xB2, 0xB3, 0xB3, 0xB4, 0xB5, 0xB5, 0xB6, 0xB7, 0xB7, 0xB8, 0xB9, 0xB9, 0xBA, 0xBB,
            0xBB, 0xBC, 0xBD, 0xBD, 0xBE, 0xBF, 0xBF, 0xC0, 0xC1, 0xC1, 0xC2, 0xC2, 0xC3, 0xC4, 0xC4, 0xC5,
            0xC5, 0xC6, 0xC7, 0xC7, 0xC8, 0xC9, 0xC9, 0xCA, 0xCA, 0xCB, 0xCC, 0xCC, 0xCD, 0xCD, 0xCE, 0xCE,
            0xCF, 0xD0, 0xD0, 0xD1, 0xD1, 0xD2, 0xD2, 0xD3, 0xD3, 0xD4, 0xD4, 0xD5, 0xD5, 0xD6, 0xD6, 0xD7,
            0xD7, 0xD8, 0xD9, 0xD9, 0xDA, 0xDA, 0xDB, 0xDB, 0xDC, 0xDC, 0xDD, 0xDD, 0xDE, 0xDE, 0xDF, 0xDF,
            0xE0, 0xE1, 0xE1, 0xE2, 0xE2, 0xE3, 0xE3, 0xE4, 0xE4, 0xE5, 0xE5, 0xE6, 0xE6, 0xE7, 0xE7, 0xE8,
            0xE9, 0xE9, 0xEA, 0xEA, 0xEB, 0xEB, 0xEC, 0xEC, 0xED, 0xED, 0xED, 0xEE, 0xEE, 0xEF, 0xEF, 0xF0,
            0xF0, 0xF1, 0xF1, 0xF2, 0xF2, 0xF3, 0xF3, 0xF4, 0xF4, 0xF5, 0xF5, 0xF5, 0xF6, 0xF6, 0xF7, 0xF7,
            0xF8, 0xF8, 0xF9, 0xF9, 0xFA, 0xFA, 0xFB, 0xFB, 0xFB, 0xFC, 0xFC, 0xFD, 0xFD, 0xFE, 0xFE, 0xFE};

    Net::Compression packetCompression;


    static uint32_t GetFrameSize() {
        return gWidth * gHeight * 4;
    }

    static uint32_t GetMaxCompressedSize() {
        uint32_t size = GetFrameSize();
        return size + (size / 255) + 16;
    }

    static int32_t NetworkThreadEntry(int32_t argc, const char **argv) {
        DEBUG_FUNCTION_LINE("Network thread started");

        while (running) {
            if (gNetworkChanged) {
                gNetworkChanged = false;

                Net::Shutdown();
                Net::Init(gIP, gPort);

                havePrevious = false;
                frameCounter = 0;
            }

            FrameMessage frame;

            if (GetLatestFrame(frame)) {
                const uint8_t *current = static_cast<const uint8_t *>(frame.buffer);

                if (frame.needsSRGB && frame.compressionMode == CompressionMode::JPEG) {
                    uint32_t *px32      = (uint32_t *) frame.buffer;
                    uint32_t pixelPitch = frame.pitch / 4; // pitch is in bytes; RGBA8 = 4 bytes/px
                    for (uint32_t y = 0; y < frame.height; y++) {
                        uint32_t *row = &px32[y * pixelPitch];
                        for (uint32_t x = 0; x < frame.width; x++) {
                            uint32_t pxl = row[x];
                            uint8_t r    = sRGBGammaLUT[(pxl >> 24) & 0xFF];
                            uint8_t g    = sRGBGammaLUT[(pxl >> 16) & 0xFF];
                            uint8_t b    = sRGBGammaLUT[(pxl >> 8) & 0xFF];
                            row[x]       = (r << 24) | (g << 16) | (b << 8) | (pxl & 0xFF);
                        }
                    }
                }

                int compressedSize = 0;
                bool keyframe      = true;

                frameCounter++;


                if (frame.compressionMode == CompressionMode::JPEG) {
                    packetCompression = Net::Compression::JPEG;

                    unsigned long jpegSize = 0;

                    jpegBuffer = nullptr;

                    int result = tjCompress2(
                            jpegHandle,
                            current,
                            frame.width,
                            frame.pitch,
                            frame.height,
                            TJPF_RGBX,
                            &jpegBuffer,
                            &jpegSize,
                            TJSAMP_420,
                            gJPEGQuality,
                            TJFLAG_FASTDCT);


                    if (result == 0) {
                        compressedSize = jpegSize;
                    } else {
                        DEBUG_FUNCTION_LINE("JPEG failed: %s", tjGetErrorStr());

                        if (jpegBuffer) {
                            tjFree(jpegBuffer);
                            jpegBuffer = nullptr;
                        }
                    }
                } else {
                    const uint8_t *input = current;

                    bool useDelta =
                            frame.compressionMode == CompressionMode::LZ4 &&
                            gDeltaEnabled &&
                            havePrevious &&
                            (frameCounter % gKeyframeInterval != 0);


                    if (useDelta) {
                        for (uint32_t i = 0; i < frame.size; i++) {
                            deltaFrame[i] =
                                    current[i] ^
                                    previousFrame[i];
                        }

                        input             = deltaFrame;
                        packetCompression = Net::Compression::DeltaLZ4;
                        keyframe          = false;
                    } else {
                        packetCompression = Net::Compression::LZ4;
                        keyframe          = true;
                    }


                    compressedSize = LZ4_compress_default(
                            (const char *) input,
                            (char *) compressedBuffer,
                            frame.size,
                            GetMaxCompressedSize());
                }


                if (compressedSize > 0) {
                    const uint8_t *output =
                            (frame.compressionMode == CompressionMode::JPEG)
                                    ? jpegBuffer
                                    : compressedBuffer;

                    if (Net::SendFrame(
                                output,
                                compressedSize,
                                frame.size,
                                frame.width,
                                frame.height,
                                frame.pitch,
                                packetCompression,
                                keyframe,
                                frame.needsSRGB)) {
                        if (frame.compressionMode != CompressionMode::JPEG) {
                            memcpy(previousFrame, current, frame.size);
                            havePrevious = true;
                        }
                    } else {
                        havePrevious = false; // force a keyframe next time
                    }

                    if (frame.compressionMode == CompressionMode::JPEG) {
                        if (jpegBuffer) {
                            tjFree(jpegBuffer);
                            jpegBuffer = nullptr;
                        }
                    }
                } else {
                    havePrevious = false;
                }


                ReleaseFrame(frame);
            } else {
                OSSleepTicks(OSMillisecondsToTicks(1));
            }
        }


        DEBUG_FUNCTION_LINE(
                "Network thread stopped");


        return 0;
    }


    bool InitThread() {
        DEBUG_FUNCTION_LINE("Network thread initializing");

        if (running)
            return true;

        jpegHandle = tjInitCompress();

        if (!jpegHandle) {
            DEBUG_FUNCTION_LINE("TurboJPEG init failed");
            return false;
        }

        compressedBuffer = (uint8_t *) malloc(GetMaxCompressedSize());
        previousFrame    = (uint8_t *) malloc(GetFrameSize());
        deltaFrame       = (uint8_t *) malloc(GetFrameSize());
        if (!compressedBuffer || !previousFrame || !deltaFrame) {
            DEBUG_FUNCTION_LINE("Failed to allocate frame buffers");
            if (compressedBuffer) {
                free(compressedBuffer);
                compressedBuffer = nullptr;
            }
            if (previousFrame) {
                free(previousFrame);
                previousFrame = nullptr;
            }
            if (deltaFrame) {
                free(deltaFrame);
                deltaFrame = nullptr;
            }
        }

        networkThread = (OSThread *) memalign(0x20, sizeof(OSThread));


        networkStack = (uint8_t *) memalign(0x20, STACK_SIZE);


        if (!networkThread || !networkStack) {
            DEBUG_FUNCTION_LINE(
                    "Thread allocation failed");

            return false;
        }


        running = true;

        DEBUG_FUNCTION_LINE(
                "Thread=%p Stack=%p",
                networkThread,
                networkStack);

        // Thread priority is low here
        if (!OSCreateThread(
                    networkThread,
                    NetworkThreadEntry,
                    0,
                    nullptr,
                    networkStack + STACK_SIZE,
                    STACK_SIZE,
                    21,
                    OS_THREAD_ATTRIB_AFFINITY_ANY)) {
            DEBUG_FUNCTION_LINE(
                    "OSCreateThread failed");

            free(networkStack);
            free(networkThread);

            networkStack  = nullptr;
            networkThread = nullptr;

            running = false;

            return false;
        }


        OSSetThreadName(
                networkThread,
                "StreamMii Network");


        OSResumeThread(networkThread);


        return true;
    }


    void ShutdownThread() {
        if (!running)
            return;

        running = false;

        if (networkThread) {
            OSJoinThread(networkThread, nullptr);
        }

        if (jpegHandle) {
            tjDestroy(jpegHandle);
            jpegHandle = nullptr;
        }

        free(networkStack);
        free(networkThread);

        networkStack  = nullptr;
        networkThread = nullptr;

        free(compressedBuffer);
        free(previousFrame);
        free(deltaFrame);

        compressedBuffer = nullptr;
        previousFrame    = nullptr;
        deltaFrame       = nullptr;

        havePrevious = false;
        frameCounter = 0;
    }

} // namespace StreamMii