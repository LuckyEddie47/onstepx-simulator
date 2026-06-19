// SimClock.cpp — Simulated time and coordinate update engine

#include "SimClock.h"
#include "SiderealConstants.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr double PI                    = 3.14159265358979323846;
static constexpr double SIDEREAL_HZ           = sidereal::HZ;
static constexpr double SIDEREAL_RATE_DEG_PER_SEC = sidereal::RATE_DEG_PER_SEC;
static constexpr int    TICK_HZ               = 10;
static constexpr double TICK_SEC              = 1.0 / TICK_HZ;

// ---------------------------------------------------------------------------
// Phase 4 — Focuser rate table
//
// gotoRate 1..5 maps to a step count per 100ms tick.  The firmware's
// focuser speed presets are approximate; these values give visually
// sensible motion in the simulator without being instant.
//
// gotoRate:  1    2    3     4      5
// steps/tick: 1   10   50   200   1000
// ---------------------------------------------------------------------------
static constexpr long FOCUSER_STEPS_PER_TICK[6] = { 0, 1, 10, 50, 200, 1000 };
//                                                  ^--- index 0 unused (rates are 1-based)

// ---------------------------------------------------------------------------
// Phase 4 — Rotator rate table
//
// gotoRate 1..9 maps to degrees per tick (100ms).
//
// gotoRate:    1      2      3     4      5      6      7      8      9
// deg/tick:  0.01   0.05   0.1   0.5    1.0    2.0    5.0   10.0   30.0
// ---------------------------------------------------------------------------
static constexpr double ROTATOR_DEGS_PER_TICK[10] = {
    0.0,   // index 0 unused
    0.01,  // 1
    0.05,  // 2
    0.1,   // 3
    0.5,   // 4
    1.0,   // 5
    2.0,   // 6
    5.0,   // 7
    10.0,  // 8
    30.0,  // 9
};

// ---------------------------------------------------------------------------
// Thread control
// ---------------------------------------------------------------------------

void SimClock::start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_thread = std::thread([this]() { threadFunc(); });
}

void SimClock::stop() {
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
}

// ---------------------------------------------------------------------------
// Background thread
// ---------------------------------------------------------------------------

void SimClock::threadFunc() {
    while (m_running.load()) {
        auto t0 = std::chrono::steady_clock::now();
        tick();
        auto t1 = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);
        auto sleepUs = std::chrono::microseconds(1000000 / TICK_HZ) - elapsed;
        if (sleepUs.count() > 0) {
            std::this_thread::sleep_for(sleepUs);
        }
    }
}

// ---------------------------------------------------------------------------
// Per-tick logic
// ---------------------------------------------------------------------------

