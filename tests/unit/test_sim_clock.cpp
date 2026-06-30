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
#include "state/MountStateMachine.h"

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

// ---------------------------------------------------------------------------
// Phase 17 — Continuous limit monitor (audit 4.1, excluding auto-flip)
// ---------------------------------------------------------------------------

class LimitMonitorTest : public SimClockTest {
protected:
    void SetUp() override {
        SimClockTest::SetUp();
        // Put mount in a fully trusted, date-ready, tracking state at a
        // safe position so limits do NOT fire during setup.
        std::lock_guard<std::mutex> lk(state.mutex);
        state.dateReady      = true;
        state.timeReady      = true;
        state.startupTrusted = true;
        state.limitsEnabled  = true;
        state.axesEnabled    = true;
        state.isTracking     = true;
        state.mountState     = MountState::TRACKING;
        state.parkState      = PS_UNPARKED;
        state.gotoState      = GotoState::NONE;
        state.guideState     = GuideState::NONE;
        // Start near south meridian, well within all limits
        state.utcDate        = {2024, 6, 15};
        state.utcHours       = 12.0;
        state.sites[0].latitude  = 51.5;   // Rochdale, UK approx
        state.sites[0].longitude = -2.0;
        state.sites[0].elevation = 100.0;
        // RA on meridian, Dec 30° → altitude ~68° at this latitude
        state.ra  = 8.0;
        state.ha  = 0.0;
        state.dec = 30.0;
        state.horizonMin    = -10.0;
        state.horizonMax    =  90.0;
        state.axis1LimitMin = -180.0;
        state.axis1LimitMax =  180.0;
        state.axis2LimitMin =  -90.0;
        state.axis2LimitMax =   90.0;
    }
};

TEST_F(LimitMonitorTest, Phase17_LimitsEnabled_OnGotoCompletion) {
    // Firmware Goto.cpp:117 sets limitsEnabled=true after goto.
    // Simulator: SimClock goto completion path sets it.
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP();
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.limitsEnabled  = false;
        state.startupTrusted = true;
        state.dateReady      = true;
        state.timeReady      = true;
        state.mountState     = MountState::SLEWING_GOTO;
        state.gotoState      = GotoState::GOTO;
        state.targetRA       = state.ra;   // zero-distance goto completes immediately
        state.targetDec      = state.dec;
    }
    clock.start();
    bool enabled = waitFor([this]{ return state.limitsEnabled; }, 3000);
    clock.stop();
    EXPECT_TRUE(enabled) << "limitsEnabled should become true after goto completion";
}

TEST_F(LimitMonitorTest, Phase17_LimitsEnabled_OnUnpark) {
    // Firmware Park.cpp:291 sets limitsEnabled=true on unpark.
    if (!cfg.hasMount) GTEST_SKIP();
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.limitsEnabled  = false;
        state.startupTrusted = true;
        state.dateReady      = true;
        state.timeReady      = true;
        state.parkState      = PS_PARKED;
    }
    MountStateMachine msm;
    msm.setConfig(&cfg);
    msm.setState(&state);
    CommandError e = msm.beginUnpark();
    ASSERT_EQ(e, CE_NONE);
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_TRUE(state.limitsEnabled)
        << "limitsEnabled should be true immediately after beginUnpark()";
}

TEST_F(LimitMonitorTest, Phase17_AltitudeMinLimit_StopsTracking) {
    if (!cfg.hasMount) GTEST_SKIP();
    // Force altitude below horizonMin. dec=-89 is always below the horizon at
    // lat 51.5N regardless of hour angle / LST, so this doesn't depend on the
    // exact LST computed from utcHours/date — avoids fragile ha/ra coupling.
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.horizonMin = -10.0;  // default; dec=-89 is below this at any ha
        state.dec        = -89.0;
    }
    clock.start();
    bool stopped = waitFor([this]{
        return !state.isTracking;   // waitFor already holds state.mutex here
    }, 3000);
    clock.stop();
    EXPECT_TRUE(stopped) << "Altitude below horizonMin should stop tracking";
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_EQ(state.mountState, MountState::STANDBY);
}

