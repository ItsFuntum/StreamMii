#pragma once

#include <stdint.h>

namespace StreamMii
{
namespace Net
{

bool Init(const char *ip, uint16_t port);

bool SendFrame(
    const void *buffer,
    uint32_t size,
    uint32_t width,
    uint32_t height,
    uint32_t pitch);

void Shutdown();

}
}