#pragma once

#include <stdint.h>

struct FrameMessage {
    uint32_t poolIndex;

    void *buffer;

    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;

    bool needsSRGB;
};