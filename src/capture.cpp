#include "capture.hpp"
#include "config.hpp"
#include "net.hpp"
#include "thread.hpp"
#include "utils/logger.h"

#include <coreinit/cache.h>
#include <coreinit/filesystem.h>
#include <coreinit/mutex.h>
#include <cstdio>
#include <cstdlib>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <malloc.h>
#include <memory/mappedmemory.h>
#include <string.h>

namespace StreamMii {

    GX2Surface resolvedSurface   = {};
    static GX2Surface sTVSurface = {};

    static StoredBuffer sTVBuffer;
    static StoredBuffer sDRCBuffer;

    static constexpr uint32_t CAPTURE_POOL_SIZE = 3;

    struct CaptureSurface {
        GX2ColorBuffer buffer;

        bool busy;
        OSTime gpuTimestamp;

        uint32_t width;
        uint32_t height;
        uint32_t pitch;

        bool needsSRGB;
    };

    static CaptureSurface sCapturePool[CAPTURE_POOL_SIZE] = {};
    static uint32_t sCaptureWriteIndex                    = 0;

    static FrameMessage latestFrame = {};
    static bool latestReady         = false;
    static OSMutex frameMutex;

    static uint32_t sDroppedFrames = 0;

    static uint8_t frameSkip = 0;

    static bool initialized     = false;
    static bool poolInitialized = false;

    static GX2Surface sAAResolveSurface = {};
    static bool sAAResolveInitialized   = false;

    static constexpr uint32_t JPEG_SIMD_OVERREAD_PADDING = 64;


    static bool CreateAAResolveSurface(const GX2Surface &sourceSurface) {
        sAAResolveSurface = sourceSurface;
        sAAResolveSurface.aa = GX2_AA_MODE1X;

        GX2CalcSurfaceSizeAndAlignment(&sAAResolveSurface);

        sAAResolveSurface.image = MEMAllocFromMappedMemoryForGX2Ex(
            sAAResolveSurface.imageSize + JPEG_SIMD_OVERREAD_PADDING,
            sAAResolveSurface.alignment
        );

        return sAAResolveSurface.image != nullptr;
    }

    static uint32_t GetCaptureBytesPerPixel() {
        return gCompressionMode == CompressionMode::JPEG ? 4 : 2;
    }

    static CaptureSurface *GetFreeCaptureSurface() {
        OSLockMutex(&frameMutex);
        CaptureSurface *found = nullptr;
        for (uint32_t i = 0; i < CAPTURE_POOL_SIZE; i++) {
            uint32_t index = (sCaptureWriteIndex + i) % CAPTURE_POOL_SIZE;
            if (!sCapturePool[index].busy) {
                sCaptureWriteIndex = (index + 1) % CAPTURE_POOL_SIZE;
                found              = &sCapturePool[index];
                break;
            }
        }
        OSUnlockMutex(&frameMutex);
        return found;
    }

    static bool CreateCaptureSurface(CaptureSurface &surface) {
        memset(&surface, 0, sizeof(surface));

        GX2Surface &captureSurface =
                surface.buffer.surface;

        memset(
                &captureSurface,
                0,
                sizeof(GX2Surface));

        captureSurface.width  = gWidth;
        captureSurface.height = gHeight;
        captureSurface.depth  = 1;

        captureSurface.dim = GX2_SURFACE_DIM_TEXTURE_2D;

        captureSurface.mipLevels = 1;

        switch (gCompressionMode) {
            case CompressionMode::JPEG:
                captureSurface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
                break;

            default:
                captureSurface.format = GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
                break;
        }

        captureSurface.aa =
                GX2_AA_MODE1X;

        captureSurface.use = (GX2SurfaceUse) (GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);

        captureSurface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
        captureSurface.swizzle  = 0;
        captureSurface.pitch    = 0;

        GX2CalcSurfaceSizeAndAlignment(&captureSurface);

        DEBUG_FUNCTION_LINE(
                "Capture surface %ux%u size=%u align=%u pitch=%u",
                captureSurface.width,
                captureSurface.height,
                captureSurface.imageSize,
                captureSurface.alignment,
                captureSurface.pitch);

        captureSurface.image = MEMAllocFromMappedMemoryForGX2Ex(captureSurface.imageSize + JPEG_SIMD_OVERREAD_PADDING, captureSurface.alignment);

        if (!captureSurface.image) {
            DEBUG_FUNCTION_LINE("Failed to allocate capture surface");

            return false;
        }

        GX2InitColorBufferRegs(
                &surface.buffer);

        return true;
    }

