#include "capture.hpp"
#include "utils/logger.h"

#include <gx2/swap.h>
#include <gx2/display.h>
#include <wups.h>

DECL_FUNCTION(void, GX2CopyColorBufferToScanBuffer_hook, const GX2ColorBuffer *colorBuffer, GX2ScanTarget scanTarget)
{
    real_GX2CopyColorBufferToScanBuffer_hook(colorBuffer, scanTarget);
    if (scanTarget == GX2_SCAN_TARGET_TV && colorBuffer)
    {
        StreamMii::CaptureFrame((GX2ColorBuffer *) colorBuffer);
    }
}

DECL_FUNCTION(void, GX2SetTVBuffer_hook, void *buffer, uint32_t buffer_size, int32_t mode, GX2SurfaceFormat format, GX2BufferingMode buffering)
{
    DEBUG_FUNCTION_LINE(
        "GX2SetTVBuffer buffer=%p size=%u format=%d mode=%d buffering=%d",
        buffer,
        buffer_size,
        format,
        mode,
        buffering
    );

    StreamMii::SetTVBuffer(
        buffer,
        buffer_size,
        mode,
        format,
        buffering
    );

    real_GX2SetTVBuffer_hook(
        buffer,
        buffer_size,
        mode,
        format,
        buffering
    );
}

DECL_FUNCTION(void, GX2SetDRCBuffer_hook, void *buffer, uint32_t buffer_size, int32_t mode, GX2SurfaceFormat format, GX2BufferingMode buffering)
{
    DEBUG_FUNCTION_LINE(
        "GX2SetDRCBuffer buffer=%p size=%u format=%d mode=%d buffering=%d",
        buffer,
        buffer_size,
        format,
        mode,
        buffering
    );

    StreamMii::SetDRCBuffer(
        buffer,
        buffer_size,
        mode,
        format,
        buffering);

    real_GX2SetDRCBuffer_hook(
        buffer,
        buffer_size,
        mode,
        format,
        buffering);
}

WUPS_MUST_REPLACE(
    GX2CopyColorBufferToScanBuffer_hook,
    WUPS_LOADER_LIBRARY_GX2,
    GX2CopyColorBufferToScanBuffer
);

WUPS_MUST_REPLACE(
    GX2SetTVBuffer_hook,
    WUPS_LOADER_LIBRARY_GX2,
    GX2SetTVBuffer
);

WUPS_MUST_REPLACE(
    GX2SetDRCBuffer_hook,
    WUPS_LOADER_LIBRARY_GX2,
    GX2SetDRCBuffer
);