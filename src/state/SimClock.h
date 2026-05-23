#pragma once
// SimClock.h — Simulated time and coordinate update engine.
//
// Runs a background thread at 10 Hz. Responsibilities:
//   - Maintain UTC (initialised from system clock; settable via site commands)
//   - Compute LST = GMST(UTC) + longitude
//   - Compute HA = LST - RA
//   - While tracking: advance RA at sidereal rate (scaled by trackingRateHz)
//   - While SLEWING_GOTO: interpolate RA/Dec toward target; fire callback on arrival
//   - While PARKING: interpolate toward park position; fire callback on completion
//   - While HOMING: fire callback after home_duration_ms
//
// All SimState access is mutex-protected.
// Durations are scaled by slewMultiplier (CLI --slew-multiplier).
//
// DEC-006: SimClock starts ticking immediately. RA advancement only begins
// once state.dateReady && state.timeReady are both true.

#include "SimState.h"
#include "config/SimConfig.h"

#include <atomic>
#include <functional>
#include <thread>

class SimClock {
public:
    using CompletionCb = std::function<void()>;

    SimClock() = default;
    ~SimClock() { stop(); }

    // Inject dependencies
    void setConfig(const SimConfig* cfg) { m_cfg = cfg; }
    void setState(SimState* state)       { m_state = state; }

    // Scale all simulated motion durations (--slew-multiplier N)
    void setSlewMultiplier(int n)        { m_slewMultiplier = n > 0 ? n : 1; }

    // Park / home completion timeouts (before multiplier scaling)
    void setParkDurationMs(int ms)  { m_parkDurationMs  = ms; }
    void setHomeDurationMs(int ms)  { m_homeDurationMs  = ms; }

    // Start the 10 Hz background thread
    void start();

    // Stop the background thread (blocks until thread exits)
    void stop();

    bool isRunning() const { return m_running.load(); }

private:
    const SimConfig* m_cfg   = nullptr;
    SimState*        m_state = nullptr;

    int m_slewMultiplier = 10;
    int m_parkDurationMs = 2000;
    int m_homeDurationMs = 3000;

    std::atomic<bool> m_running{false};
    std::thread       m_thread;

    // Tick state (accessed only by background thread)
    MountState m_prevMountState = MountState::STANDBY;  // detect state transitions
    int    m_gotoTicksRemaining  = 0;
    int    m_parkTicksRemaining  = 0;
    int    m_homeTicksRemaining  = 0;
    double m_gotoStartRA         = 0.0;
    double m_gotoStartDec        = 0.0;
    double m_parkStartRA         = 0.0;
    double m_parkStartDec        = 0.0;

    // Called by MountStateMachine (with mutex held) to prime timed operations.
    // Also called internally on first detection of each state.
    void beginGoto();
    void beginPark();
    void beginHome();

    void threadFunc();
    void tick();

    // Coordinate helpers
    static double gmst(double utcHours, int y, int m, int d);
    static double angularSeparationDeg(double ra1, double dec1,
                                       double ra2, double dec2);
    static double wrapHours(double h);
    static double wrapDeg(double d);
};
