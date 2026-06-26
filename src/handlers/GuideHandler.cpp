// GuideHandler.cpp — Guide command handler.
//
// Protocol source: Guide.command.cpp, Guide.cpp

#include "handlers/GuideHandler.h"
#include "state/SiderealConstants.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool GuideHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    *suppressFrame = false;

    // :GX90# — Get pulse guide rate (n.nn#)
    if (cmd[0] == 'G' && cmd[1] == 'X' &&
        param[0] == '9' && param[1] == '0' && param[2] == '\0') {
        float rate;
        { std::lock_guard<std::mutex> lk(m_state->mutex); rate = rateIndexToSidereal(m_state->pulseRateSelect); }
        std::snprintf(reply, 256, "%.2f", rate);
        *numericReply = false;
        return true;
    }

    // -----------------------------------------------------------------------
    // M - Movement / guide commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'M') {

        // :Mg[d][n]#  or  :MG[d][n]#
        if (cmd[1] == 'g' || cmd[1] == 'G') {
            char dir = param[0];
            if (dir != 'n' && dir != 's' && dir != 'e' && dir != 'w') {
                *error = CE_CMD_UNKNOWN;
                return true;
            }
            int ms = std::atoi(&param[1]);
            if (ms < 0) { *error = CE_PARAM_RANGE; return true; }
            int rateSelect;
            { std::lock_guard<std::mutex> lk(m_state->mutex); rateSelect = m_state->pulseRateSelect; }
            *error = startGuide(dir, rateSelect, ms, true);
            if (cmd[1] == 'g') *numericReply = false;
            return true;
        }

        // :Mw# :Me# :Mn# :Ms# — Continuous guide
        if ((cmd[1] == 'w' || cmd[1] == 'e' || cmd[1] == 'n' || cmd[1] == 's') &&
            param[0] == '\0') {
            int rateSelect;
            { std::lock_guard<std::mutex> lk(m_state->mutex); rateSelect = m_state->guideRateSelect; }
            *error        = startGuide(cmd[1], rateSelect, 0, false);
            *numericReply = false;
            return true;
        }

        // :Mp# — Spiral guide
        if (cmd[1] == 'p' && param[0] == '\0') {
            CommandError e = validateGuide();
            if (e == CE_NONE) {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->guideState = GuideState::SPIRAL;
            }
            *error        = e;
            *numericReply = false;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // Q - Halt commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'Q') {

        // :Q# — Halt all: abort goto then stop guiding
        if (cmd[1] == '\0') {
            m_msm->abortGoto();
            stopAll();
            *numericReply = false;
            return true;
        }

        // :Qe# :Qw# — Halt E/W (axis1)
        if ((cmd[1] == 'e' || cmd[1] == 'w') && param[0] == '\0') {
            stopAxis1();
            *numericReply = false;
            return true;
        }

        // :Qn# :Qs# — Halt N/S (axis2)
        if ((cmd[1] == 'n' || cmd[1] == 's') && param[0] == '\0') {
            stopAxis2();
            *numericReply = false;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // R - Guide rate commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'R') {

        // :RA[n.n]# — Set axis1 custom guide rate in deg/s
        if (cmd[1] == 'A') {
            char* end;
            double f = std::strtod(param, &end);
            if (end == param) { *error = CE_PARAM_FORM; return true; }
            if (f < 0.0) f = 0.0;
            {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->guideRateSelect          = 10; // GUIDE_RATE_CUSTOM
                m_state->customRateAxis1DegPerSec = f;
            }
            *numericReply = false;
            return true;
        }

        // :RE[n.n]# — Set axis2 custom guide rate in deg/s
        if (cmd[1] == 'E') {
            char* end;
            double f = std::strtod(param, &end);
            if (end == param) { *error = CE_PARAM_FORM; return true; }
            if (f < 0.0) f = 0.0;
            {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->guideRateSelect          = 10; // GUIDE_RATE_CUSTOM
                m_state->customRateAxis2DegPerSec = f;
            }
            *numericReply = false;
            return true;
        }

        // :RG# :RC# :RM# :RF# :RS# or :Rn# (n=0..9)
        if (param[0] == '\0') {
            int r = namedRateToIndex(cmd[1]);
            if (r < 0) return false;
            if (!m_cfg->hasGoto && r > 6) r = 6;
            {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->guideRateSelect = r;
                if (r <= 2) m_state->pulseRateSelect = r;
            }
            *numericReply = false;
            return true;
        }

        return false;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

CommandError GuideHandler::validateGuide() {
    MountState ms;
    ParkState  ps;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        ms = m_state->mountState;
        ps = m_state->parkState;
    }
    if (ps == PS_PARKED)                   return CE_SLEW_ERR_IN_PARK;
    if (ms == MountState::STANDBY)         return CE_SLEW_ERR_IN_STANDBY;
    if (ms == MountState::SLEWING_GOTO) {
        m_msm->abortGoto();
        return CE_MOUNT_IN_MOTION;  // Phase 10: renamed from CE_SLEW_IN_MOTION
    }
    return CE_NONE;
}

CommandError GuideHandler::startGuide(char direction, int rateSelect, int durationMs, bool isPulse) {
    CommandError e = validateGuide();
    if (e != CE_NONE) return e;

    // direction -> axis (1=RA/E-W, 2=Dec/N-S) and sign.
    // Verified against firmware Guide.command.cpp:
    //   :Mw# West -> startAxis1(GA_FORWARD)  -> Axis1 increasing (+)
    //   :Me# East -> startAxis1(GA_REVERSE)  -> Axis1 decreasing (-)
    //   :Mn# North-> startAxis2(GA_FORWARD)  -> Axis2 increasing (+)
    //   :Ms# South-> startAxis2(GA_REVERSE)  -> Axis2 decreasing (-)
    int    axis = 0;
    double sign = 0.0;
    switch (direction) {
        case 'w': axis = 1; sign = +1.0; break;
        case 'e': axis = 1; sign = -1.0; break;
        case 'n': axis = 2; sign = +1.0; break;
        case 's': axis = 2; sign = -1.0; break;
        default:  return CE_CMD_UNKNOWN;
    }

    std::lock_guard<std::mutex> lk(m_state->mutex);

    // Phase 11: firmware's Guide::validate() (Guide.cpp line 327-328)
    // auto-enables axes unconditionally if not already enabled.
    // Any guide move also means the mount is no longer at its precise home
    // position, so clear isAtHome so a subsequent goto correctly re-checks
    // axesEnabled rather than relying on a stale home-position flag.
    m_state->axesEnabled = true;
    m_state->isAtHome    = false;

    double rateDegPerSec = resolveRateDegPerSec(
        rateSelect, axis,
        m_state->customRateAxis1DegPerSec,
        m_state->customRateAxis2DegPerSec);
    double signedRate = sign * rateDegPerSec;

    GuideDirection dir = (sign > 0.0) ? GuideDirection::PLUS : GuideDirection::MINUS;

    if (isPulse) {
        m_state->pulseGuide = GuideState::PULSE;
        m_state->guideState = GuideState::PULSE;

        long ticks = std::lround(static_cast<double>(durationMs) / 100.0);
        if (ticks < 0) ticks = 0;

        if (axis == 1) {
            m_state->pulseDirectionAxis1     = dir;
            m_state->pulseRateDegPerSecAxis1 = std::fabs(signedRate);
            m_state->pulseTicksRemainingAxis1 = ticks;
        } else {
            m_state->pulseDirectionAxis2     = dir;
            m_state->pulseRateDegPerSecAxis2 = std::fabs(signedRate);
            m_state->pulseTicksRemainingAxis2 = ticks;
        }
    } else {
        m_state->guideState = GuideState::ACTIVE;

        if (axis == 1) {
            m_state->jogDirectionAxis1     = dir;
            m_state->jogRateDegPerSecAxis1 = std::fabs(signedRate);
        } else {
            m_state->jogDirectionAxis2     = dir;
            m_state->jogRateDegPerSecAxis2 = std::fabs(signedRate);
        }
    }

    return CE_NONE;
}

void GuideHandler::stopAxis1() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->jogDirectionAxis1      = GuideDirection::NONE;
    m_state->jogRateDegPerSecAxis1  = 0.0;
    m_state->pulseDirectionAxis1    = GuideDirection::NONE;
    m_state->pulseRateDegPerSecAxis1  = 0.0;
    m_state->pulseTicksRemainingAxis1 = 0;

    // guideState/pulseGuide are shared status flags covering both axes.
    // Only clear them once neither axis has an active jog/pulse.
    bool axis2Active = (m_state->jogDirectionAxis2   != GuideDirection::NONE) ||
                        (m_state->pulseDirectionAxis2 != GuideDirection::NONE);
    if (!axis2Active) {
        m_state->guideState = GuideState::NONE;
        m_state->pulseGuide = GuideState::NONE;
    }
}

