#include "thread.hpp"
#include "net.hpp"
#include "capture.hpp"
#include "config.hpp"
#include "utils/logger.h"

#include "lz4.h"
#include <turbojpeg.h>

#include <memory/mappedmemory.h>
#include <coreinit/thread.h>

#include <malloc.h>
#include <string.h>


namespace StreamMii
{

static OSThread *networkThread = nullptr;
static uint8_t *networkStack = nullptr;

static bool running = false;

constexpr uint32_t STACK_SIZE = 64 * 1024;

static constexpr uint32_t MAX_FRAME_SIZE = 854 * 480 * 4;

static constexpr uint32_t MAX_COMPRESSED_SIZE = MAX_FRAME_SIZE + (MAX_FRAME_SIZE / 255) + 16;


static uint8_t compressedBuffer[MAX_COMPRESSED_SIZE];
static unsigned char *jpegBuffer = nullptr;

static tjhandle jpegHandle = nullptr;

static uint32_t frameCounter = 0;

static uint8_t previousFrame[MAX_FRAME_SIZE];
static uint8_t deltaFrame[MAX_FRAME_SIZE];
static bool havePrevious = false;

Net::Compression gCompression = Net::Compression::JPEG;


static int32_t NetworkThreadEntry(int32_t argc, const char **argv)
{
    DEBUG_FUNCTION_LINE("Network thread started");

    while(running)
    {
        if(gNetworkChanged)
        {
            gNetworkChanged = false;

            Net::Shutdown();
            Net::Init(gIP, gPort);

            havePrevious = false;
            frameCounter = 0;
        }

        FrameMessage frame;

        if(GetLatestFrame(frame))
        {
            const uint8_t *current = static_cast<const uint8_t *>(frame.buffer);

            int compressedSize = 0;
            bool keyframe = true;

            frameCounter++;


            if(gCompression == Net::Compression::JPEG)
            {
                unsigned long jpegSize = 0;

                jpegBuffer = nullptr;

                int result = tjCompress2(
                    jpegHandle,
                    current,
                    frame.width,
                    frame.pitch,
                    frame.height,
                    TJPF_RGBA,
                    &jpegBuffer,
                    &jpegSize,
                    TJSAMP_420,
                    60,
                    TJFLAG_FASTDCT
                );


                if(result == 0)
                {
                    compressedSize = jpegSize;
                }
                else
                {
                    DEBUG_FUNCTION_LINE("JPEG failed: %s", tjGetErrorStr());

                    if(jpegBuffer)
                    {
                        tjFree(jpegBuffer);
                        jpegBuffer = nullptr;
                    }
                }
            }
            else
            {
                const uint8_t *input = current;

                bool useDelta =
                    gDeltaEnabled &&
                    havePrevious &&
                    (frameCounter % gKeyframeInterval != 0);


                if(useDelta)
                {
                    const uint32_t *cur = reinterpret_cast<const uint32_t*>(current);

                    const uint32_t *prev = reinterpret_cast<const uint32_t*>(previousFrame);

                    uint32_t *dst = reinterpret_cast<uint32_t*>(deltaFrame);


                    for(uint32_t i = 0; i < frame.size / sizeof(uint32_t); i++)
                    {
                        dst[i] = cur[i] ^ prev[i];
                    }

                    input = deltaFrame;
                    gCompression = Net::Compression::DeltaLZ4;
                    keyframe = false;
                }
                else
                {
                    gCompression = Net::Compression::LZ4;
                    keyframe = true;
                }


                compressedSize = LZ4_compress_default(
                    (const char*)input,
                    (char*)compressedBuffer,
                    frame.size,
                    sizeof(compressedBuffer)
                );
            }


            if(compressedSize > 0)
            {
                const uint8_t *output =
                    (gCompression == Net::Compression::JPEG)
                    ? jpegBuffer
                    : compressedBuffer;

                if(Net::SendFrame(
                    output,
                    compressedSize,
                    frame.width,
                    frame.height,
                    frame.pitch,
                    gCompression,
                    keyframe))
                {
                    if(gCompression != Net::Compression::JPEG)
                    {
                        memcpy(previousFrame, current, frame.size);
                        havePrevious = true;
                    }
                }
                else
                {
                    havePrevious = false; // force a keyframe next time
                }

                if(gCompression == Net::Compression::JPEG)
                {
                    if(jpegBuffer)
                    {
                        tjFree(jpegBuffer);
                        jpegBuffer = nullptr;
                    }
                }
            }
            else
            {
                havePrevious = false;
            }


            ReleaseBuffer(frame.buffer);
        }
        else
        {
            OSSleepTicks(OSMillisecondsToTicks(1));
        }
    }


    DEBUG_FUNCTION_LINE(
        "Network thread stopped"
    );


    return 0;
}



bool InitThread()
{
    DEBUG_FUNCTION_LINE("Network thread initializing");

    if(running)
        return true;

    jpegHandle = tjInitCompress();

    if(!jpegHandle)
    {
        DEBUG_FUNCTION_LINE("TurboJPEG init failed");
        return false;
    }

    networkThread = (OSThread *)memalign(0x20, sizeof(OSThread));


    networkStack = (uint8_t *)memalign(0x20, STACK_SIZE);


    if(!networkThread || !networkStack)
    {
        DEBUG_FUNCTION_LINE(
            "Thread allocation failed"
        );

        return false;
    }


    running = true;

    DEBUG_FUNCTION_LINE(
        "Thread=%p Stack=%p",
        networkThread,
        networkStack
    );

    // Thread priority is low here
    if(!OSCreateThread(
        networkThread,
        NetworkThreadEntry,
        0,
        nullptr,
        networkStack + STACK_SIZE,
        STACK_SIZE,
        21,
        OS_THREAD_ATTRIB_AFFINITY_ANY))
    {
        DEBUG_FUNCTION_LINE(
            "OSCreateThread failed"
        );
    
        free(networkStack);
        free(networkThread);
    
        networkStack = nullptr;
        networkThread = nullptr;
    
        running = false;
    
        return false;
    }


    OSSetThreadName(
        networkThread,
        "StreamMii Network"
    );


    OSResumeThread(networkThread);


    return true;
}



void ShutdownThread()
{
    if(!running)
        return;

    running = false;

    if(networkThread)
    {
        OSJoinThread(networkThread, nullptr);
    }

    if(jpegHandle)
    {
        tjDestroy(jpegHandle);
        jpegHandle = nullptr;
    }

    free(networkStack);
    free(networkThread);

    networkStack = nullptr;
    networkThread = nullptr;

    havePrevious = false;
    frameCounter = 0;
}

}