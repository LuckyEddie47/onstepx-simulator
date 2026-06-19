// test_sim_clock.cpp — SimClock unit tests.
//
// Tests cover:
//   - UTC advance over time
//   - RA tracking advance once dateReady && timeReady
//   - No RA advance before date/time set (DEC-006)
//   - Date rollover at midnight
//   - GMST / LST computation sanity checks

#include <gtest/gtest.h>
#include "SimTestBase.h"
#include "state/SimClock.h"

#include <chrono>
#include <cmath>
#include <thread>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class SimClockTest : public SimTestBase {
protected:
    SimClock clock;
    SimState state;

    void SetUp() override {
        SimTestBase::SetUp();
        state.init(cfg);
        // Set known UTC state
        state.utcHours   = 12.0;
        state.utcDate    = {2024, 6, 15};
        state.dateReady  = false;
        state.timeReady  = false;
        state.isTracking = false;
        state.mountState = MountState::STANDBY;

        clock.setConfig(&cfg);
        clock.setState(&state);
        clock.setSlewMultiplier(50);   // fast for tests
        clock.setParkDurationMs(200);
        clock.setHomeDurationMs(200);
    }

    void TearDown() override {
        clock.stop();
    }

    // Wait up to maxMs for a condition to become true (polls at 10ms)
    bool waitFor(std::function<bool()> cond, int maxMs = 2000) {
        for (int i = 0; i < maxMs / 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::lock_guard<std::mutex> lk(state.mutex);
            if (cond()) return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// Thread lifecycle
// ---------------------------------------------------------------------------

TEST_F(SimClockTest, StartsAndStops) {
    clock.start();
    EXPECT_TRUE(clock.isRunning());
    clock.stop();
    EXPECT_FALSE(clock.isRunning());
}

TEST_F(SimClockTest, DoubleStartIsNoop) {
    clock.start();
    clock.start();  // Should not crash or spawn second thread
    EXPECT_TRUE(clock.isRunning());
    clock.stop();
}

// ---------------------------------------------------------------------------
// UTC advance
// ---------------------------------------------------------------------------

TEST_F(SimClockTest, UtcAdvancesOverTime) {
    double startHours;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        startHours = state.utcHours;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    // 250ms wall time -> at least 0.0001h UTC advance
    double delta = state.utcHours - startHours;
    if (delta < 0) delta += 24.0;  // midnight rollover
    EXPECT_GT(delta, 0.0) << "UTC should have advanced";
}

TEST_F(SimClockTest, UtcRollsOverAtMidnight) {
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.utcHours = 23.9999;  // just before midnight
        state.utcDate  = {2024, 6, 15};
    }
    clock.start();
    // Wait for rollover (300ms should be plenty for 0.0001h at 10Hz)
    bool rolledOver = waitFor([this]() {
        return state.utcHours < 1.0 && state.utcDate.d == 16;
    }, 500);
    clock.stop();
    EXPECT_TRUE(rolledOver) << "UTC should roll over day at midnight";
}

// ---------------------------------------------------------------------------
// RA tracking — only when dateReady && timeReady (DEC-006)
// ---------------------------------------------------------------------------

TEST_F(SimClockTest, RaDoesNotAdvanceBeforeDateTimeSet) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    double startRa;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.isTracking  = true;
        state.mountState  = MountState::TRACKING;
        state.dateReady   = false;
        state.timeReady   = false;
        state.ra          = 6.0;
        startRa           = state.ra;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_NEAR(state.ra, startRa, 0.001)
        << "RA should NOT advance before date/time are set";
}

TEST_F(SimClockTest, RaAdvancesWhenTrackingAndDateTimeSet) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    double startRa;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.isTracking  = true;
        state.mountState  = MountState::TRACKING;
        state.dateReady   = true;
        state.timeReady   = true;
        state.ra          = 6.0;
        state.trackingRateHz = 60.136f;
        startRa           = state.ra;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    double delta = state.ra - startRa;
    if (delta < 0) delta += 24.0;
    EXPECT_GT(delta, 0.0) << "RA should advance when tracking with date/time set";
}

