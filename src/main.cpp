#include <gx2/display.h>
#include "utils/logger.h"
#include "capture.hpp"
#include "net.hpp"
#include "thread.hpp"

#include <wups.h>


WUPS_PLUGIN_NAME("StreamMii");
WUPS_PLUGIN_DESCRIPTION("Wii U screen streaming plugin for Aroma");
WUPS_PLUGIN_VERSION("v1.0");
WUPS_PLUGIN_AUTHOR("Funtum");
WUPS_PLUGIN_LICENSE("MIT");


WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("streammii");


INITIALIZE_PLUGIN()
{
    initLogging();
    DEBUG_FUNCTION_LINE("StreamMii initialized");
}

ON_APPLICATION_START()
{
    DEBUG_FUNCTION_LINE("Application start");

    StreamMii::Net::Init("192.168.1.1", 4242); // Local PC IP Address and Port

    StreamMii::InitThread();

    StreamMii::InitCapture();
}

ON_APPLICATION_REQUESTS_EXIT()
{
    StreamMii::ShutdownThread();

    StreamMii::ShutdownCapture();

    StreamMii::Net::Shutdown();
}

DEINITIALIZE_PLUGIN()
{
    deinitLogging();
    DEBUG_FUNCTION_LINE("StreamMii deinitialized");
}