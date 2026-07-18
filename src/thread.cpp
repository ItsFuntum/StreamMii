#include "thread.hpp"
#include "net.hpp"
#include "capture.hpp"
#include "config.hpp"
#include "utils/logger.h"
#include "libs/lz4.h"

#include <memory/mappedmemory.h>
#include <coreinit/thread.h>

#include <malloc.h>
#include <string.h>


namespace StreamMii
{

static OSThread *networkThread = nullptr;
static uint8_t *networkStack = nullptr;

static bool running = false;

constexpr uint32_t STACK_SIZE = 64 * 1024;

static constexpr uint32_t MAX_FRAME_SIZE = 854 * 480 * 2;
static constexpr uint32_t MAX_COMPRESSED_SIZE = MAX_FRAME_SIZE + (MAX_FRAME_SIZE / 255) + 16;

static uint8_t compressedBuffer[MAX_COMPRESSED_SIZE];

static uint32_t frameCounter = 0;

static uint8_t previousFrame[MAX_FRAME_SIZE];
static uint8_t deltaFrame[MAX_FRAME_SIZE];
static bool havePrevious = false;


static int32_t NetworkThreadEntry(int32_t argc, const char **argv)
{
    DEBUG_FUNCTION_LINE("Network thread started");

    while(running)
    {
        if(gNetworkChanged)
        {
            gNetworkChanged = false;

            Net::Shutdown();
            Net::Init(gIP, gPort);

            havePrevious = false;
            frameCounter = 0;
        }

        FrameMessage frame;

        if(GetLatestFrame(frame))
        {
            const uint8_t *current = static_cast<const uint8_t *>(frame.buffer);

            const uint8_t *input = current;

            Net::Compression compression = Net::Compression::LZ4;

            bool keyframe = !havePrevious || (frameCounter % gKeyframeInterval == 0);

            frameCounter++;

            if(gDeltaEnabled && !keyframe)
            {
                auto *cur = reinterpret_cast<const uint16_t*>(current);

                auto *prev = reinterpret_cast<const uint16_t*>(previousFrame);

                auto *dst = reinterpret_cast<uint16_t*>(deltaFrame);

                for(uint32_t i = 0; i < frame.size / 2; i++)
                {
                    dst[i] = cur[i] ^ prev[i];
                }

                input = deltaFrame;
                compression = Net::Compression::DeltaLZ4;
            }
            else
            {
                keyframe = true;
            }


            int compressedSize = LZ4_compress_default(
                (const char *)input,
                (char *)compressedBuffer,
                frame.size,
                sizeof(compressedBuffer)
            );


            if(compressedSize > 0)
            {
                if (Net::SendFrame(
                        compressedBuffer,
                        compressedSize,
                        frame.width,
                        frame.height,
                        frame.pitch,
                        compression,
                        keyframe))
                {
                    memcpy(previousFrame, current, frame.size);
                    havePrevious = true;
                }
                else
                {
                    havePrevious = false; // force a keyframe next time
                }
            }
            else
            {
                havePrevious = false;
            }


            ReleaseBuffer(frame.buffer);
        }
        else
        {
            OSSleepTicks(OSMillisecondsToTicks(1));
        }
    }


    DEBUG_FUNCTION_LINE(
        "Network thread stopped"
    );


    return 0;
}



bool InitThread()
{
    DEBUG_FUNCTION_LINE("Network thread initializing");

    if(running)
        return true;

    networkThread = (OSThread *)memalign(0x20, sizeof(OSThread));


    networkStack = (uint8_t *)memalign(0x20, STACK_SIZE);


    if(!networkThread || !networkStack)
    {
        DEBUG_FUNCTION_LINE(
            "Thread allocation failed"
        );

        return false;
    }


    running = true;

    DEBUG_FUNCTION_LINE(
        "Thread=%p Stack=%p",
        networkThread,
        networkStack
    );

    // Thread priority is low here
    if(!OSCreateThread(
        networkThread,
        NetworkThreadEntry,
        0,
        nullptr,
        networkStack + STACK_SIZE,
        STACK_SIZE,
        21,
        OS_THREAD_ATTRIB_AFFINITY_ANY))
    {
        DEBUG_FUNCTION_LINE(
            "OSCreateThread failed"
        );
    
        free(networkStack);
        free(networkThread);
    
        networkStack = nullptr;
        networkThread = nullptr;
    
        running = false;
    
        return false;
    }


    OSSetThreadName(
        networkThread,
        "StreamMii Network"
    );


    OSResumeThread(networkThread);


    return true;
}



void ShutdownThread()
{
    if(!running)
        return;


    running = false;
    havePrevious = false;
    frameCounter = 0;


    if(networkThread)
    {
        OSJoinThread(networkThread, nullptr);
    }


    free(networkStack);
    free(networkThread);


    networkStack = nullptr;
    networkThread = nullptr;
}

}