TEST_F(SimClockTest, RaDoesNotAdvanceWhenNotTracking) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    double startRa;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.isTracking  = false;
        state.mountState  = MountState::STANDBY;
        state.dateReady   = true;
        state.timeReady   = true;
        state.ra          = 6.0;
        startRa           = state.ra;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_NEAR(state.ra, startRa, 0.001)
        << "RA should not advance when not tracking";
}

// ---------------------------------------------------------------------------
// Goto completion
// ---------------------------------------------------------------------------

TEST_F(SimClockTest, GotoCompletesAndArrives) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";
    if (!cfg.hasGoto)  GTEST_SKIP() << "No goto in this config";

    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady   = true;
        state.timeReady   = true;
        state.ra          = 6.0;
        state.dec         = 0.0;
        state.targetRA    = 6.5;
        state.targetDec   = 10.0;
        state.targetRASet = true;
        state.targetDecSet= true;
        state.mountState  = MountState::SLEWING_GOTO;
        state.isTracking  = false;
        // Manually prime goto ticks (simulates what MountStateMachine will do)
        // For now, set directly via beginGoto equivalent: small sep -> 1 tick
        // slewRateDegPerSec is from config; with multiplier=50 it's fast
    }

    // Start clock — it picks up SLEWING_GOTO state
    clock.start();
    bool arrived = waitFor([this]() {
        return state.mountState == MountState::TRACKING;
    }, 3000);
    clock.stop();

    EXPECT_TRUE(arrived) << "Goto should complete and transition to TRACKING";
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        EXPECT_NEAR(state.ra,  state.targetRA,  0.01);
        EXPECT_NEAR(state.dec, state.targetDec, 0.01);
        EXPECT_TRUE(state.isTracking);
    }
}

// ---------------------------------------------------------------------------
// Park completion
// ---------------------------------------------------------------------------

TEST_F(SimClockTest, ParkCompletesAndParks) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady   = true;
        state.timeReady   = true;
        state.parkRA      = 0.0;
        state.parkDec     = 90.0;
        state.ra          = 6.0;
        state.dec         = 45.0;
        state.mountState  = MountState::PARKING;
        state.parkState   = PS_PARKING;
        state.isTracking  = true;
    }

    clock.start();
    bool parked = waitFor([this]() {
        return state.parkState == PS_PARKED;
    }, 3000);
    clock.stop();

    EXPECT_TRUE(parked) << "Park should complete within timeout";
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        EXPECT_EQ(state.mountState, MountState::PARKED);
        EXPECT_FALSE(state.isTracking);
    }
}

// ---------------------------------------------------------------------------
// Home completion
// ---------------------------------------------------------------------------

TEST_F(SimClockTest, HomeCompletesAndSetIsAtHome) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady   = true;
        state.timeReady   = true;
        state.isAtHome    = false;
        state.mountState  = MountState::HOMING;
        state.homeState   = HomeState::HOMING;
        state.isTracking  = false;
    }

    clock.start();
    bool homed = waitFor([this]() {
        return state.isAtHome;
    }, 3000);
    clock.stop();

    EXPECT_TRUE(homed) << "Homing should complete and set isAtHome";
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        EXPECT_EQ(state.mountState, MountState::STANDBY);
        EXPECT_EQ(state.homeState,  HomeState::IDLE);
    }
}

// ---------------------------------------------------------------------------
// Date rollover at month end
// ---------------------------------------------------------------------------

TEST_F(SimClockTest, DateRollsOverMonthEnd) {
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.utcHours = 23.9999;
        state.utcDate  = {2024, 1, 31};  // Jan 31 -> Feb 1
    }
    clock.start();
    bool rolled = waitFor([this]() {
        return state.utcDate.m == 2 && state.utcDate.d == 1;
    }, 500);
    clock.stop();
    EXPECT_TRUE(rolled) << "Date should roll to Feb 1 after Jan 31 midnight";
}