    bool GetLatestFrame(FrameMessage &out) {
        bool result = false;

        OSLockMutex(&frameMutex);

        if (latestReady) {
            out         = latestFrame;
            latestReady = false;
            result      = true;
        }

        OSUnlockMutex(&frameMutex);

        if (result) {
            CaptureSurface &surface =
                    sCapturePool[out.poolIndex];

            if (surface.gpuTimestamp > 0) {
                GX2WaitTimeStamp(surface.gpuTimestamp);
            }

            GX2Invalidate(
                    GX2_INVALIDATE_MODE_CPU,
                    surface.buffer.surface.image,
                    surface.buffer.surface.imageSize);
        }

        return result;
    }

    void ReleaseFrame(const FrameMessage &frame) {
        if (frame.poolIndex >= CAPTURE_POOL_SIZE)
            return;

        OSLockMutex(&frameMutex);

        sCapturePool[frame.poolIndex].busy = false;

        OSUnlockMutex(&frameMutex);
    }

    void SetTVBuffer(void *buffer, uint32_t buffer_size, int32_t mode, GX2SurfaceFormat format, GX2BufferingMode buffering) {
        sTVBuffer.buffer         = buffer;
        sTVBuffer.buffer_size    = buffer_size;
        sTVBuffer.mode           = mode;
        sTVBuffer.surface_format = format;
        sTVBuffer.buffering_mode = buffering;

        memset(&sTVSurface, 0, sizeof(GX2Surface));
    }

    void SetDRCBuffer(void *buffer, uint32_t buffer_size, int32_t mode, GX2SurfaceFormat format, GX2BufferingMode buffering) {
        sDRCBuffer.buffer         = buffer;
        sDRCBuffer.buffer_size    = buffer_size;
        sDRCBuffer.mode           = mode;
        sDRCBuffer.surface_format = format;
        sDRCBuffer.buffering_mode = buffering;
    }

    const StoredBuffer &GetTVBuffer() {
        return sTVBuffer;
    }

    const StoredBuffer &GetDRCBuffer() {
        return sDRCBuffer;
    }

    static bool InitializeCapturePool() {
        DEBUG_FUNCTION_LINE(
                "Initializing capture surface pool");

        for (uint32_t i = 0;
             i < CAPTURE_POOL_SIZE;
             i++) {

            if (!CreateCaptureSurface(
                        sCapturePool[i])) {

                DEBUG_FUNCTION_LINE(
                        "Failed to create capture surface %u",
                        i);

                // Clean up surfaces that were successfully created before the failure
                for (uint32_t j = 0;
                     j < i;
                     j++) {

                    CaptureSurface &surface =
                            sCapturePool[j];

                    if (surface.buffer.surface.image) {
                        MEMFreeToMappedMemory(
                                surface.buffer.surface.image);

                        surface.buffer.surface.image =
                                nullptr;
                    }

                    memset(
                            &surface,
                            0,
                            sizeof(CaptureSurface));
                }

                return false;
            }
        }

        sCaptureWriteIndex = 0;

        DEBUG_FUNCTION_LINE(
                "Capture surface pool initialized");

        return true;
    }

    void InitCapture() {
        if (initialized)
            return;

        OSInitMutex(&frameMutex);

        initialized = true;

        DEBUG_FUNCTION_LINE("Capture system initialized");
    }

    void ShutdownCapture() {
        if (!initialized)
            return;

        OSLockMutex(
                &frameMutex);

        if (latestReady) {

            if (latestFrame.poolIndex <
                CAPTURE_POOL_SIZE) {

                sCapturePool[latestFrame.poolIndex].busy = false;
            }

            latestReady = false;

            memset(
                    &latestFrame,
                    0,
                    sizeof(FrameMessage));
        }

        OSUnlockMutex(&frameMutex);

        // Free every persistent capture surface
        for (uint32_t i = 0; i < CAPTURE_POOL_SIZE; i++) {

            CaptureSurface &surface =
                    sCapturePool[i];

            if (surface.buffer.surface.image) {

                MEMFreeToMappedMemory(
                        surface.buffer.surface.image);

                surface.buffer.surface.image =
                        nullptr;
            }

            memset(
                    &surface,
                    0,
                    sizeof(CaptureSurface));
        }

        if (sAAResolveSurface.image) {
            MEMFreeToMappedMemory(sAAResolveSurface.image);
            sAAResolveSurface.image = nullptr;
        }
        sAAResolveInitialized = false;

        sCaptureWriteIndex = 0;

        poolInitialized = false;

        DEBUG_FUNCTION_LINE("Capture shutdown");

        initialized = false;
    }