void SimClock::tick() {
    std::lock_guard<std::mutex> lock(m_state->mutex);

    // ------------------------------------------------------------------
    // 1. Advance UTC
    // ------------------------------------------------------------------
    m_state->utcHours += TICK_SEC / 3600.0;
    if (m_state->utcHours >= 24.0) {
        m_state->utcHours -= 24.0;
        m_state->utcDate.d++;
        static const int daysInMonth[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        int y  = m_state->utcDate.y;
        int mo = m_state->utcDate.m;
        int maxDay = daysInMonth[mo];
        if (mo == 2 && ((y%4==0 && y%100!=0) || y%400==0)) maxDay = 29;
        if (m_state->utcDate.d > maxDay) {
            m_state->utcDate.d = 1;
            m_state->utcDate.m++;
            if (m_state->utcDate.m > 12) { m_state->utcDate.m = 1; m_state->utcDate.y++; }
        }
    }

    // Only advance coordinates if date and time have been set by the driver
    if (!m_state->dateReady || !m_state->timeReady) {
        // Phase 4: focuser and rotator motion does not depend on date/time —
        // advance them even before the driver has set the site clock.
        if (m_cfg) {
            for (int i = 0; i < m_cfg->numFocusers && i < 6; ++i) {
                tickFocuser(i);
            }
            if (m_cfg->hasRotator) {
                tickRotator();
            }
        }
        return;
    }

    // ------------------------------------------------------------------
    // 2. Compute LST from UTC + longitude
    // ------------------------------------------------------------------
    double lst = gmst(m_state->utcHours,
                      m_state->utcDate.y,
                      m_state->utcDate.m,
                      m_state->utcDate.d)
                 + m_state->sites[m_state->currentSite].longitude / 15.0;
    lst = wrapHours(lst);

    // ------------------------------------------------------------------
    // 3. Mount state-machine updates
    // ------------------------------------------------------------------
    MountState ms = m_state->mountState;

    if (ms != m_prevMountState) {
        if (ms == MountState::SLEWING_GOTO) beginGoto();
        if (ms == MountState::PARKING)      beginPark();
        if (ms == MountState::HOMING)       beginHome();
        m_prevMountState = ms;
    }

    if (ms == MountState::TRACKING || ms == MountState::GUIDING) {
        double rateScale = m_state->trackingRateHz / SIDEREAL_HZ;
        double raAdvanceSec = SIDEREAL_RATE_DEG_PER_SEC * rateScale * TICK_SEC;
        m_state->ra += raAdvanceSec / 15.0;
        m_state->ra  = wrapHours(m_state->ra);
        m_state->ha  = wrapHours(lst - m_state->ra);
    }

    if (ms == MountState::SLEWING_GOTO) {
        if (m_gotoTicksRemaining <= 0) {
            m_state->ra  = m_state->targetRA;
            m_state->dec = m_state->targetDec;
            m_state->ha  = wrapHours(lst - m_state->ra);
            if (m_cfg->mountType == MOUNT_GEM ||
                m_cfg->mountType == MOUNT_GEM_TA ||
                m_cfg->mountType == MOUNT_GEM_TAC) {
                m_state->pierSide = (m_state->ha < 0.0) ? PIER_SIDE_EAST : PIER_SIDE_WEST;
            }
            m_state->mountState = MountState::TRACKING;
            m_state->gotoState  = GotoState::DONE;
            m_state->isTracking = true;
        } else {
            double frac = 1.0 - static_cast<double>(m_gotoTicksRemaining) /
                                static_cast<double>(m_gotoTicksRemaining + 1);
            m_state->ra  = m_gotoStartRA  + frac * (m_state->targetRA  - m_gotoStartRA);
            m_state->dec = m_gotoStartDec + frac * (m_state->targetDec - m_gotoStartDec);
            m_state->ha  = wrapHours(lst - m_state->ra);
            --m_gotoTicksRemaining;
        }
    }

    if (ms == MountState::PARKING) {
        if (m_parkTicksRemaining <= 0) {
            m_state->ra         = m_state->parkRA;
            m_state->dec        = m_state->parkDec;
            m_state->ha         = wrapHours(lst - m_state->ra);
            m_state->isTracking = false;
            m_state->mountState = MountState::PARKED;
            m_state->parkState  = PS_PARKED;
        } else {
            double frac = 1.0 - static_cast<double>(m_parkTicksRemaining) /
                                static_cast<double>(m_parkTicksRemaining + 1);
            m_state->ra  = m_parkStartRA  + frac * (m_state->parkRA  - m_parkStartRA);
            m_state->dec = m_parkStartDec + frac * (m_state->parkDec - m_parkStartDec);
            m_state->ha  = wrapHours(lst - m_state->ra);
            --m_parkTicksRemaining;
        }
    }

    if (ms == MountState::HOMING) {
        if (m_homeTicksRemaining <= 0) {
            m_state->isAtHome   = true;
            m_state->homeState  = HomeState::IDLE;
            m_state->mountState = MountState::STANDBY;
            m_state->isTracking = false;
        } else {
            --m_homeTicksRemaining;
        }
    }

    // Always refresh HA
    m_state->ha = wrapHours(lst - m_state->ra);

    // ------------------------------------------------------------------
    // 3b. Phase 8 — Jog and pulse guide motion
    // ------------------------------------------------------------------
    // Independent of mountState (matches firmware: guiding can run
    // concurrently with TRACKING). beginGoto()/beginPark()/beginHome()
    // already clear jog/pulse fields via MountStateMachine, so this never
    // races with goto/park/home interpolation above.
    applyJogAndPulse(lst);

    // ------------------------------------------------------------------
    // 4. Phase 4 — Focuser and rotator motion
    // ------------------------------------------------------------------
    if (m_cfg) {
        for (int i = 0; i < m_cfg->numFocusers && i < 6; ++i) {
            tickFocuser(i);
        }
        if (m_cfg->hasRotator) {
            tickRotator();
        }
    }
}

// ---------------------------------------------------------------------------
// Phase 8 — Jog and pulse guide motion (called while mutex IS held)
// ---------------------------------------------------------------------------
//
// Reachability note: jog/pulse motion can only be started via GuideHandler,
// whose validateGuide() rejects the command while mountState == STANDBY —
// and SimState::init() always pairs STANDBY with dateReady=false/
// timeReady=false. So this method is never reached on a tick where lst
// could not have been computed; tick() only calls it after the date/time
// early-return above.

void SimClock::applyJogAndPulse(double lst) {
    bool anyMotion = false;

    // --- Axis1 (RA) -----------------------------------------------------
    if (m_state->jogDirectionAxis1 != GuideDirection::NONE) {
        anyMotion = true;
        bool ok = advanceAndClampAxis(1, m_state->jogDirectionAxis1,
                                       m_state->jogRateDegPerSecAxis1, lst);
        if (!ok) {
            m_state->jogDirectionAxis1     = GuideDirection::NONE;
            m_state->jogRateDegPerSecAxis1 = 0.0;
        }
    }
    if (m_state->pulseDirectionAxis1 != GuideDirection::NONE) {
        anyMotion = true;
        bool ok = advanceAndClampAxis(1, m_state->pulseDirectionAxis1,
                                       m_state->pulseRateDegPerSecAxis1, lst);
        if (ok && m_state->pulseTicksRemainingAxis1 > 0) {
            --m_state->pulseTicksRemainingAxis1;
        }
        if (!ok || m_state->pulseTicksRemainingAxis1 <= 0) {
            m_state->pulseDirectionAxis1      = GuideDirection::NONE;
            m_state->pulseRateDegPerSecAxis1  = 0.0;
            m_state->pulseTicksRemainingAxis1 = 0;
        }
    }

    // --- Axis2 (Dec) ------------------------------------------------------
    if (m_state->jogDirectionAxis2 != GuideDirection::NONE) {
        anyMotion = true;
        bool ok = advanceAndClampAxis(2, m_state->jogDirectionAxis2,
                                       m_state->jogRateDegPerSecAxis2, lst);
        if (!ok) {
            m_state->jogDirectionAxis2     = GuideDirection::NONE;
            m_state->jogRateDegPerSecAxis2 = 0.0;
        }
    }
    if (m_state->pulseDirectionAxis2 != GuideDirection::NONE) {
        anyMotion = true;
        bool ok = advanceAndClampAxis(2, m_state->pulseDirectionAxis2,
                                       m_state->pulseRateDegPerSecAxis2, lst);
        if (ok && m_state->pulseTicksRemainingAxis2 > 0) {
            --m_state->pulseTicksRemainingAxis2;
        }
        if (!ok || m_state->pulseTicksRemainingAxis2 <= 0) {
            m_state->pulseDirectionAxis2      = GuideDirection::NONE;
            m_state->pulseRateDegPerSecAxis2  = 0.0;
            m_state->pulseTicksRemainingAxis2 = 0;
        }
    }

    if (!anyMotion) return;

    // Refresh HA after any RA motion this tick.
    m_state->ha = wrapHours(lst - m_state->ra);

    // If neither axis has any jog/pulse left active, clear the shared
    // status flags so :GU#/:Gu# correctly report "not guiding" again.
    bool stillActive =
        (m_state->jogDirectionAxis1   != GuideDirection::NONE) ||
        (m_state->jogDirectionAxis2   != GuideDirection::NONE) ||
        (m_state->pulseDirectionAxis1 != GuideDirection::NONE) ||
        (m_state->pulseDirectionAxis2 != GuideDirection::NONE);
    if (!stillActive) {
        m_state->guideState = GuideState::NONE;
        m_state->pulseGuide = GuideState::NONE;
    }
}

// Advance one axis by rateDegPerSec * TICK_SEC, signed by dir, then clamp
// to that axis's stored limit pair and reject (no partial application) if
// the resulting altitude would fall outside horizonMin/Max.
//
// Returns false if the move was rejected outright (altitude violation) OR
// landed exactly on an axis-limit clamp — both cases mean the caller should
// auto-stop that axis's jog/pulse, mirroring a real mount hitting a limit
// switch. Returns true only when the full, unclamped move was applied.
bool SimClock::advanceAndClampAxis(int axis, GuideDirection dir,
                                    double rateDegPerSec, double lst) {
    double sign     = (dir == GuideDirection::PLUS) ? 1.0 : -1.0;
    double deltaDeg = sign * rateDegPerSec * TICK_SEC;

    if (axis == 1) {
        // ra is stored in hours; axis1LimitMin/Max are stored in degrees.
        // Verified against LimitsHandler: :GXEe#/:GXEw# report
        // axis1LimitMin/Max directly in degrees, and :GXEB# divides
        // axis1LimitMax by 15 to additionally report it in hours.
        double raDeg = m_state->ra * 15.0 + deltaDeg;

        bool clamped = false;
        if (raDeg < m_state->axis1LimitMin) { raDeg = m_state->axis1LimitMin; clamped = true; }
        if (raDeg > m_state->axis1LimitMax) { raDeg = m_state->axis1LimitMax; clamped = true; }

        double newRa  = wrapHours(raDeg / 15.0);
        double altDeg = altitudeDeg(newRa, m_state->dec, lst);
        if (altDeg < m_state->horizonMin || altDeg > m_state->horizonMax) {
            return false; // reject the whole move this tick
        }

        m_state->ra = newRa;
        return !clamped;
    } else {
        double decDeg = m_state->dec + deltaDeg;

        bool clamped = false;
        if (decDeg < m_state->axis2LimitMin) { decDeg = m_state->axis2LimitMin; clamped = true; }
        if (decDeg > m_state->axis2LimitMax) { decDeg = m_state->axis2LimitMax; clamped = true; }

        double altDeg = altitudeDeg(m_state->ra, decDeg, lst);
        if (altDeg < m_state->horizonMin || altDeg > m_state->horizonMax) {
            return false;
        }

        m_state->dec = decDeg;
        return !clamped;
    }
}

// Altitude in degrees for an arbitrary ra (hours) / dec (degrees) pair at
// the given LST (hours), using the configured site latitude. Self-contained
// so SimClock has no reverse dependency on MountStateMachine (which has its
// own, narrower targetAltitudeDeg() used only for :MS# goto validation).
double SimClock::altitudeDeg(double raHours, double decDeg, double lstHours) const {
    double haHours = wrapHours(lstHours - raHours);
    double lat     = m_state->sites[m_state->currentSite].latitude * PI / 180.0;
    double dec     = decDeg * PI / 180.0;
    double haRad   = haHours * 15.0 * PI / 180.0;
    double sinAlt  = std::sin(dec) * std::sin(lat) +
                      std::cos(dec) * std::cos(lat) * std::cos(haRad);
    sinAlt = std::max(-1.0, std::min(1.0, sinAlt));
    return std::asin(sinAlt) * 180.0 / PI;
}

// ---------------------------------------------------------------------------
// Mount state transition helpers (called while mutex IS held)
// ---------------------------------------------------------------------------

void SimClock::beginGoto() {
    m_gotoStartRA  = m_state->ra;
    m_gotoStartDec = m_state->dec;
    double sep = angularSeparationDeg(m_state->ra * 15.0, m_state->dec,
                                      m_state->targetRA * 15.0, m_state->targetDec);
    double durationSec = std::max(sep / m_state->slewRateDegPerSec, 0.5);
    durationSec /= m_slewMultiplier;
    m_gotoTicksRemaining = std::max(1, static_cast<int>(durationSec * TICK_HZ));
}

void SimClock::beginPark() {
    m_parkStartRA  = m_state->ra;
    m_parkStartDec = m_state->dec;
    double durationSec = static_cast<double>(m_parkDurationMs) / 1000.0 / m_slewMultiplier;
    m_parkTicksRemaining = std::max(1, static_cast<int>(durationSec * TICK_HZ));
}

void SimClock::beginHome() {
    double durationSec = static_cast<double>(m_homeDurationMs) / 1000.0 / m_slewMultiplier;
    m_homeTicksRemaining = std::max(1, static_cast<int>(durationSec * TICK_HZ));
}

// ---------------------------------------------------------------------------
// Phase 4 — Focuser tick (called while mutex IS held)
// ---------------------------------------------------------------------------

void SimClock::tickFocuser(int slot) {
    FocuserState& f = m_state->focuser[slot];

    // Detect start of new move (isMoving just became true)
    if (f.isMoving && !m_focuserPrevMoving[slot]) {
        // Nothing to prime — step size is determined per-tick from gotoRate
    }
    m_focuserPrevMoving[slot] = f.isMoving;

    if (!f.isMoving) return;
    if (f.positionSteps == f.targetSteps) {
        f.isMoving = false;
        return;
    }

    long stepsPerTick = focuserStepsPerTick(f.gotoRate);

    long delta = f.targetSteps - f.positionSteps;
    if (delta > 0) {
        long advance = std::min(delta, stepsPerTick);
        f.positionSteps += advance;
    } else {
        long advance = std::min(-delta, stepsPerTick);
        f.positionSteps -= advance;
    }

    // Clamp to limits
    if (f.positionSteps < f.limitMinSteps) f.positionSteps = f.limitMinSteps;
    if (f.positionSteps > f.limitMaxSteps && f.limitMaxSteps > 0)
        f.positionSteps = f.limitMaxSteps;

    if (f.positionSteps == f.targetSteps) {
        f.isMoving = false;
    }
}

// ---------------------------------------------------------------------------
// Phase 4 — Rotator tick (called while mutex IS held)
// ---------------------------------------------------------------------------

void SimClock::tickRotator() {
    RotatorState& r = m_state->rotator;

    m_rotatorPrevMoving = r.isMoving;

    if (!r.isMoving) return;

    double diff = r.targetAngle - r.angle;
    // Wrap diff to [-180, +180] for shortest-path rotation
    while (diff >  180.0) diff -= 360.0;
    while (diff < -180.0) diff += 360.0;

    if (std::fabs(diff) < 1e-6) {
        r.angle    = r.targetAngle;
        r.isMoving = false;
        return;
    }

    double degsPerTick = rotatorDegsPerTick(r.gotoRate);
    double step = std::min(std::fabs(diff), degsPerTick);
    r.angle += (diff > 0.0) ? step : -step;
    r.angle = wrapDeg(r.angle);

    // Re-wrap target for comparison after wrap
    double wrappedTarget = wrapDeg(r.targetAngle);
    double remaining = wrappedTarget - r.angle;
    while (remaining >  180.0) remaining -= 360.0;
    while (remaining < -180.0) remaining += 360.0;

    if (std::fabs(remaining) < 1e-4) {
        r.angle    = wrappedTarget;
        r.isMoving = false;
    }
}

// ---------------------------------------------------------------------------
// Phase 4 — Rate helpers
// ---------------------------------------------------------------------------

long SimClock::focuserStepsPerTick(int gotoRate) {
    if (gotoRate < 1) gotoRate = 1;
    if (gotoRate > 5) gotoRate = 5;
    return FOCUSER_STEPS_PER_TICK[gotoRate];
}

double SimClock::rotatorDegsPerTick(int gotoRate) {
    if (gotoRate < 1) gotoRate = 1;
    if (gotoRate > 9) gotoRate = 9;
    return ROTATOR_DEGS_PER_TICK[gotoRate];
}

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

double SimClock::gmst(double utcHours, int y, int m, int d) {
    auto julianDay = [](int yr, int mo, int da) -> double {
        int a  = (14 - mo) / 12;
        int yy = yr + 4800 - a;
        int mm = mo + 12 * a - 3;
        return da + (153*mm + 2)/5 + 365*yy + yy/4 - yy/100 + yy/400 - 32045.0;
    };
    double jd  = julianDay(y, m, d) + utcHours / 24.0;
    double t   = (jd - 2451545.0) / 36525.0;
    double gmstDeg = 280.46061837
                   + 360.98564736629 * (jd - 2451545.0)
                   + 0.000387933 * t * t
                   - t * t * t / 38710000.0;
    return wrapHours(gmstDeg / 15.0);
}

double SimClock::angularSeparationDeg(double ra1Deg, double dec1,
                                      double ra2Deg, double dec2) {
    double r1 = ra1Deg * PI / 180.0;
    double d1 = dec1   * PI / 180.0;
    double r2 = ra2Deg * PI / 180.0;
    double d2 = dec2   * PI / 180.0;
    double cosSep = std::sin(d1)*std::sin(d2) +
                    std::cos(d1)*std::cos(d2)*std::cos(r1 - r2);
    cosSep = std::max(-1.0, std::min(1.0, cosSep));
    return std::acos(cosSep) * 180.0 / PI;
}

double SimClock::wrapHours(double h) {
    while (h >= 24.0) h -= 24.0;
    while (h <   0.0) h += 24.0;
    return h;
}

double SimClock::wrapDeg(double d) {
    while (d >= 360.0) d -= 360.0;
    while (d <    0.0) d += 360.0;
    return d;
}