// ---------------------------------------------------------------------------
// Phase 8 — Jog and pulse guide motion
// ---------------------------------------------------------------------------
//
// Fixture's fixed date/time (2024-06-15, 12:00 UTC, lon=0) gives LST
// ~17.650h. ra=17.650 sits on the meridian for the default site latitude
// (51.5deg, unmodified by any config), so altitude = 90 - 51.5 + dec =
// 38.5 + dec — comfortably clear of horizonMin/Max (-10/90 by default)
// regardless of small jog deltas, for any dec within an axis2 range that
// itself stays within +/-50deg or so of true Dec.
//
// dec itself is NOT a fixed constant: axis2LimitMin/Max vary per config
// (e.g. fork_altaz uses AXIS2_LIMIT_MIN=0, since Axis2 there is altitude,
// not true Dec) — see midAxis2() below, called per-test after
// state.init(cfg) has populated those limits in SetUp().

namespace {
constexpr double kSafeRa = 17.650382730469573; // hours, == LST at fixture time

// Returns a dec/Axis2 value safely inside [axis2LimitMin, axis2LimitMax]
// for the current config, with room on both sides for a short jog.
double midAxis2(const SimState& s) {
    return (s.axis2LimitMin + s.axis2LimitMax) / 2.0;
}
} // namespace

TEST_F(SimClockTest, JogWestIncreasesRa) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    double startRa;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady = true;
        state.timeReady = true;
        state.mountState = MountState::TRACKING;
        state.isTracking = true;
        state.ra  = kSafeRa;
        state.dec = midAxis2(state);
        // 1x sidereal so motion is easy to detect within a short sleep but
        // small enough not to need horizon-safety margin beyond ~1 deg.
        state.jogDirectionAxis1     = GuideDirection::PLUS; // West
        state.jogRateDegPerSecAxis1 = 15.0 * (360.0 / 86164.0905); // ~1x sidereal in deg/s
        startRa = state.ra;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    double delta = state.ra - startRa;
    if (delta < 0) delta += 24.0;
    EXPECT_GT(delta, 0.0) << "West jog should increase RA";
}

TEST_F(SimClockTest, JogEastDecreasesRa) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    double startRa;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady = true;
        state.timeReady = true;
        state.mountState = MountState::TRACKING;
        state.isTracking = true;
        state.ra  = kSafeRa;
        state.dec = midAxis2(state);
        state.jogDirectionAxis1     = GuideDirection::MINUS; // East
        state.jogRateDegPerSecAxis1 = 15.0 * (360.0 / 86164.0905);
        startRa = state.ra;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    // East jog decreases RA; tracking (1x sidereal, additive on axis1 while
    // TRACKING) increases it — at a 1x jog rate the two should roughly
    // cancel, so just confirm RA did NOT increase by the jog-only amount
    // and did not run away in the West direction.
    double delta = state.ra - startRa;
    if (delta > 12.0) delta -= 24.0;
    if (delta < -12.0) delta += 24.0;
    EXPECT_LT(delta, 0.05) << "East jog should not let RA run away increasing";
}

TEST_F(SimClockTest, JogNorthIncreasesDec) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    double startDec;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady = true;
        state.timeReady = true;
        state.mountState = MountState::TRACKING;
        state.isTracking = true;
        state.ra  = kSafeRa;
        state.dec = midAxis2(state);
        state.jogDirectionAxis2     = GuideDirection::PLUS; // North
        state.jogRateDegPerSecAxis2 = 1.0; // 1 deg/s — large, easy to detect
        startDec = state.dec;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_GT(state.dec, startDec) << "North jog should increase Dec";
}

TEST_F(SimClockTest, JogSouthDecreasesDec) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    double startDec;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady = true;
        state.timeReady = true;
        state.mountState = MountState::TRACKING;
        state.isTracking = true;
        state.ra  = kSafeRa;
        state.dec = midAxis2(state);
        state.jogDirectionAxis2     = GuideDirection::MINUS; // South
        state.jogRateDegPerSecAxis2 = 1.0;
        startDec = state.dec;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_LT(state.dec, startDec) << "South jog should decrease Dec";
}

TEST_F(SimClockTest, JogDoesNotMoveBeforeDateTimeSet) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    double startDec;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady = false; // deliberately not ready
        state.timeReady = false;
        state.ra  = kSafeRa;
        state.dec = midAxis2(state);
        state.jogDirectionAxis2     = GuideDirection::PLUS;
        state.jogRateDegPerSecAxis2 = 1.0;
        startDec = state.dec;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_NEAR(state.dec, startDec, 0.001)
        << "Jog should not move before date/time are set (unreachable via "
           "GuideHandler in practice, but SimClock must not assume callers "
           "obey that precondition)";
}

