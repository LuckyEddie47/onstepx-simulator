// LimitsHandler.cpp — Horizon and meridian limit commands
// Source reference: Limits.command.cpp

#include "LimitsHandler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

bool LimitsHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)suppressFrame;
    if (!m_cfg->hasMount) return false;

    if (cmd[0] == 'G') {
        // :Gh# — horizon min limit "sDD*"
        // Phase 16 (audit 4.5): firmware uses %+02ld* (max value ±30, never 3 digits).
        // Pre-Phase-16 the simulator used %+03ld* (wrong extra leading zero).
        if (cmd[1] == 'h' && param[0] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            std::sprintf(reply, "%+02ld*", std::lroundf(
                static_cast<float>(m_state->horizonMin)));
            *numericReply = false;
            return true;
        }

        // :Go# — overhead max limit "DD*"
        if (cmd[1] == 'o' && param[0] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            std::sprintf(reply, "%02ld*", std::lroundf(
                static_cast<float>(m_state->horizonMax)));
            *numericReply = false;
            return true;
        }

        // :GXE[m]# — other limits
        if (cmd[1] == 'X' && param[0] == 'E' && param[2] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            *numericReply = false;
            switch (param[1]) {
            case '9': // meridian E in minutes (degrees * 4)
                std::sprintf(reply, "%ld",
                    std::lroundf(static_cast<float>(m_state->meridianLimitEDeg * 4.0)));
                return true;
            case 'A': // meridian W in minutes
                std::sprintf(reply, "%ld",
                    std::lroundf(static_cast<float>(m_state->meridianLimitWDeg * 4.0)));
                return true;
            case 'e': // axis1 east limit in degrees
                std::sprintf(reply, "%ld",
                    std::lroundf(static_cast<float>(m_state->axis1LimitMin)));
                return true;
            case 'w': // axis1 west limit in degrees
                std::sprintf(reply, "%ld",
                    std::lroundf(static_cast<float>(m_state->axis1LimitMax)));
                return true;
            case 'B': // axis1 west limit in hours
                std::sprintf(reply, "%ld",
                    std::lroundf(static_cast<float>(m_state->axis1LimitMax / 15.0)));
                return true;
            case 'C': // axis2 south limit in degrees
                std::sprintf(reply, "%ld",
                    std::lroundf(static_cast<float>(m_state->axis2LimitMin)));
                return true;
            case 'D': // axis2 north limit in degrees
                std::sprintf(reply, "%ld",
                    std::lroundf(static_cast<float>(m_state->axis2LimitMax)));
                return true;
            default: return false;
            }
        }

        return false;
    }

    if (cmd[0] == 'S') {
        *numericReply = true;

        // :Sh[sDD]# — set horizon min (-30 to +30)
        if (cmd[1] == 'h') {
            int deg = static_cast<int>(std::strtol(param, nullptr, 10));
            if (deg >= -30 && deg <= 30) {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->horizonMin = static_cast<double>(deg);
            } else {
                *error = CE_PARAM_RANGE;
            }
            return true;
        }

        // :So[DD]# — set overhead max (60 to 90)
        if (cmd[1] == 'o') {
            int deg = static_cast<int>(std::strtol(param, nullptr, 10));
            if (deg >= 60 && deg <= 90) {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->horizonMax = static_cast<double>(deg);
            } else {
                *error = CE_PARAM_RANGE;
            }
            return true;
        }

        // :SXE9,[n]# / :SXEA,[n]# — set meridian limits in minutes
        if (cmd[1] == 'X' && param[0] == 'E' && param[2] == ',') {
            long l = std::strtol(&param[3], nullptr, 10);
            double degs = l / 4.0;
            switch (param[1]) {
            case '9':
                if (degs >= -360.0 && degs <= 360.0) {
                    std::lock_guard<std::mutex> lk(m_state->mutex);
                    m_state->meridianLimitEDeg = degs;
                } else {
                    *error = CE_PARAM_RANGE;
                }
                return true;
            case 'A':
                if (degs >= -360.0 && degs <= 360.0) {
                    std::lock_guard<std::mutex> lk(m_state->mutex);
                    m_state->meridianLimitWDeg = degs;
                } else {
                    *error = CE_PARAM_RANGE;
                }
                return true;
            default: return false;
            }
        }

        return false;
    }

    return false;
}
