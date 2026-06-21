// RotatorHandler.cpp — Rotator and derotator command handler.
//
// Protocol source: Rotator.command.cpp

#include "handlers/RotatorHandler.h"
#include "lib/CoordFormat.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

// ---------------------------------------------------------------------------
// Parallactic angle computation — named constants so the formula
// components are easy to identify and replace if needed.
// ---------------------------------------------------------------------------

static constexpr double PA_DEG_TO_RAD = 3.14159265358979323846 / 180.0;
static constexpr double PA_RAD_TO_DEG = 180.0 / 3.14159265358979323846;

// ---------------------------------------------------------------------------
// handle()
// ---------------------------------------------------------------------------

bool RotatorHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    // Guard: no rotator in this config
    if (!m_cfg->hasRotator) return false;

    // -----------------------------------------------------------------------
    // :GX98# — rotator/derotator type identification
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G' && cmd[1] == 'X' &&
        param[0] == '9' && param[1] == '8' && param[2] == '\0') {
        reply[0] = m_cfg->hasDerotator ? 'D' : 'R';
        reply[1] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :hP# / :hR# — rotator park/unpark (only when mount is absent)
    // -----------------------------------------------------------------------
    if (cmd[0] == 'h' && !m_cfg->hasMount) {
        if (cmd[1] == 'P' && param[0] == '\0') {
            // Park: move to angle 0 and set isParked
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            r.targetAngle = 0.0;
            r.isMoving    = (r.angle != 0.0);
            r.isParked    = true;
            *error = CE_1;
            return true;
        }
        if (cmd[1] == 'R' && param[0] == '\0') {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->rotator.isParked = false;
            *error = CE_1;
            return true;
        }
    }

    // All remaining commands start with 'r'
    if (cmd[0] != 'r') return false;

    char subCmd = cmd[1];

    // -----------------------------------------------------------------------
    // :rT# — status ("I" idle, "B" busy/moving)
    // -----------------------------------------------------------------------
    if (subCmd == 'T' && param[0] == '\0') {
        bool moving;
        { std::lock_guard<std::mutex> lk(m_state->mutex); moving = m_state->rotator.isMoving; }
        reply[0] = moving ? 'B' : 'I';
        reply[1] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rA# — get angle as signed decimal degrees ("+DDD.DD")
    // -----------------------------------------------------------------------
    if (subCmd == 'A' && param[0] == '\0') {
        double angle;
        { std::lock_guard<std::mutex> lk(m_state->mutex); angle = m_state->rotator.angle; }
        std::snprintf(reply, 256, "%+.2f", angle);
        return true;
    }

    // -----------------------------------------------------------------------
    // :rG# — get angle in "sDDD*MM" format (PM_LOW, fullRange — verified
    // against firmware's Rotator.command.cpp:
    // convert.doubleToDms(reply, angle, true, true, PM_LOW))
    // Example: angle=123.75 -> "+123*45"
    // Phase 9: this implementation already produced firmware-correct
    // output (it had its own correct carry handling); migrated to the
    // shared coordformat:: utility purely for single-source-of-truth
    // consistency with the other coordinate-formatting call sites.
    // -----------------------------------------------------------------------
    if (subCmd == 'G' && param[0] == '\0') {
        double angle;
        { std::lock_guard<std::mutex> lk(m_state->mutex); angle = m_state->rotator.angle; }
        coordformat::doubleToDms(reply, angle, true, true, CoordPrecision::Low);
        return true;
    }

    // -----------------------------------------------------------------------
    // :rS[deg]# — absolute goto decimal degrees -> '0'/'1'
    // -----------------------------------------------------------------------
    if (subCmd == 'S') {
        double targetDeg = std::atof(param);
        std::lock_guard<std::mutex> lk(m_state->mutex);
        RotatorState& r = m_state->rotator;
        if (targetDeg < r.limitMin || targetDeg > r.limitMax) {
            *numericReply = true;
            reply[0] = '0';
            return true;
        }
        r.targetAngle = targetDeg;
        r.isMoving    = (std::fabs(r.angle - targetDeg) > 1e-4);
        *numericReply = true;
        reply[0] = '1';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rr[deg]# — relative goto signed degrees -> nothing
    // -----------------------------------------------------------------------
    if (subCmd == 'r') {
        double deltaDeg = std::atof(param);
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            double newTarget = r.angle + deltaDeg;
            // Clamp to limits
            if (newTarget < r.limitMin) newTarget = r.limitMin;
            if (newTarget > r.limitMax) newTarget = r.limitMax;
            r.targetAngle = newTarget;
            r.isMoving    = (std::fabs(r.angle - newTarget) > 1e-4);
        }
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rQ# — stop
    // -----------------------------------------------------------------------
    if (subCmd == 'Q' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        RotatorState& r = m_state->rotator;
        r.targetAngle = r.angle;
        r.isMoving    = false;
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :r[1-9]# — set goto rate
    // -----------------------------------------------------------------------
    if (subCmd >= '1' && subCmd <= '9' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->rotator.gotoRate = subCmd - '0';
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rF# — reset position to 0 degrees
    // -----------------------------------------------------------------------
    if (subCmd == 'F' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        RotatorState& r = m_state->rotator;
        r.angle       = 0.0;
        r.targetAngle = 0.0;
        r.isMoving    = false;
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rP# — goto parallactic angle
    // -----------------------------------------------------------------------
    if (subCmd == 'P' && param[0] == '\0') {
        double pa = computeParallacticAngle();
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            // Clamp to rotator limits
            if (pa < r.limitMin) pa = r.limitMin;
            if (pa > r.limitMax) pa = r.limitMax;
            r.targetAngle = pa;
            r.isMoving    = (std::fabs(r.angle - pa) > 1e-4);
        }
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :r+# — enable derotator (ALTAZM mount only)
    // :r-# — disable derotator
    // -----------------------------------------------------------------------
    if (subCmd == '+' && param[0] == '\0') {
        // Only meaningful for ALTAZM / ALTAZM_UNL; silently ignored otherwise.
        // Mount type constants: ALTAZM=3, ALTAZM_UNL=9 (from plan).
        if (m_cfg->hasDerotator) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->rotator.derotEnabled = true;
        }
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }
    if (subCmd == '-' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->rotator.derotEnabled = false;
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rR# — derotator reverse toggle
    // -----------------------------------------------------------------------
    if (subCmd == 'R' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->rotator.derotReverse = !m_state->rotator.derotReverse;
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rZ# — sync to 0
    // -----------------------------------------------------------------------
    if (subCmd == 'Z' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        RotatorState& r = m_state->rotator;
        r.angle       = 0.0;
        r.targetAngle = 0.0;
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rB# / :rB[n]# — backlash get/set (arcseconds)
    // :rb# — backlash get (steps)
    // -----------------------------------------------------------------------
    if (subCmd == 'B') {
        if (param[0] == '\0') {
            long bl;
            { std::lock_guard<std::mutex> lk(m_state->mutex); bl = m_state->rotator.backlash; }
            std::snprintf(reply, 256, "%ld", bl);
            return true;
        }
        long bl = std::atol(param);
        if (bl < 0) {
            *numericReply = true;
            reply[0] = '0';
            return true;
        }
        { std::lock_guard<std::mutex> lk(m_state->mutex); m_state->rotator.backlash = bl; }
        *numericReply = true;
        reply[0] = '1';
        return true;
    }
    if (subCmd == 'b' && param[0] == '\0') {
        long bl;
        { std::lock_guard<std::mutex> lk(m_state->mutex); bl = m_state->rotator.backlash; }
        std::snprintf(reply, 256, "%ld", bl);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Parallactic angle computation
// ---------------------------------------------------------------------------
// Named variables for each physical quantity so they are easy to find
// and replace if more precise values are later required.

double RotatorHandler::computeParallacticAngle() const {
    // Retrieve current state without holding the mutex long
    double hourAngleHours;
    double declinationDeg;
    double latitudeDeg;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        hourAngleHours = m_state->ha;
        declinationDeg = m_state->dec;
        latitudeDeg    = m_state->sites[m_state->currentSite].latitude;
    }

    // Convert to radians
    double hourAngleRad    = hourAngleHours * 15.0 * PA_DEG_TO_RAD;
    double declinationRad  = declinationDeg       * PA_DEG_TO_RAD;
    double latitudeRad     = latitudeDeg           * PA_DEG_TO_RAD;

    // Standard parallactic angle formula:
    //   PA = atan2(sin(H),  tan(lat)*cos(Dec) - sin(Dec)*cos(H))
    double sinH   = std::sin(hourAngleRad);
    double cosH   = std::cos(hourAngleRad);
    double sinDec = std::sin(declinationRad);
    double cosDec = std::cos(declinationRad);
    double tanLat = std::tan(latitudeRad);

    double paRad = std::atan2(sinH, tanLat * cosDec - sinDec * cosH);
    double paDeg = paRad * PA_RAD_TO_DEG;

    return paDeg;
}