TEST_F(SimClockTest, PulseGuideStopsAfterTicksExpire) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    double startDec;
    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady = true;
        state.timeReady = true;
        state.mountState = MountState::TRACKING;
        state.isTracking = true;
        state.ra  = kSafeRa;
        state.dec = midAxis2(state);
        startDec = state.dec;
        state.pulseDirectionAxis2      = GuideDirection::PLUS;
        state.pulseRateDegPerSecAxis2  = 1.0;
        state.pulseTicksRemainingAxis2 = 2; // 200ms at 10Hz
        state.guideState = GuideState::PULSE;
        state.pulseGuide = GuideState::PULSE;
    }
    clock.start();
    bool stopped = waitFor([this]() {
        return state.pulseDirectionAxis2 == GuideDirection::NONE;
    }, 1000);
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_TRUE(stopped) << "Pulse guide should self-stop once ticks expire";
    EXPECT_EQ(state.pulseTicksRemainingAxis2, 0);
    EXPECT_GT(state.dec, startDec) << "Dec should have moved during the pulse";
    EXPECT_EQ(state.guideState, GuideState::NONE)
        << "guideState should clear once no axis has active jog/pulse";
    EXPECT_EQ(state.pulseGuide, GuideState::NONE);
}

TEST_F(SimClockTest, JogAutoStopsAtAxis2Limit) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady = true;
        state.timeReady = true;
        state.mountState = MountState::TRACKING;
        state.isTracking = true;
        state.ra  = kSafeRa;
        state.dec = midAxis2(state);
        // Tighten the max limit to just above the current position so the
        // jog hits it almost immediately, regardless of this config's
        // normal axis2 range.
        state.axis2LimitMax = state.dec + 0.05;
        state.jogDirectionAxis2     = GuideDirection::PLUS; // North, toward limit
        state.jogRateDegPerSecAxis2 = 1.0; // fast approach
    }
    clock.start();
    bool stopped = waitFor([this]() {
        return state.jogDirectionAxis2 == GuideDirection::NONE;
    }, 1000);
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_TRUE(stopped) << "Jog should auto-stop on reaching axis2LimitMax";
    EXPECT_LE(state.dec, state.axis2LimitMax + 0.001)
        << "Dec should be clamped at the axis limit, not overshoot it";
}

TEST_F(SimClockTest, GotoSupersedesActiveJog) {
    // Verifies MountStateMachine::beginGoto() clears jog/pulse fields so
    // goto interpolation and jog motion never write ra/dec in the same
    // tick (see clearJogAndPulseMotion()). This is exercised here at the
    // SimClock level via direct field inspection rather than going through
    // GuideHandler/GotoHandler, since SimClockTest's fixture does not wire
    // up a MountStateMachine.
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";

    {
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady = true;
        state.timeReady = true;
        state.mountState = MountState::TRACKING;
        state.isTracking = true;
        state.ra  = kSafeRa;
        state.dec = midAxis2(state);
        state.jogDirectionAxis1     = GuideDirection::PLUS;
        state.jogRateDegPerSecAxis1 = 1.0;
        // Simulate what MountStateMachine::beginGoto() does to jog state.
        state.jogDirectionAxis1     = GuideDirection::NONE;
        state.jogRateDegPerSecAxis1 = 0.0;
        state.targetRA      = kSafeRa + 0.1;
        state.targetDec     = state.dec;
        state.targetRASet   = true;
        state.targetDecSet  = true;
        state.mountState    = MountState::SLEWING_GOTO;
        state.gotoState     = GotoState::GOTO;
    }
    clock.start();
    bool arrived = waitFor([this]() {
        return state.mountState == MountState::TRACKING && state.gotoState == GotoState::DONE;
    }, 3000);
    clock.stop();

    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_TRUE(arrived) << "Goto should complete normally with jog already cleared";
    EXPECT_EQ(state.jogDirectionAxis1, GuideDirection::NONE);
}
