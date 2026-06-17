// AxisHandler.cpp — Axis parameter get/set and driver status commands.
//
// Protocol source: Axis.command.cpp

#include "handlers/AxisHandler.h"
#include "fault/FaultInjector.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* const AXIS_PARAM_DEFAULTS[] = {
    nullptr,                                // p=0  (handled separately)
    nullptr,                                // p=1  (handled separately)
    "0,-2000,2000,1,Backlash",              // p=2
    "0,-99,99,0,Slew Rate Offset",          // p=3
    "180,0,360,2,Max Slew Rate",            // p=4
    "0,0,360,2,Min Slew Rate",              // p=5
    "0,-999,999,0,Accel/Decel",             // p=6
    "0,-180,180,2,Limits Min",              // p=7
    "0,-180,180,2,Limits Max",              // p=8
    "0,0,1,0,Home Sense",                   // p=9
    "0,0,1,0,Limit Min Sense",              // p=10
    "0,0,1,0,Limit Max Sense",              // p=11
    "0,0,255,0,Microstep Mode",             // p=12
    "0,0,255,0,Current",                    // p=13
};
static constexpr int AXIS_PARAM_COUNT = 14;

bool AxisHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)suppressFrame;

    if (cmd[0] == 'G' && cmd[1] == 'X') {

        // :GXAn,p# — get axis parameter
        if (param[0] == 'A' && param[1] >= '1' && param[1] <= '9' && param[2] == ',') {
            int axisNumber = param[1] - '0';
            if (!axisPresent(axisNumber)) return false;

            const char* pStr = &param[3];

            if (pStr[0] == '0' && pStr[1] == '\0') {
                std::snprintf(reply, 256, "%d", AXIS_PARAM_COUNT);
                return true;
            }
            if (pStr[0] == 'M' && pStr[1] == '\0') {
                setReply(reply, "Simulated TMC2209");
                return true;
            }
            if (pStr[0] == '1' && pStr[1] == '\0') {
                formatStepsPerMeasure(axisNumber, reply);
                return true;
            }
            int p = std::atoi(pStr);
            if (p >= 2 && p < AXIS_PARAM_COUNT) {
                setReply(reply, AXIS_PARAM_DEFAULTS[p]);
                return true;
            }
            reply[0] = '\0';
            return true;
        }

        // :GXUn# — driver status for axis n
        if (param[0] == 'U' && param[1] >= '1' && param[1] <= '9' && param[2] == '\0') {
            int axisNumber = param[1] - '0';
            if (!axisPresent(axisNumber)) {
                *error = CE_0;
                return true;
            }
            // Check for fault-injected axis status (Phase 6)
            if (m_faultInjector) {
                std::string override = m_faultInjector->axisStatusOverride(axisNumber);
                if (!override.empty()) {
                    setReply(reply, override.c_str());
                    return true;
                }
            }
            // Normal: empty string (DEC-018)
            reply[0] = '\0';
            return true;
        }

        return false;
    }

    if (cmd[0] == 'S' && cmd[1] == 'X') {

        if (param[0] == 'A' && param[1] >= '1' && param[1] <= '9' && param[2] == ',') {
            int axisNumber = param[1] - '0';
            if (!axisPresent(axisNumber)) {
                replyNumericFail(reply, numericReply);
                return true;
            }
            replyNumericOk(reply, numericReply);
            return true;
        }

        if (param[0] == 'A' && param[1] >= '1' && param[1] <= '9' &&
            param[2] == ',' && param[3] == 'R' && param[4] == '\0') {
            replyNumericOk(reply, numericReply);
            return true;
        }

        if (param[0] == 'A' && param[1] == 'C' && param[2] == ',') {
            replyNumericOk(reply, numericReply);
            return true;
        }

        return false;
    }

    return false;
}

bool AxisHandler::axisPresent(int axisNumber) const {
    if (axisNumber == 1 || axisNumber == 2) return m_cfg->hasMount;
    if (axisNumber == 3)                    return m_cfg->hasRotator;
    if (axisNumber >= 4 && axisNumber <= 9) return (axisNumber - 4) < m_cfg->numFocusers;
    return false;
}

void AxisHandler::formatStepsPerMeasure(int axisNumber, char* reply) const {
    double value   = 0.0;
    double minVal  = 0.0;
    double maxVal  = 2000000.0;
    const char* name = "Steps/Degree";

    if (axisNumber >= 1 && axisNumber <= 3) {
        value = m_cfg->stepsPerDegree[axisNumber - 1];
    } else if (axisNumber >= 4 && axisNumber <= 9) {
        value  = m_cfg->stepsPerMicron[axisNumber - 4];
        name   = "Steps/Micron";
        maxVal = 100000.0;
    }
    std::snprintf(reply, 256, "%g,%g,%g,2,%s", value, minVal, maxVal, name);
}

void AxisHandler::formatDefaultParam(int p, char* reply) {
    if (p >= 2 && p < AXIS_PARAM_COUNT && AXIS_PARAM_DEFAULTS[p]) {
        setReply(reply, AXIS_PARAM_DEFAULTS[p]);
    } else {
        reply[0] = '\0';
    }
}
