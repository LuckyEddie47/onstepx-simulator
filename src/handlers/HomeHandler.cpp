// HomeHandler.cpp — Home command handler
// Source reference: Home.command.cpp

#include "HomeHandler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

bool HomeHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)suppressFrame;
    if (!m_cfg->hasMount) return false;
    if (cmd[0] != 'h') return false;

    // :h?# — get home status
    // Returns: "hasSense,axis1Offset,axis2Offset"# (3 fields — source confirmed)
    if (cmd[1] == '?' && param[0] == 0) {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        int hasSense = m_cfg->hasHomeSense ? 1 : 0;
        std::sprintf(reply, "%d,%ld,%ld",
                     hasSense,
                     m_state->homeOffsetAxis1,
                     m_state->homeOffsetAxis2);
        *numericReply = false;
        return true;
    }

    // :hAn# — set auto home at boot (0 or 1), no reply
    if (cmd[1] == 'A' && param[1] == 0) {
        *numericReply = false;
        switch (param[0]) {
        case '0':
            { std::lock_guard<std::mutex> lk(m_state->mutex);
              m_state->autoHomeAtBoot = false; }
            break;
        case '1':
            { std::lock_guard<std::mutex> lk(m_state->mutex);
              m_state->autoHomeAtBoot = true; }
            break;
        default:
            *error = CE_PARAM_RANGE;
            break;
        }
        return true;
    }

    // :hC# — begin homing
    // Returns: nothing. *commandError = request() result.
    // Firmware sets numericReply=false regardless of error.
    if (cmd[1] == 'C' && param[0] == 0) {
        *numericReply = false;
        *error = m_sm->beginHome();
        return true;
    }

    // :hC1,n# — set axis1 home offset or reverse
    if (cmd[1] == 'C' && param[0] == '1' && param[1] == ',') {
        *numericReply = false;
        if (param[2] == 'R' && param[3] == 0) {
            // Reverse home sense direction — toggle (sim stores but doesn't act on it)
            // No state change needed beyond acknowledgement
        } else {
            long l = std::strtol(&param[2], nullptr, 10);
            if (l >= -648000 && l <= 648000) {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->homeOffsetAxis1 = l;
            } else {
                *error = CE_PARAM_RANGE;
            }
        }
        return true;
    }

    // :hC2,n# — set axis2 home offset or reverse
    if (cmd[1] == 'C' && param[0] == '2' && param[1] == ',') {
        *numericReply = false;
        if (param[2] == 'R' && param[3] == 0) {
            // Reverse home sense direction — noop in sim
        } else {
            long l = std::strtol(&param[2], nullptr, 10);
            if (l >= -648000 && l <= 648000) {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->homeOffsetAxis2 = l;
            } else {
                *error = CE_PARAM_RANGE;
            }
        }
        return true;
    }

    // :hF# — reset position to home (no slew)
    // Firmware: reset(true) then park.reset() then limits.enabled(...)
    if (cmd[1] == 'F' && param[0] == 0) {
        *numericReply = false;
        *error = m_sm->resetHome();
        return true;
    }

    return false;
}
