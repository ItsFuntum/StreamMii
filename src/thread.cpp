#include "thread.hpp"
#include "net.hpp"
#include "capture.hpp"
#include "utils/logger.h"

#include <memory/mappedmemory.h>
#include <coreinit/thread.h>
#include <coreinit/messagequeue.h>

#include <malloc.h>
#include <string.h>


namespace StreamMii
{

static OSThread *networkThread = nullptr;
static uint8_t *networkStack = nullptr;

static OSMessageQueue queue;

static constexpr uint32_t QUEUE_SIZE = 8;
static OSMessage messages[QUEUE_SIZE];

static FrameMessage framePool[QUEUE_SIZE];
static bool framePoolUsed[QUEUE_SIZE];

static bool running = false;

constexpr uint32_t STACK_SIZE = 64 * 1024;

static FrameMessage *AllocateFrameMessage()
{
    for(uint32_t i = 0; i < QUEUE_SIZE; i++)
    {
        if(!framePoolUsed[i])
        {
            framePoolUsed[i] = true;
            return &framePool[i];
        }
    }

    return nullptr;
}


static void FreeFrameMessage(FrameMessage *msg)
{
    for(uint32_t i = 0; i < QUEUE_SIZE; i++)
    {
        if(&framePool[i] == msg)
        {
            framePoolUsed[i] = false;
            return;
        }
    }
}

static int32_t NetworkThreadEntry(int32_t argc, const char **argv)
{
    DEBUG_FUNCTION_LINE(
        "Network thread started"
    );

    OSMessage msg;


    while(running)
    {
        if(OSReceiveMessage(&queue, &msg, OS_MESSAGE_FLAGS_BLOCKING))
        {
            if(msg.message == nullptr)
                break;

            FrameMessage *frame = (FrameMessage *)msg.message;

            Net::SendFrame(
                frame->buffer,
                frame->size,
                frame->width,
                frame->height,
                frame->pitch
            );

            ReleaseBuffer(frame->buffer);

            FreeFrameMessage(frame);
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

    OSInitMessageQueue(
        &queue,
        messages,
        QUEUE_SIZE
    );

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
        1,
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


    OSMessage msg = {};
    msg.message = nullptr;


    OSSendMessage(
        &queue,
        &msg,
        OS_MESSAGE_FLAGS_NONE
    );


    OSJoinThread(
        networkThread,
        nullptr
    );


    free(networkStack);
    free(networkThread);


    networkStack = nullptr;
    networkThread = nullptr;
}



bool QueueFrame(const FrameMessage &frame)
{
    FrameMessage *copy = AllocateFrameMessage();

    if(!copy)
    {
        ReleaseBuffer(frame.buffer);
        return false;
    }

    *copy = frame;

    OSMessage msg = {};
    msg.message = copy;


    if(!OSSendMessage(
        &queue,
        &msg,
        OS_MESSAGE_FLAGS_NONE))
    {
        DEBUG_FUNCTION_LINE(
            "Frame queue full"
        );

        ReleaseBuffer(frame.buffer);
        FreeFrameMessage(copy);
        return false;
    }

    return true;
}


}