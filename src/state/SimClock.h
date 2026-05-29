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
// Phase 4 additions:
//   - Per-focuser motion: step positionSteps toward targetSteps each tick
//     while FocuserState::isMoving is true.  Motion rate is derived from the
//     focuser's gotoRate / moveRate selection — see FOCUSER_STEPS_PER_TICK[].
//   - Rotator motion: step angle toward targetAngle each tick while
//     RotatorState::isMoving is true.
//
// All SimState access is mutex-protected.
// Durations are scaled by slewMultiplier (CLI --slew-multiplier).
//
// DEC-006: SimClock starts ticking immediately. RA advancement only begins
// once state.dateReady && state.timeReady are both true.

#include "SimState.h"
#include "config/SimConfig.h"

#include <atomic>
#include <thread>

class SimClock {
public:
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

    // Mount tick state (accessed only by background thread)
    MountState m_prevMountState = MountState::STANDBY;
    int    m_gotoTicksRemaining  = 0;
    int    m_parkTicksRemaining  = 0;
    int    m_homeTicksRemaining  = 0;
    double m_gotoStartRA         = 0.0;
    double m_gotoStartDec        = 0.0;
    double m_parkStartRA         = 0.0;
    double m_parkStartDec        = 0.0;

    // Phase 4 — Focuser tick state (one entry per focuser slot 0..5).
    // Tracks whether each focuser was moving on the previous tick so we can
    // detect the start of a new move and prime the step-rate calculation.
    bool m_focuserPrevMoving[6] = {};

    // Phase 4 — Rotator tick state
    bool m_rotatorPrevMoving = false;

    // Called while mutex IS held by tick().
    void beginGoto();
    void beginPark();
    void beginHome();

    // Phase 4 helpers — called while mutex IS held by tick().
    // Advance one focuser slot by up to stepsPerTick toward its target.
    // Clears isMoving when the target is reached.
    void tickFocuser(int slot);

    // Advance the rotator by up to degsPerTick toward its targetAngle.
    // Clears isMoving when the target is reached.
    void tickRotator();

    // Compute focuser step size per tick for a given gotoRate selection (1-5).
    // The divisor is NOT scaled by slewMultiplier — focuser moves are already
    // slow; multiplier only applies to mount slews.
    static long focuserStepsPerTick(int gotoRate);

    // Compute rotator degrees per tick for a given gotoRate selection (1-9).
    static double rotatorDegsPerTick(int gotoRate);

    void threadFunc();
    void tick();

    // Coordinate helpers
    static double gmst(double utcHours, int y, int m, int d);
    static double angularSeparationDeg(double ra1, double dec1,
                                       double ra2, double dec2);
    static double wrapHours(double h);
    static double wrapDeg(double d);
};
