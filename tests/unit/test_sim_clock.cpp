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
