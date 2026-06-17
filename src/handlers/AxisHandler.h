#pragma once
// AxisHandler.h — Handles axis parameter get/set and driver status commands.
//
// Commands handled (matching Axis.command.cpp):
//
//   :GXAn,p#    Get axis parameter p for axis n
//   :GXUn#      Get driver status for axis n
//               Normal (no fault): empty string + '#'
//               With fault injection: FaultInjector::axisStatusOverride(n)
//               If AXIS_DRIVER_STATUS == OFF for this axis: CE_0
//   :SXAn,p,v#  Set axis parameter p for axis n to value v -> '1'
//   :SXAn,R#    Revert axis n parameters to defaults -> '1'
//   :SXAC,0#    NV write mode off -> '1'
//   :SXAC,1#    NV write mode on  -> '1'
//
// Axis presence rules:
//   Axis 1,2 : present if cfg.hasMount
//   Axis 3   : present if cfg.hasRotator
//   Axis 4-9 : present if (axisNumber - 4) < cfg.numFocusers

#include "HandlerBase.h"

// Forward declaration — FaultInjector is optional
class FaultInjector;

class AxisHandler : public HandlerBase {
public:
    // Register fault injector (optional; nullptr = no fault injection)
    void setFaultInjector(FaultInjector* fi) { m_faultInjector = fi; }

    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error
    ) override;

private:
    FaultInjector* m_faultInjector = nullptr;

    bool axisPresent(int axisNumber) const;
    void formatStepsPerMeasure(int axisNumber, char* reply) const;
    static void formatDefaultParam(int p, char* reply);
};
