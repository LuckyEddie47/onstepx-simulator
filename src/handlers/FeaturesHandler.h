#pragma once
// FeaturesHandler.h — Handles auxiliary feature get/set commands.
//
// Protocol source: Features.command.cpp
//
// Commands handled:
//
//   :GXY0#        Active features mask — 8-char string, one per slot, '0'/'1'
//   :GXY[n]#      Feature info for slot n (1-8)
//                   -> CE_0 if slot is OFF or HIDDEN_SWITCH
//                   -> "name,purpose"# otherwise
//                   MOMENTARY_SWITCH (5) and COVER_SWITCH (7) report as purpose=1 (SWITCH)
//   :GXX[n]#      Feature value/state for slot n (1-8)
//                   -> CE_0 if slot is OFF or HIDDEN_SWITCH
//                   -> SWITCH/MOMENTARY/COVER: "0"# or "1"#
//                   -> ANALOG_OUTPUT: "0"-"255"#
//                   -> DEW_HEATER: "enabled,zero,span,deltaT"#
//                   -> INTERVALOMETER: "currentCount,exposure,delay,count"#
//
//   :SXX[n],V[v]# Set value / enable
//   :SXX[n],Z[v]# Set dew-heater zero offset
//   :SXX[n],S[v]# Set dew-heater span
//   :SXX[n],E[v]# Set intervalometer exposure (seconds)
//   :SXX[n],D[v]# Set intervalometer delay (seconds)
//   :SXX[n],C[v]# Set intervalometer count
//
// Feature purpose codes (from Constants.h):
//   SWITCH=1, ANALOG_OUTPUT=2, DEW_HEATER=3, INTERVALOMETER=4
//   MOMENTARY_SWITCH=5, HIDDEN_SWITCH=6, COVER_SWITCH=7

#include "HandlerBase.h"

class FeaturesHandler : public HandlerBase {
public:
    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error
    ) override;

private:
    // Returns true if this config has at least one active (non-OFF) feature.
    bool hasAnyFeature() const;

    // Returns true if slot idx (0-based) is visible (non-OFF, non-HIDDEN).
    // HIDDEN_SWITCH slots exist but are not reported via GXY/GXX.
    static bool isVisible(int purpose);

    // Returns the reported purpose for GXY[n]#.
    // MOMENTARY_SWITCH -> 1 (SWITCH); COVER_SWITCH -> 1 (SWITCH); others as-is.
    static int reportedPurpose(int purpose);

    // Format :GXX[n]# value reply into reply[] for a given slot.
    // Caller has already validated slot is visible.
    void formatValue(int slot, char* reply) const;

    // Format intervalometer exposure with variable decimal places:
    //   >= 60s  -> 0 dp   (e.g. "120")
    //   >= 10s  -> 1 dp   (e.g. "30.0")
    //   >= 1s   -> 2 dp   (e.g. "1.50")
    //   <  1s   -> 3 dp   (e.g. "0.250")
    static void formatExposure(float secs, char* buf, int bufLen);
};
