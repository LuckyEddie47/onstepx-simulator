// test_mount_state_machine.cpp — MountStateMachine unit tests.
// Config-driven per DEC-001. All tests that require a mount skip when !cfg.hasMount.

#include <gtest/gtest.h>
#include "SimTestBase.h"
#include "state/MountStateMachine.h"
#include "state/SimClock.h"

#include <chrono>
#include <thread>

class MountStateMachineTest : public SimTestBase {
protected:
    SimState          state;
    SimClock          clock;
    MountStateMachine msm;

    void SetUp() override {
        SimTestBase::SetUp();
        state.init(cfg);
        state.dateReady = true;
        state.timeReady = true;
        state.utcHours  = 12.0;
        state.utcDate   = {2024, 6, 15};
        state.sites[0].latitude  = 51.5;
        state.sites[0].longitude = 0.0;

        clock.setConfig(&cfg);
        clock.setState(&state);
        clock.setSlewMultiplier(100);
        clock.setParkDurationMs(100);
        clock.setHomeDurationMs(100);

        msm.setConfig(&cfg);
        msm.setState(&state);
        msm.setClock(&clock);
    }

    void TearDown() override { clock.stop(); }

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
// Tracking
// ---------------------------------------------------------------------------

TEST_F(MountStateMachineTest, StartTracking) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    CommandError e = msm.startTracking();
    EXPECT_EQ(e, CE_NONE);
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_TRUE(state.isTracking);
    EXPECT_EQ(state.mountState, MountState::TRACKING);
}

TEST_F(MountStateMachineTest, StopTracking) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    msm.startTracking();
    CommandError e = msm.stopTracking();
    EXPECT_EQ(e, CE_NONE);
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_FALSE(state.isTracking);
    EXPECT_EQ(state.mountState, MountState::STANDBY);
}

TEST_F(MountStateMachineTest, CannotStartTrackingWhenParked) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex); state.parkState = PS_PARKED; }
    CommandError e = msm.startTracking();
    EXPECT_EQ(e, CE_PARKED);
}

// ---------------------------------------------------------------------------
// Goto validation
// ---------------------------------------------------------------------------

// Phase 11: the old "no target set" precondition (CE_SLEW_ERR_SLEW when
// targetRASet/targetDecSet were false) no longer exists — it had no
// firmware equivalent. Goto now proceeds to RA=0/Dec=0 if no target was
// set, matching firmware behavior (gotoTarget is zero-initialized).
// This test is repurposed to cover the new FIRST precondition: the
// unconditional startupTrusted check (firmware's Goto::request() line 81).
TEST_F(MountStateMachineTest, GotoFailsUntrusted) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex);
      state.startupTrusted = false; }
    CommandError e = msm.beginGoto();
    EXPECT_EQ(e, CE_SLEW_ERR_UNSPECIFIED)
        << "Goto without alignment/trust should return CE_SLEW_ERR_UNSPECIFIED ('9')";
}

TEST_F(MountStateMachineTest, GotoFailsWhenParked) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex);
      state.startupTrusted = true;   // get past the trust check to reach the park check
      state.targetRA = 6.0; state.targetDec = 45.0;
      state.parkState = PS_PARKED; }
    CommandError e = msm.beginGoto();
    EXPECT_EQ(e, CE_SLEW_ERR_IN_PARK);
}

// Phase 11: the old standby check was dateReady/timeReady — that was the
// wrong condition (firmware checks !axis1.isEnabled() || !axis2.isEnabled()).
// The new check is !axesEnabled — but since isAtHome defaults to true,
// the auto-recovery path normally avoids this entirely at fresh-boot.
// This test exercises the case where the mount has moved away from home
// (isAtHome=false) and axes are disabled — the only state where
// CE_SLEW_ERR_IN_STANDBY is truly unreachable from firmware's own design
// (except deliberately, via some future axis-disable command).
TEST_F(MountStateMachineTest, GotoFailsWhenAxesDisabledAndNotAtHome) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex);
      state.startupTrusted = true;
      state.axesEnabled    = false;
      state.isAtHome       = false;   // prevents auto-recovery
      state.targetRA = 6.0; state.targetDec = 45.0;
      state.parkState = PS_UNPARKED; }
    CommandError e = msm.beginGoto();
    EXPECT_EQ(e, CE_SLEW_ERR_IN_STANDBY);
}

TEST_F(MountStateMachineTest, GotoSucceeds) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex);
      state.startupTrusted = true;   // Phase 11: required by new trust gate
      state.targetRASet = true; state.targetDecSet = true;
      state.targetRA = 6.0; state.targetDec = 45.0;
      state.parkState = PS_UNPARKED; }
    CommandError e = msm.beginGoto();
    EXPECT_EQ(e, CE_NONE);
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_EQ(state.mountState, MountState::SLEWING_GOTO);
    EXPECT_EQ(state.gotoState,  GotoState::GOTO);
}

