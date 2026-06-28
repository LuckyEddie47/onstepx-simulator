// RotatorHandler.cpp — Rotator and derotator command handler.
//
// Protocol source: Rotator.command.cpp (Phase 15 complete).

#include "handlers/RotatorHandler.h"
#include "lib/CoordFormat.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

static constexpr double PA_DEG_TO_RAD = 3.14159265358979323846 / 180.0;
static constexpr double PA_RAD_TO_DEG = 180.0 / 3.14159265358979323846;

bool RotatorHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    if (!m_cfg->hasRotator) return false;

    // Phase 12: default to non-numeric reply; numeric paths set *numericReply=true.
    *numericReply = false;

    // -----------------------------------------------------------------------
    // :GX98# — rotator/derotator type identification
    // Returns: "D#" for rotate/derotate (ALTAZM), "R#" for rotate only
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
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            r.targetAngle       = 0.0;
            r.isMoving          = (r.angle != 0.0);
            r.continuousMoveDir = 0;
            r.isParked          = true;
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

    if (cmd[0] != 'r') return false;

    char sub = cmd[1];

    // -----------------------------------------------------------------------
    // :rA# — rotator Active? (presence check)
    //         Returns: 1 (always, since we only reach here when hasRotator)
    // Firmware: falls through default numericReply=true path → "1"
    // -----------------------------------------------------------------------
    if (sub == 'A' && param[0] == '\0') {
        *numericReply = true;
        // reply[0] defaults to '\0'; framer sends '1' for CE_NONE with numericReply
        return true;
    }

    // -----------------------------------------------------------------------
    // :rT# — rotator sTatus
    //         Returns: "M" if moving, "S[D][R]n" if stopped.
    //         n = getGotoRate() → 1..5
    // Firmware: Status.command.cpp pattern exactly.
    // -----------------------------------------------------------------------
    if (sub == 'T') {
        bool moving, derotEnabled, derotReverse;
        int  gotoRate;
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            moving      = r.isMoving || (r.continuousMoveDir != 0);
            derotEnabled = r.derotEnabled;
            derotReverse = r.derotReverse;
            gotoRate    = r.gotoRate;
        }
        int pos = 0;
        if (moving) {
            reply[pos++] = 'M';
        } else {
            reply[pos++] = 'S';
            if (derotEnabled) reply[pos++] = 'D';
            if (derotReverse) reply[pos++] = 'R';
        }
        // getGotoRate() maps gotoRate setting → 1..5 bucket
        // Firmware: compares against AXIS3_SLEW_RATE_BASE_DESIRED multiples.
        // Sim: use gotoRate 1-4 as move rates → bucket 1-4; 5-9 as goto rates.
        int rateChar = (gotoRate >= 1 && gotoRate <= 9) ? gotoRate : 3;
        // Clamp to 1-5 as firmware does
        if (rateChar < 1) rateChar = 1;
        if (rateChar > 5) rateChar = 5;
        reply[pos++] = static_cast<char>('0' + rateChar);
        reply[pos]   = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rI# — get rotator mInimum position (degrees)
    //         Returns: n#
    // -----------------------------------------------------------------------
    if (sub == 'I') {
        double lim;
        { std::lock_guard<std::mutex> lk(m_state->mutex); lim = m_state->rotator.limitMin; }
        std::snprintf(reply, 256, "%ld", static_cast<long>(std::round(lim)));
        return true;
    }

    // -----------------------------------------------------------------------
    // :rM# — get rotator Max position (degrees)
    //         Returns: n#
    // -----------------------------------------------------------------------
    if (sub == 'M') {
        double lim;
        { std::lock_guard<std::mutex> lk(m_state->mutex); lim = m_state->rotator.limitMax; }
        std::snprintf(reply, 256, "%ld", static_cast<long>(std::round(lim)));
        return true;
    }

    // -----------------------------------------------------------------------
    // :rD# — get degrees per step  (1.0/stepsPerDegree)
    //         Returns: n.n# (7.5f format)
    // -----------------------------------------------------------------------
    if (sub == 'D') {
        double spd;
        { std::lock_guard<std::mutex> lk(m_state->mutex); spd = m_state->rotator.stepsPerDegree; }
        double degPerStep = (spd > 0.0) ? 1.0 / spd : 0.0;
        std::snprintf(reply, 256, "%7.5f", degPerStep);
        return true;
    }

    // -----------------------------------------------------------------------
    // :rW# — get Working slew rate (deg/s)
    //         Returns: d.d#
    // Firmware: settings.gotoRate — the actual slew rate in deg/s.
    // Sim: derive from gotoRate index via rotatorDegsPerTick * tickHz.
    // -----------------------------------------------------------------------
    if (sub == 'W') {
        int gr;
        { std::lock_guard<std::mutex> lk(m_state->mutex); gr = m_state->rotator.gotoRate; }
        // rotatorDegsPerTick(gr) * 10 ticks/s gives deg/s (SimClock ticks at 10 Hz).
        // Values: rate 1→0.01, 2→0.1, 3→1.0, 4→5.0, 5+→10.0 deg/s
        static const double RATE_TABLE[] = {0.01, 0.01, 0.1, 1.0, 5.0, 10.0, 10.0, 10.0, 10.0, 10.0};
        int idx = (gr >= 1 && gr <= 9) ? gr : 3;
        std::snprintf(reply, 256, "%0.1f", RATE_TABLE[idx]);
        return true;
    }

    // -----------------------------------------------------------------------
    // :rc# — set continuous move mode (no-op in OnStepX; all moves are
    //         continuous). Returns: Nothing
    // -----------------------------------------------------------------------
    if (sub == 'c') {
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :r># — move rotator CW (continuous until :rQ# or limit)
    //         Returns: Nothing
    // :r<# — move rotator CCW (continuous until :rQ# or limit)
    //         Returns: Nothing
    // Phase 15: firmware calls move(DIR_FORWARD/REVERSE) → axis3.autoSlew().
    // Sim: set continuousMoveDir; SimClock advances angle per tick.
    // -----------------------------------------------------------------------
    if (sub == '>' && param[0] == '\0') {
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            if (r.isParked) { *error = CE_PARKED; return true; }
            r.continuousMoveDir = +1;
            r.isMoving          = true;
        }
        *suppressFrame = true;
        return true;
    }
    if (sub == '<' && param[0] == '\0') {
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            if (r.isParked) { *error = CE_PARKED; return true; }
            r.continuousMoveDir = -1;
            r.isMoving          = true;
        }
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :rG# — get rotator current angle
    //         Returns: sDDD*MM#
    // -----------------------------------------------------------------------
    if (sub == 'G' && param[0] == '\0') {
        double angle;
        { std::lock_guard<std::mutex> lk(m_state->mutex); angle = m_state->rotator.angle; }
        coordformat::doubleToDms(reply, angle, true, true, CoordPrecision::Low);
        return true;
    }

    // -----------------------------------------------------------------------
    // :rr[sDDD*MM...]# — relative goto (signed degrees)
    //         Returns: Nothing
    // -----------------------------------------------------------------------
    if (sub == 'r') {
        double deltaDeg = std::atof(param);
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            r.continuousMoveDir = 0;
            double newTarget = r.angle + deltaDeg;
            if (newTarget < r.limitMin) newTarget = r.limitMin;
            if (newTarget > r.limitMax) newTarget = r.limitMax;
            r.targetAngle = newTarget;
            r.isMoving    = (std::fabs(r.angle - newTarget) > 1e-4);
        }
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :rS[sDDD*MM...]# — absolute goto
    //         Returns: 0 on failure, 1 on success
    // -----------------------------------------------------------------------
    if (sub == 'S') {
        *numericReply = true;
        double targetDeg = std::atof(param);
        std::lock_guard<std::mutex> lk(m_state->mutex);
        RotatorState& r = m_state->rotator;
        if (targetDeg < r.limitMin || targetDeg > r.limitMax) {
            reply[0] = '0';
            return true;
        }
        r.continuousMoveDir = 0;
        r.targetAngle = targetDeg;
        r.isMoving    = (std::fabs(r.angle - targetDeg) > 1e-4);
        reply[0] = '1';
        return true;
    }

    // -----------------------------------------------------------------------
    // :rQ# — stop (Quit) rotator movement
    //         Returns: Nothing
    // -----------------------------------------------------------------------
    if (sub == 'Q' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        RotatorState& r = m_state->rotator;
        r.continuousMoveDir = 0;
        r.targetAngle       = r.angle;
        r.isMoving          = false;
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :r[1-9]# — set move/goto rate
    //         Returns: Nothing
    // -----------------------------------------------------------------------
    if (sub >= '1' && sub <= '9' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->rotator.gotoRate = sub - '0';
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :rF# — set current position as Half-travel
    //         (redefine where we are as the midpoint — no motion)
    //         Returns: Nothing
    // Firmware: axis3.resetPosition((max+min)/2) — sets current position,
    //           does NOT move toward it.
    // -----------------------------------------------------------------------
    if (sub == 'F' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        RotatorState& r = m_state->rotator;
        double halfTravel = (r.limitMax + r.limitMin) / 2.0;
        r.angle             = halfTravel;
        r.targetAngle       = halfTravel;
        r.continuousMoveDir = 0;
        r.isMoving          = false;
        r.isParked          = false;
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :rC# — move rotator to half-travel position (goto, not reset)
    //         Returns: Nothing
    // Firmware: if hasSense → autoSlewHome(); else → gotoTarget(half-travel)
    // Sim: no home-sense model → always go to (max+min)/2
    // -----------------------------------------------------------------------
    if (sub == 'C' && param[0] == '\0') {
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            if (r.isParked) { *error = CE_PARKED; return true; }
            r.continuousMoveDir = 0;
            double halfTravel = (r.limitMax + r.limitMin) / 2.0;
            r.targetAngle = halfTravel;
            r.isMoving    = (std::fabs(r.angle - halfTravel) > 1e-4);
        }
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :rZ# — sync current position to 0 degrees
    //         Returns: Nothing
    // -----------------------------------------------------------------------
    if (sub == 'Z' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        RotatorState& r = m_state->rotator;
        r.angle             = 0.0;
        r.targetAngle       = 0.0;
        r.continuousMoveDir = 0;
        r.isParked          = false;
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :rP# — goto parallactic angle
    //         Returns: Nothing
    // -----------------------------------------------------------------------
    if (sub == 'P' && param[0] == '\0') {
        double pa = computeParallacticAngle();
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            if (pa < r.limitMin) pa = r.limitMin;
            if (pa > r.limitMax) pa = r.limitMax;
            r.continuousMoveDir = 0;
            r.targetAngle = pa;
            r.isMoving    = (std::fabs(r.angle - pa) > 1e-4);
        }
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :r+# — enable derotator (ALTAZM only)
    // :r-# — disable derotator
    //         Returns: Nothing
    // -----------------------------------------------------------------------
    if (sub == '+' && param[0] == '\0') {
        if (m_cfg->hasDerotator) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            RotatorState& r = m_state->rotator;
            if (r.isParked) { *error = CE_PARKED; return true; }
            r.derotEnabled = true;
        }
        *suppressFrame = true;
        return true;
    }
    if (sub == '-' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->rotator.derotEnabled = false;
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :rR# — toggle derotator reverse
    //         Returns: Nothing
    // -----------------------------------------------------------------------
    if (sub == 'R' && param[0] == '\0') {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->rotator.derotReverse = !m_state->rotator.derotReverse;
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :rB# / :rb# — backlash get/set
    // -----------------------------------------------------------------------
    if (sub == 'b' && param[0] == '\0') {
        long bl;
        { std::lock_guard<std::mutex> lk(m_state->mutex); bl = m_state->rotator.backlash; }
        std::snprintf(reply, 256, "%ld", bl);
        return true;
    }
    if (sub == 'b') {
        long bl = std::atol(param);
        if (bl < 0) { *numericReply = true; reply[0] = '0'; return true; }
        { std::lock_guard<std::mutex> lk(m_state->mutex); m_state->rotator.backlash = bl; }
        *numericReply = true; reply[0] = '1';
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Parallactic angle computation
// ---------------------------------------------------------------------------

double RotatorHandler::computeParallacticAngle() const {
    double hourAngleHours, declinationDeg, latitudeDeg;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        hourAngleHours = m_state->ha;
        declinationDeg = m_state->dec;
        latitudeDeg    = m_state->sites[m_state->currentSite].latitude;
    }
    double hourAngleRad   = hourAngleHours * 15.0 * PA_DEG_TO_RAD;
    double declinationRad = declinationDeg         * PA_DEG_TO_RAD;
    double latitudeRad    = latitudeDeg             * PA_DEG_TO_RAD;
    double sinH   = std::sin(hourAngleRad);
    double cosH   = std::cos(hourAngleRad);
    double sinDec = std::sin(declinationRad);
    double cosDec = std::cos(declinationRad);
    double tanLat = std::tan(latitudeRad);
    double paRad  = std::atan2(sinH, tanLat * cosDec - sinDec * cosH);
    return paRad * PA_RAD_TO_DEG;
}
