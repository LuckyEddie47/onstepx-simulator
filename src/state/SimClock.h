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
// Phase 8 additions:
//   - Jog motion: while jogDirectionAxis{1,2} != NONE, advance ra (axis1)
//     and/or dec (axis2) each tick at jogRateDegPerSecAxis{1,2}, signed per
//     GuideDirection (PLUS = West/North, MINUS = East/South). Runs
//     independently of mountState — tracking continues on Axis1 underneath
//     a jog if the mount was TRACKING (matches firmware's per-axis guide
//     behaviour; see Decision Log).
//   - Pulse guide motion: same as jog but for a fixed number of ticks
//     (pulseTicksRemainingAxis{1,2}, decremented to 0), set by GuideHandler
//     from the :Mg#/:MG# duration in ms (quantized to 100ms ticks).
//   - Both jog and pulse motion clamp ra (via axis1LimitMin/Max, degrees)
//     and dec (via axis2LimitMin/Max) and the resulting altitude (via
//     horizonMin/Max). Hitting a limit auto-stops that axis's jog/pulse,
//     mirroring a real mount's limit switch.
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
    void pollLimits(double lst);   // Phase 17: continuous limit monitor

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

    // Phase 8 — apply jog and pulse guide motion for one tick.
    // Called while mutex IS held, after the goto/park/home block so that a
    // goto/park/home in progress (which already clears jog/pulse fields via
    // MountStateMachine) cannot race with this. lst is passed in so ha can
    // be refreshed after any motion without recomputing GMST.
    void applyJogAndPulse(double lst);

    // Phase 8 — advance one axis (1=ra in hours, 2=dec in degrees) by
    // rateDegPerSec * TICK_SEC, signed by dir, then clamp the result to
    // that axis's stored limit pair (axis1LimitMin/Max or
    // axis2LimitMin/Max). If the resulting altitude would fall outside
    // horizonMin/Max, the whole move is rejected (state unchanged) rather
    // than partially applied. lst is needed to recompute altitude for the
    // trial RA on axis 1.
    // Returns false if the move was rejected on altitude, or landed
    // exactly on an axis-limit clamp — both cases mean the caller should
    // auto-stop that axis's jog/pulse, mirroring a real mount hitting a
    // limit switch. Returns true only when the full, unclamped move was
    // applied.
    bool advanceAndClampAxis(int axis, GuideDirection dir,
                              double rateDegPerSec, double lst);

    // Phase 8 — altitude in degrees for an arbitrary ra (hours) / dec
    // (degrees) pair, given current LST (hours) and site latitude.
    // Self-contained (does not depend on MountStateMachine) so SimClock has
    // no reverse dependency on it.
    double altitudeDeg(double raHours, double decDeg, double lstHours) const;

    void threadFunc();
    void tick();

    // Coordinate helpers
    static double gmst(double utcHours, int y, int m, int d);
    static double angularSeparationDeg(double ra1, double dec1,
                                       double ra2, double dec2);
    static double wrapHours(double h);
    static double wrapDeg(double d);
};
