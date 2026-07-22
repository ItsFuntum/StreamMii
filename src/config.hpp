#pragma once

#include <wups/button_combo/api.h>
#include <forward_list>
#include <stdint.h>

namespace StreamMii
{

extern bool gEnabled;

extern uint32_t gWidth;
extern uint32_t gHeight;

extern uint32_t gFrameSkip;

extern bool gDeltaEnabled;

extern uint32_t gKeyframeInterval;

extern char gIP[16];
extern uint32_t gPort;

extern bool gResolutionChanged;
extern bool gNetworkChanged;
extern bool gCompressionChanged;

enum class CompressionMode : uint32_t
{
    LZ4 = 0,
    JPEG = 1
};

extern CompressionMode gCompressionMode;

extern uint32_t gJPEGQuality;


extern std::forward_list<WUPSButtonComboAPI::ButtonCombo> sButtonComboInstances;


void InitConfig();

}