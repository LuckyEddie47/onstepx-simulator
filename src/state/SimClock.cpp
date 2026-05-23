// SimClock.cpp — Simulated time and coordinate update engine

#include "SimClock.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr double PI         = 3.14159265358979323846;
static constexpr double SIDEREAL_HZ = 60.136;          // Hz
static constexpr double SIDEREAL_RATE_DEG_PER_SEC = 360.0 / 86164.0905; // deg/s
static constexpr int    TICK_HZ   = 10;                // background thread rate
static constexpr double TICK_SEC  = 1.0 / TICK_HZ;    // seconds per tick

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
        // Advance date (simple day rollover, ignoring month/year for now)
        m_state->utcDate.d++;
        // Rough month-end handling — sufficient for simulation durations
        static const int daysInMonth[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        int y = m_state->utcDate.y;
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
    if (!m_state->dateReady || !m_state->timeReady) return;

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
    // 3. State-machine updates
    // Detect transitions and prime timed operations automatically.
    // ------------------------------------------------------------------
    MountState ms = m_state->mountState;

    if (ms != m_prevMountState) {
        if (ms == MountState::SLEWING_GOTO) beginGoto();
        if (ms == MountState::PARKING)      beginPark();
        if (ms == MountState::HOMING)       beginHome();
        m_prevMountState = ms;
    }

    if (ms == MountState::TRACKING || ms == MountState::GUIDING) {
        // Advance RA at sidereal rate (scaled by trackingRateHz / SIDEREAL_HZ)
        double rateScale = m_state->trackingRateHz / SIDEREAL_HZ;
        double raAdvanceSec = SIDEREAL_RATE_DEG_PER_SEC * rateScale * TICK_SEC;
        m_state->ra += raAdvanceSec / 15.0;  // deg/s -> hr/s
        m_state->ra  = wrapHours(m_state->ra);
        m_state->ha  = wrapHours(lst - m_state->ra);
    }

    if (ms == MountState::SLEWING_GOTO) {
        if (m_gotoTicksRemaining <= 0) {
            // Arrived — snap to target
            m_state->ra  = m_state->targetRA;
            m_state->dec = m_state->targetDec;
            m_state->ha  = wrapHours(lst - m_state->ra);

            // Determine pier side for GEM mounts
            if (m_cfg->mountType == MOUNT_GEM ||
                m_cfg->mountType == MOUNT_GEM_TA ||
                m_cfg->mountType == MOUNT_GEM_TAC) {
                m_state->pierSide = (m_state->ha < 0.0) ? PIER_SIDE_EAST : PIER_SIDE_WEST;
            }

            m_state->mountState = MountState::TRACKING;
            m_state->gotoState  = GotoState::DONE;
            m_state->isTracking = true;
        } else {
            // Interpolate linearly
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
            // Parked
            m_state->ra        = m_state->parkRA;
            m_state->dec       = m_state->parkDec;
            m_state->ha        = wrapHours(lst - m_state->ra);
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

    // Always update HA
    m_state->ha = wrapHours(lst - m_state->ra);
}

// ---------------------------------------------------------------------------
// State transition helpers (called while mutex IS held by caller)
// These are called by MountStateMachine which holds the lock.
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
// Coordinate helpers
// ---------------------------------------------------------------------------

// Greenwich Mean Sidereal Time in hours for given UTC
double SimClock::gmst(double utcHours, int y, int m, int d) {
    // Simple GMST formula accurate to ~0.1s for simulation purposes
    // JD at J2000.0 = 2451545.0
    // JD = julianDay(y,m,d) + utcHours/24.0
    auto julianDay = [](int yr, int mo, int da) -> double {
        int a = (14 - mo) / 12;
        int yy = yr + 4800 - a;
        int mm = mo + 12 * a - 3;
        return da + (153*mm + 2)/5 + 365*yy + yy/4 - yy/100 + yy/400 - 32045.0;
    };
    double jd = julianDay(y, m, d) + utcHours / 24.0;
    double t  = (jd - 2451545.0) / 36525.0;  // Julian centuries from J2000.0
    double gmstDeg = 280.46061837
                   + 360.98564736629 * (jd - 2451545.0)
                   + 0.000387933 * t * t
                   - t * t * t / 38710000.0;
    // Convert degrees to hours and wrap to [0, 24)
    return wrapHours(gmstDeg / 15.0);
}

double SimClock::angularSeparationDeg(double ra1Deg, double dec1,
                                      double ra2Deg, double dec2) {
    double r1 = ra1Deg  * PI / 180.0;
    double d1 = dec1    * PI / 180.0;
    double r2 = ra2Deg  * PI / 180.0;
    double d2 = dec2    * PI / 180.0;
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
