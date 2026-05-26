// HomeHandler.cpp — Home command handler.
//
// Protocol source: Home.command.cpp

#include "handlers/HomeHandler.h"

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

    if (cmd[0] != 'h') return false;

    // :h?# — Get home status "hasSense,axis1Offset,axis2Offset#"
    if (cmd[1] == '?' && param[0] == '\0') {
        bool hasSense;
        long off1, off2;
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            hasSense = m_cfg->hasHomeSense;
            off1     = m_state->homeOffsetAxis1;
            off2     = m_state->homeOffsetAxis2;
        }
        std::snprintf(reply, 256, "%d,%ld,%ld", hasSense ? 1 : 0, off1, off2);
        *numericReply = false;
        return true;
    }

    // :hA[0|1]# — Set auto home at boot
    if (cmd[1] == 'A' && param[1] == '\0') {
        switch (param[0]) {
            case '0': {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->autoHomeAtBoot = false;
                break;
            }
            case '1': {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->autoHomeAtBoot = true;
                break;
            }
            default:
                *error = CE_PARAM_RANGE;
        }
        *numericReply = false;
        return true;
    }

    // :hC# — Move mount to home position
    if (cmd[1] == 'C' && param[0] == '\0') {
        *error = m_msm->beginHome();
        *numericReply = false;
        return true;
    }

    // :hC1,n# or :hC1,R# — Set axis1 home offset or reverse
    if (cmd[1] == 'C' && param[0] == '1' && param[1] == ',') {
        *numericReply = false;
        const char* val = &param[2];
        if (!(val[0] == 'R' && val[1] == '\0')) {
            long l = std::atol(val);
            if (l < -648000 || l > 648000) {
                *error = CE_PARAM_RANGE;
            } else {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->homeOffsetAxis1 = l;
            }
        }
        return true;
    }

    // :hC2,n# or :hC2,R# — Set axis2 home offset or reverse
    if (cmd[1] == 'C' && param[0] == '2' && param[1] == ',') {
        *numericReply = false;
        const char* val = &param[2];
        if (!(val[0] == 'R' && val[1] == '\0')) {
            long l = std::atol(val);
            if (l < -648000 || l > 648000) {
                *error = CE_PARAM_RANGE;
            } else {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->homeOffsetAxis2 = l;
            }
        }
        return true;
    }

    // :hF# — Reset mount at home position (cold start)
    // Mirrors firmware: reset(true); park.reset(); limits.enabled(site.isDateTimeReady())
    if (cmd[1] == 'F' && param[0] == '\0') {
        *error = m_msm->resetHome();
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->parkState       = PS_UNPARKED;
            m_state->parkPositionSet = false;
        }
        *numericReply = false;
        return true;
    }

    return false;
}
