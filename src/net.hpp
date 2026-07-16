#pragma once

#include <stdint.h>

namespace StreamMii
{
namespace Net
{

enum class Compression : uint8_t
{
    None = 0,
    LZ4 = 1,
    DeltaLZ4 = 2
};

bool Init(const char *ip, uint16_t port);

bool SendFrame(
    const void *buffer,
    uint32_t size,
    uint32_t width,
    uint32_t height,
    uint32_t pitch,
    Compression compression,
    bool keyframe);

void Shutdown();

}
}