// Phase 11: new test — goto from a fresh-boot state (isAtHome=true,
// axesEnabled=false) should auto-enable axes and succeed, matching
// firmware's Goto::setTarget() recovery path when mount is at home.
TEST_F(MountStateMachineTest, GotoAutoEnablesAxesWhenAtHome) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex);
      state.startupTrusted = true;
      state.axesEnabled    = false;
      state.isAtHome       = true;   // triggers auto-enable recovery
      state.targetRA = 6.0; state.targetDec = 45.0;
      state.parkState = PS_UNPARKED; }
    CommandError e = msm.beginGoto();
    EXPECT_EQ(e, CE_NONE) << "Goto at home should auto-enable axes and succeed";
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_TRUE(state.axesEnabled) << "axesEnabled should be true after auto-enable";
    EXPECT_FALSE(state.isAtHome)   << "isAtHome should be false once goto starts";
}

TEST_F(MountStateMachineTest, GotoCompletesViaSimClock) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";

    // Set up state before starting clock so clock sees consistent initial state.
    // Phase 11: startupTrusted required by the new trust gate.
    // DEC-016 fix: the fixture's default utcHours=12.0 on 2024-06-15
    // puts RA=6.5/Dec=10.0 at ~-27° altitude (below the horizon), making
    // validateGoto() return CE_SLEW_ERR_BELOW_HORIZON — the root cause of
    // the pre-Phase-11 time-of-day flakiness. Overriding to utcHours=0.0
    // gives altitude ~47° (confirmed above-horizon for the full UTC
    // 22:00-03:00 window, independent of when the test actually runs).
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.utcHours     = 0.0;   // DEC-016 fix: safe altitude window
        state.ra  = 6.0;  state.dec = 0.0;
        state.targetRA  = 6.5; state.targetDec = 10.0;
        state.targetRASet  = true;
        state.targetDecSet = true;
        state.mountState   = MountState::STANDBY;
        state.isTracking   = false;
        state.parkState    = PS_UNPARKED;
        state.dateReady    = true;
        state.timeReady    = true;
        state.startupTrusted = true;   // Phase 11: required
    }

    // Start clock, then immediately trigger goto — no settle delay needed
    // since m_prevMountState=STANDBY matches initial mountState=STANDBY,
    // and the very next tick after beginGoto() sets SLEWING_GOTO will detect
    // the transition and call beginGoto() on the SimClock.
    clock.start();
    CommandError e = msm.beginGoto();
    ASSERT_EQ(e, CE_NONE) << "beginGoto() should succeed";

    // At 10Hz + multiplier=100: goto completes in ~2 ticks (~200ms).
    // Wait up to 5s to be robust against slow CI machines.
    bool arrived = waitFor([this](){
        return state.mountState == MountState::TRACKING;
    }, 5000);

    EXPECT_TRUE(arrived) << "Goto should complete and transition to TRACKING";
    {   std::lock_guard<std::mutex> lk(state.mutex);
        EXPECT_NEAR(state.ra,  6.5,  0.05);
        EXPECT_NEAR(state.dec, 10.0, 0.05);
        EXPECT_TRUE(state.isTracking);
    }
}

TEST_F(MountStateMachineTest, AbortGotoRestoresTracking) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex);
      state.startupTrusted = true;  // Phase 11: required
      state.targetRASet = true; state.targetDecSet = true;
      state.targetRA = 6.0; state.targetDec = 45.0; }
    msm.beginGoto();
    msm.abortGoto();
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_EQ(state.mountState, MountState::TRACKING);
    EXPECT_EQ(state.gotoState,  GotoState::NONE);
}

// ---------------------------------------------------------------------------
// Park
// ---------------------------------------------------------------------------

TEST_F(MountStateMachineTest, ParkTransitionsToParkingState) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    // Phase 14: set all preconditions that beginPark() now enforces.
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.parkPositionSet = true;
        state.parkState       = PS_UNPARKED;
        state.startupTrusted  = true;
        state.axesEnabled     = true;
    }
    CommandError e = msm.beginPark();
    EXPECT_EQ(e, CE_NONE);
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_EQ(state.parkState,  PS_PARKING);
    EXPECT_EQ(state.mountState, MountState::PARKING);
    EXPECT_FALSE(state.isTracking);
}

