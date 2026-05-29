#pragma once
// AxisHandler.h — Handles axis parameter get/set and driver status commands.
//
// Commands handled (matching Axis.command.cpp):
//
//   :GXAn,p#    Get axis parameter p for axis n
//               p=0   -> parameter count ("14")
//               p=M   -> motor name ("Simulated TMC2209")
//               p=1   -> steps/measure as "value,min,max,type,name"
//               p=2..N -> plausible defaults for remaining parameters
//
//   :GXUn#      Get driver status for axis n
//               Normal (no fault): empty string + '#'
//               If AXIS_DRIVER_STATUS == OFF for this axis: CE_0
//
//   :SXAn,p,v#  Set axis parameter p for axis n to value v -> '1'
//   :SXAn,R#    Revert axis n parameters to defaults -> '1'
//   :SXAC,0#    NV write mode off -> '1'
//   :SXAC,1#    NV write mode on  -> '1'
//
// Axis presence rules (mirrors firmware):
//   Axis 1,2 : present if cfg.hasMount
//   Axis 3   : present if cfg.hasRotator
//   Axis 4-9 : present if (axisNumber - 4) < cfg.numFocusers

#include "HandlerBase.h"

class AxisHandler : public HandlerBase {
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
    // Returns true if axisNumber (1-based) is present for this config.
    bool axisPresent(int axisNumber) const;

    // Format the :GXAn,1# steps-per-measure reply into reply[].
    void formatStepsPerMeasure(int axisNumber, char* reply) const;

    // Format a plausible default parameter reply for parameter index p.
    // These are never queried for correctness by the driver in normal use;
    // they just need to be well-formed non-empty strings.
    static void formatDefaultParam(int p, char* reply);
};
