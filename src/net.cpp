#include "net.hpp"
#include "utils/logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <coreinit/time.h>
#include <errno.h>
#include <fcntl.h>


namespace StreamMii
{
namespace Net
{

#pragma pack(push,1)
struct PacketHeader
{
    uint32_t magic;
    uint32_t frame;

    uint16_t index;
    uint16_t count;

    uint16_t width;
    uint16_t height;
    uint16_t pitch;

    uint32_t compressedSize;
    uint32_t originalSize;

    uint16_t payloadSize;

    uint8_t compression;
    uint8_t keyframe;
};
#pragma pack(pop)

static int socket_fd = -1;
static sockaddr_in destination;


bool Init(const char *ip, uint16_t port)
{
    socket_fd = socket(
        AF_INET,
        SOCK_DGRAM,
        IPPROTO_UDP
    );

    if(socket_fd < 0)
    {
        DEBUG_FUNCTION_LINE("Socket failed");
        return false;
    }

    int sndbuf = 512 * 1024;

    setsockopt(
        socket_fd,
        SOL_SOCKET,
        SO_SNDBUF,
        &sndbuf,
        sizeof(sndbuf)
    );

    int flags = fcntl(socket_fd, F_GETFL, 0);

    fcntl(
        socket_fd,
        F_SETFL,
        flags | O_NONBLOCK
    );

    memset(
        &destination,
        0,
        sizeof(destination)
    );


    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);


    if(inet_aton(ip, &destination.sin_addr) == 0)
    {
        DEBUG_FUNCTION_LINE(
            "Invalid IP address: %s",
            ip
        );

        return false;
    }


    DEBUG_FUNCTION_LINE(
        "Sending to %s:%d",
        ip,
        port
    );


    DEBUG_FUNCTION_LINE(
        "Network initialized"
    );


    return true;
}


bool SendFrame(const void *buffer, uint32_t size, uint32_t width, uint32_t height, uint32_t pitch, Compression compression, bool keyframe)
{
    if(socket_fd < 0)
        return false;


    constexpr uint32_t MAX_PAYLOAD = 1400;

    static uint32_t frameNumber = 0;

    uint32_t frame = frameNumber++;


    const uint8_t *data = (const uint8_t *)buffer;


    uint16_t packets =
        (size + MAX_PAYLOAD - 1) / MAX_PAYLOAD;

    uint8_t packet[sizeof(PacketHeader)+MAX_PAYLOAD];

    DEBUG_FUNCTION_LINE(
        "Frame %u compressed %u/%u bytes packets=%u",
        frame,
        size,
        width * height * 2,
        packets
    );

    for(uint16_t i = 0; i < packets; i++)
    {
        uint32_t offset = i * MAX_PAYLOAD;

        uint32_t remaining = size - offset;

        uint32_t payload =
            remaining > MAX_PAYLOAD ?
            MAX_PAYLOAD :
            remaining;

        PacketHeader header;

        header.magic = htonl(0x5354524D);
        header.frame = htonl(frame);

        header.index = htons(i);
        header.count = htons(packets);

        header.width = htons(width);
        header.height = htons(height);
        header.pitch = htons(pitch);

        header.compressedSize = htonl(size);
        header.originalSize = htonl(width * height * 2);

        header.payloadSize = htons(payload);
        header.compression = static_cast<uint8_t>(compression);
        header.keyframe = keyframe ? 1 : 0;


        memcpy(packet, &header, sizeof(header));


        memcpy(packet + sizeof(header), data + offset, payload);

        int result = sendto(
            socket_fd,
            packet,
            sizeof(header)+payload,
            0,
            (sockaddr*)&destination,
            sizeof(destination)
        );

        if(result < 0)
        {
            if(errno == EWOULDBLOCK || errno == EAGAIN)
            {
                DEBUG_FUNCTION_LINE("UDP buffer full, dropping packet");
                continue;
            }

            DEBUG_FUNCTION_LINE(
                "sendto failed errno=%d",
                errno
            );

            return false;
        }
    }


    return true;
}


void Shutdown()
{
    if(socket_fd >= 0)
    {
        close(socket_fd);
        socket_fd = -1;
    }
}


}
}