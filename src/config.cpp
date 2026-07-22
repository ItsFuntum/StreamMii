#include "config.hpp"
#include "thread.hpp"

#include <wups.h>
#include <wups/button_combo/api.h>
#include <wups/config/WUPSConfigItemButtonCombo.h>
#include <wups/config/WUPSConfigCategory.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemIntegerRange.h>
#include <wups/config/WUPSConfigItemMultipleValues.h>
#include <wups/storage.h>
#include <forward_list>
#include <string.h>

#include "utils/logger.h"


namespace StreamMii
{

bool gEnabled = true;

uint32_t gResolution = 1; // 320x180
uint32_t gWidth = 320;
uint32_t gHeight = 180;

uint32_t gFrameSkip = 4; // 15 FPS Limit

bool gDeltaEnabled = false;

uint32_t gKeyframeInterval = 60;

uint32_t gIPLastOctet = 100;
char gIP[16] = "192.168.1.100";

uint32_t gPort = 4242;

bool gResolutionChanged = false;
bool gNetworkChanged = false;
bool gCompressionChanged = false;

CompressionMode gCompressionMode = CompressionMode::LZ4;
uint32_t compression = 0;

uint32_t gJPEGQuality = 70;

std::forward_list<WUPSButtonComboAPI::ButtonCombo> sButtonComboInstances;

WUPSButtonCombo_ComboHandle gDecreaseResolutionComboHandle;
WUPSButtonCombo_ComboHandle gIncreaseResolutionComboHandle;

constexpr WUPSButtonCombo_Buttons DEFAULT_DECREASE_COMBO =
    WUPS_BUTTON_COMBO_BUTTON_TV |
    WUPS_BUTTON_COMBO_BUTTON_ZL;

constexpr WUPSButtonCombo_Buttons DEFAULT_INCREASE_COMBO =
    WUPS_BUTTON_COMBO_BUTTON_TV |
    WUPS_BUTTON_COMBO_BUTTON_ZR;


void ConfigMenuClosedCallback()
{
    WUPSStorageAPI::SaveStorage();
}

void ipCallback(ConfigItemIntegerRange *, int32_t value)
{
    if(value < 1)
        value = 1;

    if(value > 254)
        value = 254;

    gIPLastOctet = value;

    snprintf(
        gIP,
        sizeof(gIP),
        "192.168.1.%d",
        gIPLastOctet
    );

    WUPSStorageAPI::Store("ip", gIPLastOctet);

    gNetworkChanged = true;
}

void SetResolution(uint32_t value)
{
    if (value > 4)
        value = 4;

    gResolution = value;

    switch(value)
    {
        case 0:
            gWidth = 160;
            gHeight = 90;
            break;

        case 1:
            gWidth = 320;
            gHeight = 180;
            break;

        case 2:
            gWidth = 480;
            gHeight = 270;
            break;

        case 3:
            gWidth = 640;
            gHeight = 360;
            break;

        case 4:
            gWidth = 854;
            gHeight = 480;
            break;
    }

    WUPSStorageAPI::Store("resolution", gResolution);

    gResolutionChanged = true;

    DEBUG_FUNCTION_LINE("Resolution changed to %ux%u", gWidth, gHeight);
}

void resolutionCallback(ConfigItemMultipleValues *, uint32_t value)
{
    SetResolution(value);
}

void DecreaseResolutionCallback(WUPSButtonCombo_ControllerTypes, WUPSButtonCombo_ComboHandle, void *)
{
    if (gResolution > 0)
    {
        SetResolution(gResolution - 1);
    }
    else
    {
        DEBUG_FUNCTION_LINE("Already at minimum resolution: %ux%u", gWidth, gHeight);
    }
}

void IncreaseResolutionCallback(WUPSButtonCombo_ControllerTypes, WUPSButtonCombo_ComboHandle, void *)
{
    if (gResolution < 4)
    {
        SetResolution(gResolution + 1);
    }
    else
    {
        DEBUG_FUNCTION_LINE("Already at maximum resolution: %ux%u", gWidth, gHeight);
    }
}

void fpsCallback(ConfigItemMultipleValues *, uint32_t value)
{
    gFrameSkip = value;

    WUPSStorageAPI::Store("fps", value);
}

void compressionCallback(ConfigItemMultipleValues *, uint32_t value)
{
    gCompressionMode = static_cast<CompressionMode>(value);

    WUPSStorageAPI::Store("compression", value);

    gCompressionChanged = true;
}

void jpegQualityCallback(ConfigItemIntegerRange *, int32_t value)
{
    if(value < 10)
        value = 10;

    if(value > 100)
        value = 100;

    gJPEGQuality = value;

    WUPSStorageAPI::Store("jpeg_quality", gJPEGQuality);
}



void buttonComboCallback(ConfigItemButtonCombo *item, uint32_t newValue)
{
    DEBUG_FUNCTION_LINE(
        "Button combo changed: %s -> 0x%08X",
        item->identifier,
        newValue
    );
}



void boolCallback( ConfigItemBoolean *item, bool value)
{
    if(strcmp(item->identifier, "delta") == 0)
    {
        gDeltaEnabled = value;
    }
    else if(strcmp(item->identifier, "enabled") == 0)
    {
        gEnabled = value;
    }

    WUPSStorageAPI::Store(item->identifier, value);
}



void integerCallback(ConfigItemIntegerRange *item, int32_t value)
{
    if(strcmp(item->identifier, "keyframe") == 0)
    {
        gKeyframeInterval = value;
    }
    else if(strcmp(item->identifier, "port") == 0)
    {
        gPort = value;
        gNetworkChanged = true;
    }


    WUPSStorageAPI::Store(item->identifier, value);
}



WUPSConfigAPICallbackStatus ConfigMenuOpenedCallback(WUPSConfigCategoryHandle rootHandle)
{
    WUPSConfigCategory root(rootHandle);


    root.add(
        WUPSConfigItemBoolean::Create(
            "enabled",
            "Enable StreamMii",
            true,
            gEnabled,
            boolCallback
        )
    );


    root.add(
        WUPSConfigItemIntegerRange::Create(
            "ip",
            "Receiver's Local IP Address (192.168.1.X)",
            100,
            gIPLastOctet,
            1,
            254,
            ipCallback
        )
    );


    root.add(
        WUPSConfigItemIntegerRange::Create(
            "port",
            "Port",
            4242,
            gPort,
            1024,
            9999,
            integerCallback
        )
    );


    constexpr WUPSConfigItemMultipleValues::ValuePair resolutions[] =
    {
        {0, "160x90"},
        {1, "320x180"},
        {2, "480x270"},
        {3, "640x360"},
        {4, "854x480"}
    };


    root.add(
        WUPSConfigItemMultipleValues::CreateFromValue(
            "resolution",
            "Resolution",
            1,
            gResolution,
            resolutions,
            resolutionCallback
        )
    );


    constexpr WUPSConfigItemMultipleValues::ValuePair fpsOptions[] =
    {
        {1, "60 FPS"},
        {2, "30 FPS"},
        {3, "20 FPS"},
        {4, "15 FPS"},
        {6, "10 FPS"},
        {12, "5 FPS"},
        {60, "1 FPS"}
    };

    root.add(
        WUPSConfigItemMultipleValues::CreateFromValue(
            "fps",
            "Max Frame Rate",
            4,
            gFrameSkip,
            fpsOptions,
            fpsCallback
        )
    );


    constexpr WUPSConfigItemMultipleValues::ValuePair compressionOptions[] =
    {
        {0, "LZ4"},
        {1, "JPEG"},
    };

    root.add(
        WUPSConfigItemMultipleValues::CreateFromValue(
            "compression",
            "Compression",
            0,
            static_cast<uint32_t>(gCompressionMode),
            compressionOptions,
            compressionCallback
        )
    );


    root.add(
        WUPSConfigItemIntegerRange::Create(
            "jpeg_quality",
            "JPEG Quality",
            70,
            gJPEGQuality,
            10,
            100,
            jpegQualityCallback
        )
    );


    root.add(
        WUPSConfigItemBoolean::Create(
            "delta",
            "LZ4 Delta encoding",
            false,
            gDeltaEnabled,
            boolCallback
        )
    );


    root.add(
        WUPSConfigItemIntegerRange::Create(
            "keyframe",
            "Keyframe interval",
            60,
            gKeyframeInterval,
            1,
            300,
            integerCallback
        )
    );

    root.add(
        WUPSConfigItemButtonCombo::Create(
            "decrease_resolution_combo",
            "Decrease Resolution Combo",
            WUPS_BUTTON_COMBO_BUTTON_TV |
            WUPS_BUTTON_COMBO_BUTTON_ZL,
            gDecreaseResolutionComboHandle,
            buttonComboCallback
        )
    );

    root.add(
        WUPSConfigItemButtonCombo::Create(
            "increase_resolution_combo",
            "Increase Resolution Combo",
            WUPS_BUTTON_COMBO_BUTTON_TV |
            WUPS_BUTTON_COMBO_BUTTON_ZR,
            gIncreaseResolutionComboHandle,
            buttonComboCallback
        )
    );


    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}



void InitConfig()
{
    WUPSConfigAPIOptionsV1 options =
    {
        .name = "StreamMii"
    };


    WUPSStorageAPI::GetOrStoreDefault(
        "enabled",
        gEnabled,
        true
    );


    WUPSStorageAPI::GetOrStoreDefault(
        "ip",
        gIPLastOctet,
        (uint32_t)100
    );
    ipCallback(nullptr, gIPLastOctet);

    WUPSStorageAPI::GetOrStoreDefault(
        "port",
        gPort,
        (uint32_t)4242
    );

    WUPSStorageAPI::GetOrStoreDefault(
        "resolution",
        gResolution,
        (uint32_t)1
    );

    resolutionCallback(nullptr, gResolution);

    WUPSStorageAPI::GetOrStoreDefault(
        "fps",
        gFrameSkip,
        (uint32_t)4
    );

    WUPSStorageAPI::GetOrStoreDefault(
        "compression",
        compression,
        (uint32_t)0
    );

    gCompressionMode = static_cast<CompressionMode>(compression);

    WUPSStorageAPI::GetOrStoreDefault(
        "jpeg_quality",
        gJPEGQuality,
        (uint32_t)70
    );

    WUPSStorageAPI::GetOrStoreDefault(
        "keyframe",
        gKeyframeInterval,
        (uint32_t)60
    );

    WUPSStorageAPI::GetOrStoreDefault(
        "delta",
        gDeltaEnabled,
        false
    );


    WUPSConfigAPI_Init(
        options,
        ConfigMenuOpenedCallback,
        ConfigMenuClosedCallback
    );


    WUPSButtonCombo_ComboStatus decreaseStatus = WUPS_BUTTON_COMBO_COMBO_STATUS_INVALID_STATUS;

    WUPSButtonCombo_Error decreaseError = WUPS_BUTTON_COMBO_ERROR_UNKNOWN_ERROR;

    auto decreaseResult =
        WUPSButtonComboAPI::CreateComboPressDown(
            "StreamMii: Decrease Resolution",
            DEFAULT_DECREASE_COMBO,
            DecreaseResolutionCallback,
            nullptr,
            decreaseStatus,
            decreaseError
        );

    if (decreaseResult &&
        decreaseError == WUPS_BUTTON_COMBO_ERROR_SUCCESS)
    {
        gDecreaseResolutionComboHandle =
            decreaseResult->getHandle();

        DEBUG_FUNCTION_LINE(
            "Decrease combo: error=%d status=%d",
            decreaseError,
            decreaseStatus
        );

        if (decreaseStatus == WUPS_BUTTON_COMBO_COMBO_STATUS_VALID)
        {
            DEBUG_FUNCTION_LINE(
                "Decrease combo is VALID and ACTIVE"
            );
        }
        else if (decreaseStatus == WUPS_BUTTON_COMBO_COMBO_STATUS_CONFLICT)
        {
            DEBUG_FUNCTION_LINE(
                "Decrease combo has a CONFLICT and is INACTIVE"
            );
        }

        // Keep the ButtonCombo object alive.
        sButtonComboInstances.emplace_front(
            std::move(*decreaseResult)
        );
    }
    else
    {
        DEBUG_FUNCTION_LINE(
            "Failed to register decrease combo: error=%d status=%d",
            decreaseError,
            decreaseStatus
        );
    }


    WUPSButtonCombo_ComboStatus increaseStatus = WUPS_BUTTON_COMBO_COMBO_STATUS_INVALID_STATUS;

    WUPSButtonCombo_Error increaseError = WUPS_BUTTON_COMBO_ERROR_UNKNOWN_ERROR;

    auto increaseResult =
        WUPSButtonComboAPI::CreateComboPressDown(
            "StreamMii: Increase Resolution",
            DEFAULT_INCREASE_COMBO,
            IncreaseResolutionCallback,
            nullptr,
            increaseStatus,
            increaseError
        );

    if (increaseResult &&
        increaseError == WUPS_BUTTON_COMBO_ERROR_SUCCESS)
    {
        gIncreaseResolutionComboHandle =
            increaseResult->getHandle();

        DEBUG_FUNCTION_LINE(
            "Increase combo: error=%d status=%d",
            increaseError,
            increaseStatus
        );

        if (increaseStatus == WUPS_BUTTON_COMBO_COMBO_STATUS_VALID)
        {
            DEBUG_FUNCTION_LINE(
                "Increase combo is VALID and ACTIVE"
            );
        }
        else if (increaseStatus == WUPS_BUTTON_COMBO_COMBO_STATUS_CONFLICT)
        {
            DEBUG_FUNCTION_LINE(
                "Increase combo has a CONFLICT and is INACTIVE"
            );
        }

        // Keep the ButtonCombo object alive.
        sButtonComboInstances.emplace_front(
            std::move(*increaseResult)
        );
    }
    else
    {
        DEBUG_FUNCTION_LINE(
            "Failed to register increase combo: error=%d status=%d",
            increaseError,
            increaseStatus
        );
    }
}


}