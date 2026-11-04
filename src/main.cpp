#include "capture.hpp"
#include "config.hpp"
#include "net.hpp"
#include "thread.hpp"
#include "utils/logger.h"

#include <gx2/display.h>
#include <notifications/notifications.h>
#include <wups.h>


WUPS_PLUGIN_NAME("StreamMii");
WUPS_PLUGIN_DESCRIPTION("Wii U screen streaming plugin for Aroma");
WUPS_PLUGIN_VERSION("v2.0");
WUPS_PLUGIN_AUTHOR("Funtum");
WUPS_PLUGIN_LICENSE("GPLv3+");


WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("streammii");

static bool initialized = false;

namespace StreamMii {
    void SetEnabled(bool enabled) {
        if (gEnabled == enabled && initialized == enabled)
            return;

        if (!enabled) {
            if (initialized) {
                ShutdownThread();
                ShutdownCapture();
                Net::Shutdown();
                initialized = false;
            }

            gEnabled = false;
            return;
        }

        if (initialized) {
            gEnabled = true;
            return;
        }

        if (!Net::Init(gIP, gPort)) {
            gEnabled = false;
            return;
        }

        InitCapture();

        if (!InitThread()) {
            ShutdownThread();
            ShutdownCapture();
            Net::Shutdown();
            gEnabled = false;
            return;
        }

        gEnabled    = true;
        initialized = true;
    }

} // namespace StreamMii


INITIALIZE_PLUGIN() {
    initLogging();

    NotificationModule_InitLibrary();

    StreamMii::InitConfig();

    DEBUG_FUNCTION_LINE("StreamMii initialized");

    StreamMii::SetEnabled(StreamMii::gAutoStart);

    deinitLogging();
}

DEINITIALIZE_PLUGIN() {
    StreamMii::sButtonComboInstances.clear();

    NotificationModule_DeInitLibrary();
}

ON_APPLICATION_START() {
    initLogging();

    DEBUG_FUNCTION_LINE("Application starting");

    if (StreamMii::gEnabled) {
        if (initialized) {
            StreamMii::ShutdownThread();
            StreamMii::ShutdownCapture();
            StreamMii::Net::Shutdown();

            initialized = false;
        }

        if (StreamMii::Net::Init(StreamMii::gIP, StreamMii::gPort)) {
            StreamMii::InitCapture();

            if (StreamMii::InitThread()) {
                initialized = true;
            } else {
                StreamMii::ShutdownThread();
                StreamMii::ShutdownCapture();
                StreamMii::Net::Shutdown();
                StreamMii::gEnabled = false;
            }
        } else {
            StreamMii::gEnabled = false;
        }
    }

    if (StreamMii::gEnabled && initialized) {
        StreamMii::ShowStreamMiiNotification();
    }

    deinitLogging();
}

ON_APPLICATION_ENDS() {
    deinitLogging();
}

ON_APPLICATION_REQUESTS_EXIT() {
    if (!initialized)
        return;

    DEBUG_FUNCTION_LINE("Application exiting");

    StreamMii::ShutdownThread();

    StreamMii::ShutdownCapture();

    StreamMii::Net::Shutdown();

    initialized = false;
}
