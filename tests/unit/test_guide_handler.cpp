// test_guide_handler.cpp — Unit tests for GuideHandler

#include "SimTestBase.h"
#include "handlers/GuideHandler.h"
#include "state/MountStateMachine.h"
#include "state/SimClock.h"

#include <cstring>

class GuideHandlerTest : public SimTestBase {
protected:
    SimState          simState;
    SimClock          simClock;
    MountStateMachine msm;
    GuideHandler      handler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);
        msm.setConfig(&cfg);
        msm.setState(&simState);
        msm.setClock(&simClock);
        handler.setConfig(&cfg);
        handler.setState(&simState);
        handler.setStateMachine(&msm);

        simState.mountState = MountState::TRACKING;
        simState.isTracking = true;
        simState.parkState  = PS_UNPARKED;
    }
};

// ---------------------------------------------------------------------------
// Guide rate set
// ---------------------------------------------------------------------------

TEST_F(GuideHandlerTest, NamedRate_RG_SetsIndex2) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("RG", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr); // :RG# returns nothing
    EXPECT_EQ(simState.guideRateSelect, 2);
    EXPECT_EQ(simState.pulseRateSelect, 2); // <= GR_1X so pulse updated too
}

TEST_F(GuideHandlerTest, NamedRate_RC_SetsIndex5) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("RC", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.guideRateSelect, 5);
}

TEST_F(GuideHandlerTest, NumericRate_R5_SetsIndex5) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("R5", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.guideRateSelect, 5);
}

TEST_F(GuideHandlerTest, NumericRate_CappedAt6_WhenNoGoto) {
    if (cfg.hasGoto) GTEST_SKIP() << "Config has goto — cap test N/A";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("R9", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.guideRateSelect, 6);
}

// ---------------------------------------------------------------------------
// GX90
// ---------------------------------------------------------------------------

TEST_F(GuideHandlerTest, GX90_ReturnsPulseRate) {
    simState.pulseRateSelect = 2; // GR_1X → 1.00

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "90", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_STREQ(reply, "1.00");
}

// ---------------------------------------------------------------------------
// Pulse guide
// ---------------------------------------------------------------------------

TEST_F(GuideHandlerTest, MG_PulseGuide_SetsGuidePulseState) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("MG", "w100", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.guideState, GuideState::PULSE);
}

TEST_F(GuideHandlerTest, Mg_PulseGuide_ReturnsNothing) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mg", "n200", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(err, CE_NONE);
}

TEST_F(GuideHandlerTest, Mg_NegativeDuration_ReturnsParamRange) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mg", "n-5", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_PARAM_RANGE);
}

// ---------------------------------------------------------------------------
// Continuous guide
// ---------------------------------------------------------------------------

TEST_F(GuideHandlerTest, Mw_ContinuousGuide_SetsActiveState) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mw", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.guideState, GuideState::ACTIVE);
}

// ---------------------------------------------------------------------------
// Spiral guide
// ---------------------------------------------------------------------------

TEST_F(GuideHandlerTest, Mp_SpiralGuide_SetsSpiralState) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mp", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.guideState, GuideState::SPIRAL);
}

// ---------------------------------------------------------------------------
// Halt commands
// ---------------------------------------------------------------------------

TEST_F(GuideHandlerTest, Q_HaltAll_ClearsGuideAndAbortsGoto) {
    simState.guideState = GuideState::ACTIVE;
    simState.mountState = MountState::SLEWING_GOTO;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Q", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(simState.guideState, GuideState::NONE);
    EXPECT_EQ(simState.pulseGuide, GuideState::NONE);
    EXPECT_NE(simState.mountState, MountState::SLEWING_GOTO);
}

TEST_F(GuideHandlerTest, Qe_ClearsAxis1Guide) {
    simState.guideState = GuideState::ACTIVE;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Qe", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(simState.guideState, GuideState::NONE);
}

// ---------------------------------------------------------------------------
// Precondition failures
// ---------------------------------------------------------------------------

TEST_F(GuideHandlerTest, Guide_WhenParked_Fails) {
    simState.mountState = MountState::PARKED;
    simState.parkState  = PS_PARKED;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mw", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_SLEW_ERR_IN_PARK);
    EXPECT_EQ(simState.guideState, GuideState::NONE);
}

TEST_F(GuideHandlerTest, Guide_WhenStandby_Fails) {
    simState.mountState = MountState::STANDBY;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mw", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_SLEW_ERR_IN_STANDBY);
}

TEST_F(GuideHandlerTest, Guide_WhenGotoActive_AbortsGotoReturnsInMotion) {
    simState.mountState = MountState::SLEWING_GOTO;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mw", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_SLEW_IN_MOTION);
    EXPECT_NE(simState.mountState, MountState::SLEWING_GOTO);
}
