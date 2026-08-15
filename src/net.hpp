#pragma once

#include <stdint.h>

namespace StreamMii {
    namespace Net {

        enum class Compression : uint8_t {
            LZ4      = 1,
            DeltaLZ4 = 2,
            JPEG     = 3
        };

        bool Init(const char *ip, uint16_t port);

        bool SendFrame(const void *buffer, uint32_t compressedSize, uint32_t originalSize, uint32_t width, uint32_t height, uint32_t pitch, Compression compression, bool keyframe, uint8_t needsSRGB);

        void Shutdown();

    } // namespace Net

} // namespace StreamMii
