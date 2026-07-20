#include "capture.hpp"
#include "net.hpp"
#include "thread.hpp"
#include "config.hpp"
#include "utils/logger.h"

#include <gx2/surface.h>
#include <gx2/state.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <memory/mappedmemory.h>
#include <coreinit/cache.h>
#include <coreinit/filesystem.h>
#include <coreinit/mutex.h>
#include <malloc.h>
#include <cstdlib>
#include <cstdio>
#include <string.h>

namespace StreamMii {

GX2Surface resolvedSurface = {};
static GX2Surface sTVSurface = {};
static GX2ColorBuffer sCaptureBuffer = {};

static StoredBuffer sTVBuffer;
static StoredBuffer sDRCBuffer;

static constexpr uint32_t BUFFER_COUNT = 2;

static uint8_t *sFrameCopies[BUFFER_COUNT] = {};
static bool sFrameUsed[BUFFER_COUNT] = {};

static FrameMessage latestFrame = {};
static bool latestReady = false;
static OSMutex frameMutex;

static uint8_t frameSkip = 0;

static bool initialized = false;

static uint32_t sBytesPerPixel = 4;


static uint8_t *GetFreeBuffer()
{
    for(uint32_t i = 0; i < BUFFER_COUNT; i++)
    {
        if(!sFrameUsed[i])
        {
            sFrameUsed[i] = true;
            return sFrameCopies[i];
        }
    }

    return nullptr;
}

void ReleaseBuffer(void *buffer)
{
    for(uint32_t i = 0; i < BUFFER_COUNT; i++)
    {
        if(sFrameCopies[i] == buffer)
        {
            sFrameUsed[i] = false;
            return;
        }
    }
}

static bool CreateCaptureBuffer(GX2ColorBuffer &buffer)
{
    buffer.surface = {};

    buffer.surface.width = gWidth;
    buffer.surface.height = gHeight;
    buffer.surface.depth = 1;

    buffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    buffer.surface.mipLevels = 1;

    switch (gCompression)
    {
        case Net::Compression::JPEG:
            buffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
            break;

        default:
            buffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
            break;
    }

    buffer.surface.aa = GX2_AA_MODE1X;

    buffer.surface.use = (GX2SurfaceUse)(GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);

    buffer.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;

    buffer.surface.swizzle = 0;
    buffer.surface.pitch = 0;


    GX2CalcSurfaceSizeAndAlignment(&buffer.surface);

    DEBUG_FUNCTION_LINE(
        "Surface %ux%u size=%u align=%u pitch=%u",
        buffer.surface.width,
        buffer.surface.height,
        buffer.surface.imageSize,
        buffer.surface.alignment,
        buffer.surface.pitch
    );


    buffer.surface.image = MEMAllocFromMappedMemoryForGX2Ex(buffer.surface.imageSize, buffer.surface.alignment);

    DEBUG_FUNCTION_LINE(
        "Allocated image=%p",
        buffer.surface.image
    );


    if(!buffer.surface.image)
        return false;


    GX2InitColorBufferRegs(&buffer);


    return true;
}

bool GetLatestFrame(FrameMessage &out)
{
    bool result = false;

    OSLockMutex(&frameMutex);

    if(latestReady)
    {
        out = latestFrame;
        latestReady = false;
        result = true;
    }

    OSUnlockMutex(&frameMutex);

    return result;
}

void SetTVBuffer(void *buffer, uint32_t buffer_size, int32_t mode, GX2SurfaceFormat format, GX2BufferingMode buffering)
{
    sTVBuffer.buffer = buffer;
    sTVBuffer.buffer_size = buffer_size;
    sTVBuffer.mode = mode;
    sTVBuffer.surface_format = format;
    sTVBuffer.buffering_mode = buffering;

    memset(&sTVSurface, 0, sizeof(GX2Surface));
}

void SetDRCBuffer(void *buffer, uint32_t buffer_size, int32_t mode, GX2SurfaceFormat format, GX2BufferingMode buffering)
{
    sDRCBuffer.buffer = buffer;
    sDRCBuffer.buffer_size = buffer_size;
    sDRCBuffer.mode = mode;
    sDRCBuffer.surface_format = format;
    sDRCBuffer.buffering_mode = buffering;
}

const StoredBuffer &GetTVBuffer()
{
    return sTVBuffer;
}

const StoredBuffer &GetDRCBuffer()
{
    return sDRCBuffer;
}

void InitCapture()
{
    if(initialized)
        return;

    switch(gCompression)
    {
        case Net::Compression::JPEG:
            sBytesPerPixel = 4;
            break;

        default:
            sBytesPerPixel = 2;
            break;
    }

    OSInitMutex(&frameMutex);

    for(uint32_t i = 0; i < BUFFER_COUNT; i++)
    {
        sFrameCopies[i] =
            (uint8_t*)MEMAllocFromMappedMemoryForGX2Ex(
                gWidth * gHeight * sBytesPerPixel,
                0x100
            );

        if(!sFrameCopies[i])
        {
            DEBUG_FUNCTION_LINE(
                "Frame buffer failed %u",
                i
            );
            return;
        }

        sFrameUsed[i] = false;
    }

    initialized = true;

    DEBUG_FUNCTION_LINE(
        "Capture system initialized"
    );
}

void ShutdownCapture()
{
    if (!initialized)
        return;

    OSLockMutex(&frameMutex);

    if(latestReady)
    {
        ReleaseBuffer(latestFrame.buffer);
        latestReady = false;
        memset(&latestFrame, 0, sizeof(FrameMessage));
    }

    OSUnlockMutex(&frameMutex);

    for(uint32_t i = 0; i < BUFFER_COUNT; i++)
    {
        if(sFrameCopies[i])
        {
            MEMFreeToMappedMemory(sFrameCopies[i]);
            sFrameCopies[i] = nullptr;
        }

        sFrameUsed[i] = false;
    }

    if(sCaptureBuffer.surface.image)
    {
        MEMFreeToMappedMemory(sCaptureBuffer.surface.image);

        memset(
            &sCaptureBuffer,
            0,
            sizeof(GX2ColorBuffer)
        );
    }

    DEBUG_FUNCTION_LINE("Capture shutdown");

    initialized = false;
}

void CaptureFrame(const GX2ColorBuffer *colorBuffer)
{
    if(!gEnabled)
        return;

    frameSkip++;

    if(frameSkip % gFrameSkip)
        return;

    if(gResolutionChanged)
    {
        ShutdownCapture();
        InitCapture();

        gResolutionChanged = false;
    }

    if (!colorBuffer)
    {
        DEBUG_FUNCTION_LINE("No color buffer");
        return;
    }

    if(!sCaptureBuffer.surface.image)
    {
        if(!CreateCaptureBuffer(sCaptureBuffer))
        {
            DEBUG_FUNCTION_LINE(
                "GX2 buffer creation failed"
            );
            return;
        }

        DEBUG_FUNCTION_LINE(
            "GX2 capture buffer ready"
        );
    }

    if (!colorBuffer->surface.image)
    {
        DEBUG_FUNCTION_LINE(
            "Invalid surface image"
        );
        return;
    }

    if(colorBuffer->surface.width == 0 || colorBuffer->surface.height == 0)
    {
        return;
    }

    GX2Invalidate(GX2_INVALIDATE_MODE_CPU, sCaptureBuffer.surface.image, sCaptureBuffer.surface.imageSize);
    if(colorBuffer->surface.aa != GX2_AA_MODE1X)
    {
        GX2Surface resolvedSurface = colorBuffer->surface;
    
        resolvedSurface.aa = GX2_AA_MODE1X;
        GX2CalcSurfaceSizeAndAlignment(&resolvedSurface);
    
        resolvedSurface.image =
            MEMAllocFromMappedMemoryForGX2Ex(resolvedSurface.imageSize, resolvedSurface.alignment);
        
        if(!resolvedSurface.image)
        {
            DEBUG_FUNCTION_LINE(
                "Failed allocating AA resolve surface"
            );
            return;
        }
    
        GX2ResolveAAColorBuffer(
            colorBuffer,
            &resolvedSurface,
            colorBuffer->viewMip,
            colorBuffer->viewFirstSlice
        );
    
        GX2CopySurface(
            &resolvedSurface,
            0,
            0,
            &sCaptureBuffer.surface,
            0,
            0
        );
    
        MEMFreeToMappedMemory(resolvedSurface.image);
    }
    else
    {
        GX2CopySurface(
            &colorBuffer->surface,
            colorBuffer->viewMip,
            colorBuffer->viewFirstSlice,
            &sCaptureBuffer.surface,
            0,
            0
        );
    }

    /*

    This code may not be necessary

    Flush out destinations caches
    GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, colorBuffer->surface.image,colorBuffer->surface.imageSize);

    Wait until GPU finished writing
    GX2DrawDone();
    */

    uint8_t *buffer = GetFreeBuffer();

    if(!buffer)
    {
        DEBUG_FUNCTION_LINE("No free frame buffer");
        return;
    }

    uint8_t* src = (uint8_t*)sCaptureBuffer.surface.image;

    uint8_t* dst = (uint8_t*)buffer;

    for(uint32_t y = 0; y < gHeight; y++)
    {
        memcpy(
            dst + y * gWidth * sBytesPerPixel,
            src + y * sCaptureBuffer.surface.pitch * sBytesPerPixel,
            gWidth * sBytesPerPixel
        );
    }

    FrameMessage msg;

    msg.buffer = buffer;
    msg.size = gWidth * gHeight * sBytesPerPixel;
    msg.width = gWidth;
    msg.height = gHeight;
    msg.pitch = gWidth * sBytesPerPixel;

    OSLockMutex(&frameMutex);

    if(latestReady)
    {
        ReleaseBuffer(latestFrame.buffer);
    }

    latestFrame = msg;
    latestReady = true;

    OSUnlockMutex(&frameMutex);
}

}