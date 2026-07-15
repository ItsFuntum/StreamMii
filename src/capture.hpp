#pragma once

#include <gx2/display.h>

namespace StreamMii {

// Capture every Nth frame.
// Assuming a 60 FPS game:
// 1 = 60 FPS
// 2 = 30 FPS
// 3 = 20 FPS
// 4 = 15 FPS
// 6 = 10 FPS
static constexpr uint8_t FRAME_SKIP = 3;
static constexpr uint32_t FRAME_WIDTH = 320;
static constexpr uint32_t FRAME_HEIGHT = 180;
static constexpr uint32_t FRAME_SIZE = FRAME_WIDTH * FRAME_HEIGHT * 2;

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