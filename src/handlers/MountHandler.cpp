// MountHandler.cpp — Mount coordinate, tracking, goto, and alignment handler
// Source references: Mount.command.cpp, Goto.command.cpp

#include "MountHandler.h"
#include "lib/CoordFormat.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static constexpr double PI = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Format helpers
// ---------------------------------------------------------------------------
//
// Phase 9: these now delegate to the shared coordformat:: utility (see
// src/lib/CoordFormat.h), which replicates firmware's Convert::doubleToHms/
// doubleToDms exactly, including the rounding-carry behaviour the
// hand-rolled versions here previously lacked (they could produce
// impossible output like "12:59:60.0000" — see Phase 9 commit/decision log
// for the verified failing cases).

// RA: HH:MM:SS (PM_HIGH, default) or HH:MM:SS.SSSS (PM_HIGHEST)
void MountHandler::formatRA(char* buf, double hours, bool highPrec) const {
    coordformat::doubleToHms(buf, hours, false,
        highPrec ? CoordPrecision::Highest : CoordPrecision::High);
}

// Dec: sDD*MM:SS (PM_HIGH) or sDD*MM:SS.SSS (PM_HIGHEST)
void MountHandler::formatDec(char* buf, double deg, bool highPrec) const {
    coordformat::doubleToDms(buf, deg, false, true,
        highPrec ? CoordPrecision::Highest : CoordPrecision::High);
}

// Alt: sDD*MM:SS (PM_HIGH, default) or sDD*MM:SS.SSS (PM_HIGHEST via :GAH#)
// Verified against firmware Mount.command.cpp :GA# — NOT a 2-field/PM_LOW
// form as previously implemented; firmware defaults to PM_HIGH (3-field)
// here, same as RA/Dec, escalating to PM_HIGHEST only with an 'H' suffix.
void MountHandler::formatAlt(char* buf, double deg, bool highPrec) const {
    coordformat::doubleToDms(buf, deg, false, true,
        highPrec ? CoordPrecision::Highest : CoordPrecision::High);
}

// Az: DDD*MM:SS (PM_HIGH, default) or DDD*MM:SS.SSS (PM_HIGHEST via :GZH#)
// Verified against firmware Mount.command.cpp :GZ# — same correction as
// formatAlt above; fullRange=true (3-digit degree field), unsigned.
void MountHandler::formatAz(char* buf, double deg, bool highPrec) const {
    coordformat::doubleToDms(buf, deg, true, false,
        highPrec ? CoordPrecision::Highest : CoordPrecision::High);
}

// Parse RA: HH:MM.T or HH:MM:SS or HH:MM:SS.SSSS
bool MountHandler::parseRA(const char* p, double* hours) const {
    int h, mi;
    double s = 0.0;
    if (std::sscanf(p, "%d:%d:%lf", &h, &mi, &s) < 2) return false;
    *hours = h + mi / 60.0 + s / 3600.0;
    return (*hours >= 0.0 && *hours < 24.0);
}

// Parse Dec: sDD*MM:SS or sDD*MM:SS.SSS
bool MountHandler::parseDec(const char* p, double* deg) const {
    bool neg = false;
    const char* s = p;
    if (*s == '-') { neg = true; ++s; }
    else if (*s == '+') { ++s; }
    char buf[64];
    std::strncpy(buf, s, 63); buf[63] = 0;
    for (char* c = buf; *c; ++c) if (*c == '*') *c = ':';
    int d, m;
    double sec = 0.0;
    if (std::sscanf(buf, "%d:%d:%lf", &d, &m, &sec) < 2) return false;
    *deg = d + m / 60.0 + sec / 3600.0;
    if (neg) *deg = -(*deg);
    return (*deg >= -90.0 && *deg <= 90.0);
}

// Map CommandError to :MS# single-char reply per Goto.command.cpp.
// Explicit table: the firmware uses commandError values that have gaps,
// so arithmetic offset is unreliable. Use a direct switch instead.
char MountHandler::gotoErrorChar(CommandError e) const {
    switch (e) {
    case CE_NONE:                       return '0';
    case CE_SLEW_ERR_BELOW_HORIZON:     return '1';
    case CE_SLEW_ERR_ABOVE_OVERHEAD:    return '2';
    case CE_SLEW_ERR_IN_STANDBY:        return '3';
    case CE_SLEW_ERR_IN_PARK:           return '4';
    case CE_SLEW_IN_SLEW:               return '5';  // no target set, or already slewing
    case CE_SLEW_ERR_OUTSIDE_LIMITS:    return '6';
    case CE_SLEW_ERR_HARDWARE_FAULT:    return '7';
    case CE_SLEW_ERR_ALT_MIN:           return '8';
    case CE_SLEW_ERR_ALT_MAX:           return '9';
    default:                            return '9';
    }
}

