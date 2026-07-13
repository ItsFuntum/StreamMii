#pragma once

#include <cstdint>

namespace StreamMii
{

struct FrameMessage
{
    void *buffer;

    uint32_t size;

    uint16_t width;
    uint16_t height;
    uint16_t pitch;
};

bool InitThread();
void ShutdownThread();

bool QueueFrame(const FrameMessage &frame);

}