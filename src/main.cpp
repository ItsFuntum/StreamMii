#include <gx2/display.h>
#include "utils/logger.h"
#include "capture.hpp"
#include "net.hpp"
#include "thread.hpp"
#include "config.hpp"

#include <wups.h>


WUPS_PLUGIN_NAME("StreamMii");
WUPS_PLUGIN_DESCRIPTION("Wii U screen streaming plugin for Aroma");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("Funtum");
WUPS_PLUGIN_LICENSE("MIT");


WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("streammii");

static bool initialized = false;


INITIALIZE_PLUGIN()
{
    initLogging();

    StreamMii::InitConfig();

    DEBUG_FUNCTION_LINE("StreamMii initialized");

    deinitLogging();
}

ON_APPLICATION_START()
{
    initLogging();

    DEBUG_FUNCTION_LINE("Application starting");

    if(initialized)
    {
        DEBUG_FUNCTION_LINE("Restarting StreamMii for new application");

        StreamMii::ShutdownThread();
        StreamMii::ShutdownCapture();
        StreamMii::Net::Shutdown();
    }

    StreamMii::Net::Init(StreamMii::gIP, StreamMii::gPort);

    StreamMii::InitCapture();

    StreamMii::InitThread();

    initialized = true;
}

ON_APPLICATION_ENDS() {
    deinitLogging();
}

ON_APPLICATION_REQUESTS_EXIT()
{
    if(!initialized)
        return;

    DEBUG_FUNCTION_LINE("Application exiting");

    StreamMii::ShutdownThread();
    
    StreamMii::ShutdownCapture();

    StreamMii::Net::Shutdown();

    initialized = false;
}