// ---------------------------------------------------------------------------
// handle()
// ---------------------------------------------------------------------------

bool MountHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    if (!m_cfg->hasMount) return false;

    // -----------------------------------------------------------------------
    // G — Get coordinate / status commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G') {

        // :GR# / :GRH# — current RA
        if (cmd[1] == 'R' && (param[0] == 0 || param[1] == 0)) {
            if (param[0] != 0 && param[0] != 'H') { *error = CE_PARAM_FORM; return true; }
            bool hiPrec = (param[0] == 'H');
            std::lock_guard<std::mutex> lk(m_state->mutex);
            formatRA(reply, m_state->ra, hiPrec);
            *numericReply = false;
            return true;
        }

        // :GD# / :GDH# — current Dec
        if (cmd[1] == 'D' && (param[0] == 0 || param[1] == 0)) {
            if (param[0] != 0 && param[0] != 'H') { *error = CE_PARAM_FORM; return true; }
            bool hiPrec = (param[0] == 'H');
            std::lock_guard<std::mutex> lk(m_state->mutex);
            formatDec(reply, m_state->dec, hiPrec);
            *numericReply = false;
            return true;
        }

        // :GA# / :GAH# — current Alt
        // Previously only matched bare ":GA#" with no precision suffix
        // support at all, and always rendered the 2-field PM_LOW-style
        // form; firmware's real default is 3-field PM_HIGH (sDD*MM:SS),
        // escalating to PM_HIGHEST (sDD*MM:SS.SSS) with an 'H' suffix —
        // verified against Mount.command.cpp.
        if (cmd[1] == 'A' && (param[0] == 0 || param[1] == 0)) {
            if (param[0] != 0 && param[0] != 'H') { *error = CE_PARAM_FORM; return true; }
            bool hiPrec = (param[0] == 'H');
            std::lock_guard<std::mutex> lk(m_state->mutex);
            formatAlt(reply, m_state->alt, hiPrec);
            *numericReply = false;
            return true;
        }

        // :GZ# / :GZH# — current Az
        // Same correction as :GA# above.
        if (cmd[1] == 'Z' && (param[0] == 0 || param[1] == 0)) {
            if (param[0] != 0 && param[0] != 'H') { *error = CE_PARAM_FORM; return true; }
            bool hiPrec = (param[0] == 'H');
            std::lock_guard<std::mutex> lk(m_state->mutex);
            formatAz(reply, m_state->az, hiPrec);
            *numericReply = false;
            return true;
        }

        // :Gr# / :GrH# — target RA
        if (cmd[1] == 'r' && (param[0] == 0 || param[1] == 0)) {
            if (param[0] != 0 && param[0] != 'H' && param[0] != 'a')
                { *error = CE_PARAM_FORM; return true; }
            bool hiPrec = (param[0] == 'H' || param[0] == 'a');
            std::lock_guard<std::mutex> lk(m_state->mutex);
            formatRA(reply, m_state->targetRA, hiPrec);
            *numericReply = false;
            return true;
        }

        // :Gd# / :GdH# — target Dec
        if (cmd[1] == 'd' && (param[0] == 0 || param[1] == 0)) {
            if (param[0] != 0 && param[0] != 'H' && param[0] != 'e')
                { *error = CE_PARAM_FORM; return true; }
            bool hiPrec = (param[0] == 'H' || param[0] == 'e');
            std::lock_guard<std::mutex> lk(m_state->mutex);
            formatDec(reply, m_state->targetDec, hiPrec);
            *numericReply = false;
            return true;
        }

        // :GX9n# — goto misc get
        if (cmd[1] == 'X' && param[0] == '9' && param[2] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            *numericReply = false;
            switch (param[1]) {
            case '2': std::sprintf(reply, "%0.3f", 1000000.0f / 1000.0f); return true; // usPerStep stub
            case '3': std::sprintf(reply, "%0.3f", 1000.0f); return true;              // base us/step stub
            case '4': {
                // pier side: 0=None, 1=East, 2=West (+ suffix N if no meridian flips)
                bool mflips = m_cfg->isEquatorial();
                std::sprintf(reply, "%d%s",
                    static_cast<int>(m_state->pierSide),
                    !mflips ? " N" : "");
                return true;
            }
            case '5': std::sprintf(reply, "%d", m_state->autoFlipEnabled ? 1 : 0); return true;
            case '6': {
                // preferred pier side: E/W/B/A
                const char* sides = "EWBA";
                reply[0] = m_cfg->isEquatorial() ?
                    sides[static_cast<int>(m_state->preferredPierSide)] : 'E';
                reply[1] = 0;
                return true;
            }
            case '7': std::sprintf(reply, "%0.1f", m_state->slewRateDegPerSec); return true;
            case '9': std::sprintf(reply, "%0.3f", 500.0f); return true; // fastest step stub
            default: return false;
            }
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // S — Set target coordinates and misc
    // -----------------------------------------------------------------------
    if (cmd[0] == 'S') {

        // :Sr[HH:MM:SS]# — set target RA
        if (cmd[1] == 'r') {
            *numericReply = true;
            double hours;
            if (parseRA(param, &hours)) {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->targetRA    = hours;
                m_state->targetRASet = true;
            } else {
                *error = CE_PARAM_RANGE;
            }
            return true;
        }

        // :Sd[sDD*MM:SS]# — set target Dec
        if (cmd[1] == 'd') {
            *numericReply = true;
            double deg;
            if (parseDec(param, &deg)) {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->targetDec    = deg;
                m_state->targetDecSet = true;
            } else {
                *error = CE_PARAM_RANGE;
            }
            return true;
        }

        // :SX9n,v# — goto misc set
        if (cmd[1] == 'X' && param[0] == '9' && param[2] == ',') {
            *numericReply = true;
            std::lock_guard<std::mutex> lk(m_state->mutex);
            switch (param[1]) {
            case '5': // autoMeridianFlip
                if (param[3] == '0' || param[3] == '1')
                    m_state->autoFlipEnabled = (param[3] == '1');
                else *error = CE_PARAM_RANGE;
                return true;
            case '6': // preferred pier side
                switch (param[3]) {
                case 'E': m_state->preferredPierSide = PreferredPierSide::EAST; break;
                case 'W': m_state->preferredPierSide = PreferredPierSide::WEST; break;
                case 'B': m_state->preferredPierSide = PreferredPierSide::BEST; break;
                default:  *error = CE_PARAM_RANGE; break;
                }
                return true;
            case '8': // pause at home
                if (param[3] == '0' || param[3] == '1')
                    m_state->pauseAtHomeEnabled = (param[3] == '1');
                else *error = CE_PARAM_RANGE;
                return true;
            case '9': // continue if paused
                if (param[3] == '1') m_state->homePaused = false;
                else *error = CE_PARAM_RANGE;
                return true;
            default: return false;
            }
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // C — Sync commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'C') {
        // :CM# — sync to target, return "N/A"# on success
        if (cmd[1] == 'M' && param[0] == 0) {
            CommandError e = m_sm->syncToTarget();
            if (e == CE_NONE) {
                std::strcpy(reply, "N/A");
            } else {
                // "En#" where n is offset from CE_SLEW_ERR_BELOW_HORIZON
                reply[0] = 'E';
                reply[1] = gotoErrorChar(e);
                reply[2] = 0;
            }
            *numericReply = false;
            return true;
        }

        // :CS# — sync silently (no reply)
        if (cmd[1] == 'S' && param[0] == 0) {
            m_sm->syncToTarget();
            *numericReply = false;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // T — Tracking commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'T') {
        *numericReply = false;

        // :T+# / :Te# — enable sidereal tracking
        if ((cmd[1] == '+' || cmd[1] == 'e') && param[0] == 0) {
            m_sm->startTracking();
            {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->trackingRateHz = 60.136f;
                m_state->rateComp       = RateComp::NONE;
            }
            return true;
        }

        // :T-# — stop tracking
        if (cmd[1] == '-' && param[0] == 0) {
            m_sm->stopTracking();
            return true;
        }

        // :Ts# — solar rate 60.000 Hz
        if (cmd[1] == 's' && param[0] == 0) {
            m_sm->setTrackingRate(60.000f);
            return true;
        }

        // :To# — sidereal rate 60.136 Hz
        if (cmd[1] == 'o' && param[0] == 0) {
            m_sm->setTrackingRate(60.136f);
            return true;
        }

        // :TL# — lunar rate 57.900 Hz
        if (cmd[1] == 'L' && param[0] == 0) {
            m_sm->setTrackingRate(57.900f);
            return true;
        }

        // :TM# / :TK# — king rate 60.136 Hz (same as sidereal for sim)
        if ((cmd[1] == 'M' || cmd[1] == 'K') && param[0] == 0) {
            m_sm->setTrackingRate(60.136f);
            return true;
        }

        // :Tn# — toggle refraction correction
        if (cmd[1] == 'n' && param[0] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->rateComp = (m_state->rateComp == RateComp::NONE)
                ? RateComp::REFRACTION : RateComp::NONE;
            return true;
        }

        // :T1# / :T2# — single/dual axis refraction
        if (cmd[1] == '1' && param[0] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->rateComp = RateComp::REFRACTION;
            return true;
        }
        if (cmd[1] == '2' && param[0] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->rateComp = RateComp::REFRACTION_DUAL;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // M — Goto commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'M') {

        // :MS# — goto target
        if (cmd[1] == 'S' && param[0] == 0) {
            CommandError e = m_sm->beginGoto();
            reply[0] = gotoErrorChar(e);
            reply[1] = 0;
            *numericReply  = false;
            *suppressFrame = true;
            *error = e;
            return true;
        }

        // :D# — slew distance indicator
        if (cmd[1] == 0) {
            // cmd[1] = '\0' means cmd was just "D" (single char command :D#)
            // But framer gives us cmd[0]='D', cmd[1]='\0'
            // Fall through — handled by 'D' block below
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // D — Slew distance indicator (:D#)
    // cmd[0]='D', cmd[1]='\0' for single-char commands (DEC-004)
    // -----------------------------------------------------------------------
    if (cmd[0] == 'D' && cmd[1] == 0) {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        if (m_state->gotoState == GotoState::GOTO) {
            reply[0] = static_cast<char>(127);  // 0x7F
            reply[1] = 0;
        } else {
            reply[0] = '#';
            reply[1] = 0;
            *suppressFrame = true;
        }
        *numericReply = false;
        return true;
    }

    // -----------------------------------------------------------------------
    // R — Slew rate commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'R') {
        *numericReply = false;

        // :RS# — max slew rate
        if (cmd[1] == 'S' && param[0] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->slewRateDegPerSec = m_cfg->slewRateBaseDesired;
            return true;
        }

        // :R[0-9]# — slew rate multiplier
        if (cmd[1] >= '0' && cmd[1] <= '9' && param[0] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            int n = cmd[1] - '0';
            // Scale from 0.1x to 1.0x of base desired rate
            m_state->slewRateDegPerSec = m_cfg->slewRateBaseDesired * (n + 1) / 10.0;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // A — Alignment commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'A') {

        // :A[1-9]# — start n-star alignment
        if (cmd[1] >= '1' && cmd[1] <= '9' && param[0] == 0) {
            *numericReply = true;
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->alignExpected  = cmd[1] - '0';
            m_state->alignDoneCount = 0;
            m_state->alignDone      = false;
            m_state->isTracking     = true;
            m_state->mountState     = MountState::TRACKING;
            return true;
        }

        // :A+# — accept alignment star
        if (cmd[1] == '+' && param[0] == 0) {
            *numericReply = true;
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->alignDoneCount++;
            if (m_state->alignDoneCount >= m_state->alignExpected) {
                m_state->alignDone = true;
            }
            return true;
        }

        // :AW# — write alignment (noop in sim, return success)
        if (cmd[1] == 'W' && param[0] == 0) {
            *numericReply = true;
            return true;
        }

        // :A?# — alignment status: mno (max stars, current star, last star)
        if (cmd[1] == '?' && param[0] == 0) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            reply[0] = static_cast<char>('0' + 9);  // max stars = 9
            reply[1] = static_cast<char>('0' + std::min(m_state->alignDoneCount + 1, 9));
            reply[2] = static_cast<char>('0' + m_state->alignExpected);
            reply[3] = 0;
            *numericReply = false;
            return true;
        }

        return false;
    }

    return false;
}