TEST_F(MountStateMachineTest, ParkFailsWhenAlreadyParked) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    // Phase 14: firmware returns CE_NONE (success no-op) when already parked.
    // ParkHandler.command.cpp then maps CE_NONE to CE_1.
    // beginPark() itself returns CE_NONE for the already-parked case.
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.parkPositionSet = true;
        state.parkState       = PS_PARKED;
        state.startupTrusted  = true;
        state.axesEnabled     = true;
    }
    CommandError e = msm.beginPark();
    EXPECT_EQ(e, CE_NONE) << "Already-parked returns CE_NONE (Park.cpp line 89)";
    // State must be unchanged — still parked, not transitioning to PS_PARKING.
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_EQ(state.parkState, PS_PARKED);
}

TEST_F(MountStateMachineTest, ParkCompletesViaSimClock) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.parkRA          = 0.0;  state.parkDec = 90.0;
        state.parkState       = PS_UNPARKED;
        state.mountState      = MountState::STANDBY;
        state.isTracking      = false;
        state.dateReady       = true;
        state.timeReady       = true;
        state.parkPositionSet = true;   // Phase 14
        state.startupTrusted  = true;   // Phase 14
        state.axesEnabled     = true;   // Phase 14
    }
    clock.start();
    CommandError e = msm.beginPark();
    ASSERT_EQ(e, CE_NONE) << "beginPark() should succeed";
    bool parked = waitFor([this](){ return state.parkState == PS_PARKED; }, 5000);
    EXPECT_TRUE(parked) << "Park should complete within timeout";
    {   std::lock_guard<std::mutex> lk(state.mutex);
        EXPECT_FALSE(state.isTracking);
        EXPECT_EQ(state.mountState, MountState::PARKED);
    }
}

TEST_F(MountStateMachineTest, SetParkPosition) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex);
      state.ra = 3.5; state.dec = -20.0; }
    msm.setParkPosition();
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_NEAR(state.parkRA,  3.5,  0.001);
    EXPECT_NEAR(state.parkDec, -20.0, 0.001);
    EXPECT_TRUE(state.parkPositionSet);
}

TEST_F(MountStateMachineTest, UnparkFromParked) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex); state.parkState = PS_PARKED; }
    CommandError e = msm.beginUnpark();
    EXPECT_EQ(e, CE_NONE);
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_EQ(state.parkState, PS_UNPARKED);
    EXPECT_TRUE(state.isTracking);
}

// ---------------------------------------------------------------------------
// Home
// ---------------------------------------------------------------------------

TEST_F(MountStateMachineTest, BeginHomeTransitionsState) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    // Phase 14: set all preconditions beginHome() now enforces.
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.startupTrusted = true;
        state.dateReady      = true;
        state.timeReady      = true;
    }
    CommandError e = msm.beginHome();
    EXPECT_EQ(e, CE_NONE);
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_EQ(state.mountState, MountState::HOMING);
    EXPECT_EQ(state.homeState,  HomeState::HOMING);
}

TEST_F(MountStateMachineTest, HomeCompletesViaSimClock) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    {   std::lock_guard<std::mutex> lk(state.mutex);
        state.isAtHome       = false;
        state.homeState      = HomeState::IDLE;
        state.mountState     = MountState::STANDBY;
        state.isTracking     = false;
        state.dateReady      = true;
        state.timeReady      = true;
        state.startupTrusted = true;   // Phase 14
    }
    clock.start();
    CommandError e = msm.beginHome();
    ASSERT_EQ(e, CE_NONE) << "beginHome() should succeed";
    bool homed = waitFor([this](){ return state.isAtHome; }, 5000);
    EXPECT_TRUE(homed) << "Home should complete and set isAtHome=true";
    {   std::lock_guard<std::mutex> lk(state.mutex);
        EXPECT_EQ(state.mountState, MountState::STANDBY);
        EXPECT_EQ(state.homeState,  HomeState::IDLE);
    }
}

TEST_F(MountStateMachineTest, ResetHomeClears) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex);
      state.parkState = PS_PARKED;
      state.isAtHome  = false; }
    msm.resetHome();
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_TRUE(state.isAtHome);
    EXPECT_EQ(state.parkState, PS_UNPARKED);
    EXPECT_EQ(state.mountState, MountState::STANDBY);
}

// ---------------------------------------------------------------------------
// Sync
// ---------------------------------------------------------------------------

TEST_F(MountStateMachineTest, SyncToTargetUpdatesPosition) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    { std::lock_guard<std::mutex> lk(state.mutex);
      state.startupTrusted = true;   // Phase 11: required by new trust gate
      state.targetRA  = 10.0; state.targetDec = -30.0;
      state.targetRASet = true; state.targetDecSet = true; }
    msm.syncToTarget();
    std::lock_guard<std::mutex> lk(state.mutex);
    EXPECT_NEAR(state.ra,  10.0,  0.001);
    EXPECT_NEAR(state.dec, -30.0, 0.001);
    EXPECT_TRUE(state.alignDone);
}
