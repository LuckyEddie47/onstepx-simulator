// test_home_handler.cpp — Unit tests for HomeHandler

#include "SimTestBase.h"
#include "handlers/HomeHandler.h"
#include "state/MountStateMachine.h"
#include "state/SimClock.h"

#include <cstring>
#include <cstdio>

class HomeHandlerTest : public SimTestBase {
protected:
    SimState          simState;
    SimClock          simClock;
    MountStateMachine msm;
    HomeHandler       handler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);
        msm.setConfig(&cfg);
        msm.setState(&simState);
        msm.setClock(&simClock);
        handler.setConfig(&cfg);
        handler.setState(&simState);
        handler.setStateMachine(&msm);
    }
};

TEST_F(HomeHandlerTest, hQuery_Format) {
    simState.homeOffsetAxis1 = 100;
    simState.homeOffsetAxis2 = -200;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("h?", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    int hasSense = cfg.hasHomeSense ? 1 : 0;
    char expected[64];
    std::snprintf(expected, sizeof(expected), "%d,100,-200", hasSense);
    EXPECT_STREQ(reply, expected);
}

TEST_F(HomeHandlerTest, hA_SetAutoHome) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hA", "1", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_TRUE(simState.autoHomeAtBoot);

    ASSERT_TRUE(handler.handle("hA", "0", reply, &sf, &nr, &err));
    EXPECT_FALSE(simState.autoHomeAtBoot);
}

TEST_F(HomeHandlerTest, hA_BadParam_ReturnsParamRange) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hA", "2", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_PARAM_RANGE);
}

TEST_F(HomeHandlerTest, hC_InitiatesHoming) {
    // Phase 14: firmware requires trusted || hasSense, dateReady, timeReady,
    // no goto/guide in progress.
    simState.mountState     = MountState::TRACKING;
    simState.parkState      = PS_UNPARKED;
    simState.startupTrusted = true;
    simState.dateReady      = true;
    simState.timeReady      = true;

    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hC", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.mountState, MountState::HOMING);
}

TEST_F(HomeHandlerTest, hC1_SetsAxis1Offset) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hC", "1,3600", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.homeOffsetAxis1, 3600L);
}

TEST_F(HomeHandlerTest, hC1_OutOfRange_ReturnsParamRange) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hC", "1,999999", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_PARAM_RANGE);
}

TEST_F(HomeHandlerTest, hC2_SetsAxis2Offset) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hC", "2,-1800", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.homeOffsetAxis2, -1800L);
}

TEST_F(HomeHandlerTest, hF_ResetsMountAndClearsPark) {
    simState.mountState     = MountState::TRACKING;
    simState.parkState      = PS_PARKED;
    simState.parkPositionSet = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hF", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(simState.parkState,        PS_UNPARKED);
    EXPECT_FALSE(simState.parkPositionSet);
    EXPECT_TRUE(simState.isAtHome);
}

TEST_F(HomeHandlerTest, NonHCommand_NotConsumed) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("GU", "", reply, &sf, &nr, &err));
}

// ---------------------------------------------------------------------------
// Phase 14 — Home precondition chain (audit 2.4)
// Firmware reference: Home::request() in Home.cpp lines 50-67.
// ---------------------------------------------------------------------------

// Helper: puts state into the minimum valid condition for :hC# to succeed.
static void setHomeReadyState(SimState& s, const SimConfig& cfg) {
    s.startupTrusted = true;               // trusted OR hasSense needed
    s.dateReady      = true;
    s.timeReady      = true;
    s.gotoState      = GotoState::NONE;
    s.guideState     = GuideState::NONE;
    s.parkState      = PS_UNPARKED;
    (void)cfg;
}

TEST_F(HomeHandlerTest, Phase14_hC_NotTrustedNoSense_RejectsUnspecified) {
    // Firmware line 57: if (!trusted && !hasSense) return CE_SLEW_ERR_UNSPECIFIED
    // When hasSense=false (most configs), untrusted state must be rejected.
    if (cfg.hasHomeSense) GTEST_SKIP() << "Config has home sense — trusted check bypassed";
    setHomeReadyState(simState, cfg);
    simState.startupTrusted = false;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hC", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_SLEW_ERR_UNSPECIFIED)
        << "hC# without trust or home sense must return CE_SLEW_ERR_UNSPECIFIED";
    EXPECT_NE(simState.mountState, MountState::HOMING);
}

TEST_F(HomeHandlerTest, Phase14_hC_DateNotReady_RejectsInStandby) {
    // Firmware line 61: if (!site.dateIsReady) return CE_SLEW_ERR_IN_STANDBY
    setHomeReadyState(simState, cfg);
    simState.dateReady = false;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hC", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_SLEW_ERR_IN_STANDBY)
        << "hC# without date set must return CE_SLEW_ERR_IN_STANDBY";
    EXPECT_NE(simState.mountState, MountState::HOMING);
}

TEST_F(HomeHandlerTest, Phase14_hC_TimeNotReady_RejectsInStandby) {
    // Firmware line 61: if (!site.timeIsReady) return CE_SLEW_ERR_IN_STANDBY
    setHomeReadyState(simState, cfg);
    simState.timeReady = false;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hC", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_SLEW_ERR_IN_STANDBY)
        << "hC# without time set must return CE_SLEW_ERR_IN_STANDBY";
    EXPECT_NE(simState.mountState, MountState::HOMING);
}

TEST_F(HomeHandlerTest, Phase14_hC_GotoInProgress_RejectsMountInMotion) {
    // Firmware line 62: if (goTo.state != GS_NONE) return CE_SLEW_IN_MOTION
    setHomeReadyState(simState, cfg);
    simState.gotoState = GotoState::GOTO;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hC", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_MOUNT_IN_MOTION)
        << "hC# during a goto must return CE_MOUNT_IN_MOTION";
    EXPECT_NE(simState.mountState, MountState::HOMING);
}

TEST_F(HomeHandlerTest, Phase14_hC_GuideInProgress_RejectsMountInMotion) {
    // Firmware line 63: if (guide.state != GU_NONE) return CE_SLEW_IN_MOTION
    setHomeReadyState(simState, cfg);
    simState.guideState = GuideState::ACTIVE;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hC", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_MOUNT_IN_MOTION)
        << "hC# during a guide must return CE_MOUNT_IN_MOTION";
    EXPECT_NE(simState.mountState, MountState::HOMING);
}

TEST_F(HomeHandlerTest, Phase14_hC_HasSense_AllowsUntrusted) {
    // Firmware: !trusted && !hasSense → reject; !trusted && hasSense → allow
    if (!cfg.hasHomeSense) GTEST_SKIP() << "Config has no home sense";
    setHomeReadyState(simState, cfg);
    simState.startupTrusted = false;   // untrusted but has sense
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hC", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE)
        << "hC# with home sense should succeed even when untrusted";
    EXPECT_EQ(simState.mountState, MountState::HOMING);
}
