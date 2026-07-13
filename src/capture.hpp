#pragma once

#include <gx2/display.h>

namespace StreamMii {

struct StoredBuffer {
    void *buffer = nullptr;
    uint32_t buffer_size = 0;
    int32_t mode = 0;
    GX2SurfaceFormat surface_format;
    GX2BufferingMode buffering_mode;
};

void ReleaseBuffer(void *buffer);

void SetTVBuffer(
    void *buffer,
    uint32_t buffer_size,
    int32_t mode,
    GX2SurfaceFormat format,
    GX2BufferingMode buffering);

void SetDRCBuffer(
    void *buffer,
    uint32_t buffer_size,
    int32_t mode,
    GX2SurfaceFormat format,
    GX2BufferingMode buffering);

const StoredBuffer &GetTVBuffer();
const StoredBuffer &GetDRCBuffer();

void InitCapture();
void ShutdownCapture();
void CaptureFrame(const GX2ColorBuffer *colorBuffer);

}