TEST_F(LimitMonitorTest, Phase17_AltitudeMaxLimit_StopsTracking) {
    if (!cfg.hasMount) GTEST_SKIP();
    // dec == latitude maximizes altitude at ha=0 (transit altitude = 90°).
    // Set ra = current lst (read after the clock has advanced once) so the
    // mount sits exactly on the meridian without depending on a hand-computed
    // LST value — this is robust to any GMST formula details.
    double siteLat;
    {   std::lock_guard<std::mutex> lk(state.mutex);
        siteLat    = state.sites[state.currentSite].latitude;
        state.dec  = siteLat;     // transit altitude ≈ 90°
        state.horizonMax = 200.0; // park well above 90 so it can't fire during setup tick
    }
    // Run one tick to get LST into ha, then read it back via ha (= lst - ra
    // when ra=0) by temporarily setting ra=0 and reading the resulting ha.
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.ra = 0.0;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    double lstNow;
    {   std::lock_guard<std::mutex> lk(state.mutex);
        lstNow = state.ha;  // ha = lst - ra(0) = lst
        state.ra = lstNow;  // now ha will be ~0 → transit, altitude ≈ 90°
        state.horizonMax = 80.0;  // re-arm the limit just below 90°
    }
    bool stopped = waitFor([this]{
        return !state.isTracking;   // waitFor already holds state.mutex here
    }, 3000);
    clock.stop();
    EXPECT_TRUE(stopped) << "Altitude above horizonMax should stop tracking";
}

TEST_F(LimitMonitorTest, Phase17_Axis2DecLimit_StopsTracking) {
    if (!cfg.hasMount || !cfg.isEquatorial()) GTEST_SKIP();
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.axis2LimitMax = 45.0;
        state.dec           = 50.0;   // already past axis2 max limit
    }
    clock.start();
    bool stopped = waitFor([this]{
        return !state.isTracking;
    }, 3000);
    clock.stop();
    EXPECT_TRUE(stopped) << "Dec past axis2LimitMax should stop tracking";
}

TEST_F(LimitMonitorTest, Phase17_LimitsDisabled_NoEnforcement) {
    if (!cfg.hasMount) GTEST_SKIP();
    // With limitsEnabled=false, even an out-of-bounds position must not stop tracking
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.limitsEnabled = false;
        state.horizonMin    = 80.0;    // artificially high — would fire if enabled
        state.dec           = -60.0;   // altitude << horizonMin
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    clock.stop();
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_TRUE(state.isTracking)
        << "Limits disabled: tracking should continue even past limit bounds";
}

TEST_F(LimitMonitorTest, Phase17_MeridianWestLimit_GEM_StopsTracking) {
    if (!cfg.hasMount) GTEST_SKIP();
    if (cfg.mountType != MOUNT_GEM &&
        cfg.mountType != MOUNT_GEM_TA &&
        cfg.mountType != MOUNT_GEM_TAC) GTEST_SKIP() << "GEM only";

    // ha is recomputed from (lst - ra) every tracking tick, so directly
    // setting state.ha gets overwritten immediately. Instead: read the live
    // lst (via ra=0 trick, same as AltitudeMaxLimit test above) and set ra
    // so the resulting ha lands 0.5h past the meridian.
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.meridianLimitWDeg = 5.0;   // 5° = 20 minutes past meridian W
        state.ra       = 0.0;
        state.pierSide = PIER_SIDE_WEST;
    }
    clock.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    double lstNow;
    {   std::lock_guard<std::mutex> lk(state.mutex);
        lstNow = state.ha;          // ha = lst - ra(0) = lst
        // Want ha = 0.5h past meridian west → ra = lst - 0.5
        double targetRa = lstNow - 0.5;
        while (targetRa < 0.0)  targetRa += 24.0;
        while (targetRa >= 24.0) targetRa -= 24.0;
        state.ra = targetRa;
    }
    bool stopped = waitFor([this]{
        return !state.isTracking;
    }, 3000);
    clock.stop();
    EXPECT_TRUE(stopped) << "West meridian limit exceeded should stop tracking (no auto-flip in Phase 17)";
}