void GuideHandler::stopAxis2() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->jogDirectionAxis2      = GuideDirection::NONE;
    m_state->jogRateDegPerSecAxis2  = 0.0;
    m_state->pulseDirectionAxis2    = GuideDirection::NONE;
    m_state->pulseRateDegPerSecAxis2  = 0.0;
    m_state->pulseTicksRemainingAxis2 = 0;

    bool axis1Active = (m_state->jogDirectionAxis1   != GuideDirection::NONE) ||
                        (m_state->pulseDirectionAxis1 != GuideDirection::NONE);
    if (!axis1Active) {
        m_state->guideState = GuideState::NONE;
        m_state->pulseGuide = GuideState::NONE;
    }
}

void GuideHandler::stopAll() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->jogDirectionAxis1      = GuideDirection::NONE;
    m_state->jogRateDegPerSecAxis1  = 0.0;
    m_state->jogDirectionAxis2      = GuideDirection::NONE;
    m_state->jogRateDegPerSecAxis2  = 0.0;
    m_state->pulseDirectionAxis1    = GuideDirection::NONE;
    m_state->pulseRateDegPerSecAxis1  = 0.0;
    m_state->pulseTicksRemainingAxis1 = 0;
    m_state->pulseDirectionAxis2    = GuideDirection::NONE;
    m_state->pulseRateDegPerSecAxis2  = 0.0;
    m_state->pulseTicksRemainingAxis2 = 0;
    m_state->guideState = GuideState::NONE;
    m_state->pulseGuide = GuideState::NONE;
}

int GuideHandler::namedRateToIndex(char c) {
    switch (c) {
        case 'G': return 2;
        case 'C': return 5;
        case 'M': return 6;
        case 'F': return 7;
        case 'S': return 8;
        default:
            if (c >= '0' && c <= '9') return c - '0';
            return -1;
    }
}

float GuideHandler::rateIndexToSidereal(int r) {
    switch (r) {
        case 0: return 0.25f;
        case 1: return 0.50f;
        case 2: return 1.00f;
        case 3: return 2.00f;
        case 4: return 4.00f;
        case 5: return 8.00f;
        case 6: return 20.0f;
        case 7: return 48.0f;
        case 8: return 96.0f;
        case 9: return 192.0f;
        default: return 1.00f;
    }
}

double GuideHandler::resolveRateDegPerSec(int rateSelect, int axis,
                                           double customAxis1DegPerSec,
                                           double customAxis2DegPerSec) {
    if (rateSelect == 10) { // GUIDE_RATE_CUSTOM
        return (axis == 1) ? customAxis1DegPerSec : customAxis2DegPerSec;
    }
    double siderealMultiple = static_cast<double>(rateIndexToSidereal(rateSelect));
    return siderealMultiple * sidereal::RATE_DEG_PER_SEC;
}