    void CaptureFrame(const GX2ColorBuffer *colorBuffer, GX2SurfaceFormat scanFormat) {
        if (!gEnabled)
            return;

        frameSkip++;

        if (frameSkip % gFrameSkip)
            return;

        if (gResolutionChanged || gCompressionChanged) {
            ShutdownCapture();
            InitCapture();

            gResolutionChanged  = false;
            gCompressionChanged = false;
        }

        if (!colorBuffer) {
            DEBUG_FUNCTION_LINE("No color buffer");
            return;
        }

        if (!colorBuffer->surface.image) {
            DEBUG_FUNCTION_LINE("Invalid surface image");
            return;
        }

        if (colorBuffer->surface.width == 0 || colorBuffer->surface.height == 0) {
            return;
        }

        if (!poolInitialized) {
            if (!InitializeCapturePool()) {
                DEBUG_FUNCTION_LINE("Capture pool initialization failed");
                return;
            }

            poolInitialized = true;
        }

        CaptureSurface *surface = GetFreeCaptureSurface();

        if (!surface) {
            sDroppedFrames++;
            if (sDroppedFrames % 30 == 0) {
                DEBUG_FUNCTION_LINE("Dropped frames due to full pool: %u", sDroppedFrames);
            }
            return;
        }

        GX2Invalidate(
                GX2_INVALIDATE_MODE_CPU,
                surface->buffer.surface.image,
                surface->buffer.surface.imageSize);

        // Submit GPU Copy
        if (colorBuffer->surface.aa == GX2_AA_MODE1X) {
            GX2CopySurface(
                    &colorBuffer->surface,
                    colorBuffer->viewMip,
                    colorBuffer->viewFirstSlice,
                    &surface->buffer.surface,
                    0,
                    0);
        } else {
            if (!sAAResolveInitialized || sAAResolveSurface.format != colorBuffer->surface.format) {
                if (sAAResolveSurface.image) {
                    MEMFreeToMappedMemory(sAAResolveSurface.image);
                    sAAResolveSurface.image = nullptr;
                }
                if (!CreateAAResolveSurface(colorBuffer->surface)) {
                    DEBUG_FUNCTION_LINE("Failed to allocate AA resolve surface");
                    return;
                }
                sAAResolveInitialized = true;
            }

            GX2ResolveAAColorBuffer(
                    colorBuffer,
                    &sAAResolveSurface,
                    colorBuffer->viewMip,
                    colorBuffer->viewFirstSlice);

            GX2CopySurface(
                    &sAAResolveSurface,
                    0,
                    0,
                    &surface->buffer.surface,
                    0,
                    0);
        }

        GX2Invalidate(
                GX2_INVALIDATE_MODE_COLOR_BUFFER,
                surface->buffer.surface.image,
                surface->buffer.surface.imageSize);

        GX2Flush();

        surface->gpuTimestamp = GX2GetLastSubmittedTimeStamp();

        surface->busy = true;

        uint32_t poolIndex =
                static_cast<uint32_t>(
                        surface - sCapturePool);

        FrameMessage msg = {};

        msg.poolIndex = poolIndex;
        msg.buffer    = surface->buffer.surface.image;
        msg.width     = gWidth;
        msg.height    = gHeight;
        msg.needsSRGB = (scanFormat & 0x400) != 0;

        const uint32_t bytesPerPixel = GetCaptureBytesPerPixel();

        // GX2 pitch is in pixels/elements
        // Convert it to bytes only when sending it as a byte stride
        msg.pitch = surface->buffer.surface.pitch * bytesPerPixel;

        // The capture buffer is laid out as pitch bytes per row
        msg.size = msg.pitch * msg.height;

        OSLockMutex(&frameMutex);

        if (latestReady) {
            sCapturePool[latestFrame.poolIndex].busy = false;
        }

        latestFrame = msg;
        latestReady = true;

        OSUnlockMutex(&frameMutex);
    }

} // namespace StreamMii