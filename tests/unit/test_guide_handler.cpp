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
    EXPECT_EQ(err, CE_MOUNT_IN_MOTION);  // Phase 10: renamed from CE_SLEW_IN_MOTION
    EXPECT_NE(simState.mountState, MountState::SLEWING_GOTO);
}

// ---------------------------------------------------------------------------
// Phase 8 — Jog motion fields (direction sign, rate, axis routing)
// ---------------------------------------------------------------------------
//
// Sign convention verified against firmware Guide.command.cpp:
//   :Mw# West -> Axis1 PLUS (RA increasing)
//   :Me# East -> Axis1 MINUS (RA decreasing)
//   :Mn# North-> Axis2 PLUS (Dec increasing)
//   :Ms# South-> Axis2 MINUS (Dec decreasing)

TEST_F(GuideHandlerTest, Mw_SetsAxis1PlusDirection) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mw", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.jogDirectionAxis1, GuideDirection::PLUS);
    EXPECT_GT(simState.jogRateDegPerSecAxis1, 0.0);
    EXPECT_EQ(simState.jogDirectionAxis2, GuideDirection::NONE)
        << "Mw must not affect Axis2";
}

TEST_F(GuideHandlerTest, Me_SetsAxis1MinusDirection) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Me", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.jogDirectionAxis1, GuideDirection::MINUS);
    EXPECT_GT(simState.jogRateDegPerSecAxis1, 0.0); // magnitude is unsigned
}

TEST_F(GuideHandlerTest, Mn_SetsAxis2PlusDirection) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mn", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.jogDirectionAxis2, GuideDirection::PLUS);
    EXPECT_GT(simState.jogRateDegPerSecAxis2, 0.0);
    EXPECT_EQ(simState.jogDirectionAxis1, GuideDirection::NONE)
        << "Mn must not affect Axis1";
}

TEST_F(GuideHandlerTest, Ms_SetsAxis2MinusDirection) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Ms", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.jogDirectionAxis2, GuideDirection::MINUS);
    EXPECT_GT(simState.jogRateDegPerSecAxis2, 0.0);
}

TEST_F(GuideHandlerTest, Mg_PulseGuide_SetsAxis2PulseFieldsFromDuration) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mg", "n300", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.pulseDirectionAxis2, GuideDirection::PLUS); // n = north
    EXPECT_GT(simState.pulseRateDegPerSecAxis2, 0.0);
    EXPECT_EQ(simState.pulseTicksRemainingAxis2, 3) // 300ms / 100ms-per-tick
        << "Duration should be quantized to 100ms ticks";
}

// ---------------------------------------------------------------------------
// Phase 8 — Custom guide rate storage (:RA#/:RE#)
// ---------------------------------------------------------------------------
//
// Prior to Phase 8 these commands parsed but discarded the rate value,
// leaving customRateAxis{1,2}DegPerSec at 0 — any jog/pulse issued with
// guideRateSelect==10 (custom) would silently not move. This is fixed as
// part of wiring jog motion up to real rates.

TEST_F(GuideHandlerTest, RA_StoresCustomAxis1Rate) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("RA", "2.5", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.guideRateSelect, 10);
    EXPECT_DOUBLE_EQ(simState.customRateAxis1DegPerSec, 2.5);
}

TEST_F(GuideHandlerTest, RE_StoresCustomAxis2Rate) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("RE", "0.75", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.guideRateSelect, 10);
    EXPECT_DOUBLE_EQ(simState.customRateAxis2DegPerSec, 0.75);
}

TEST_F(GuideHandlerTest, RA_ThenMw_UsesCustomRate) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("RA", "3.0", reply, &sf, &nr, &err));
    ASSERT_TRUE(handler.handle("Mw", "", reply, &sf, &nr, &err));
    EXPECT_DOUBLE_EQ(simState.jogRateDegPerSecAxis1, 3.0);
}

// ---------------------------------------------------------------------------
// Phase 8 — Per-axis halt (:Qe#/:Qw# vs :Qn#/:Qs#)
// ---------------------------------------------------------------------------
//
// Prior to Phase 8, stopAxis1()/stopAxis2() were identical and both cleared
// guideState/pulseGuide for BOTH axes — :Qe# would silently also stop a
// concurrent :Mn#/:Ms# jog. This was undetected because the only existing
// test (Qe_ClearsAxis1Guide, above) never checked Axis2 was left alone.

TEST_F(GuideHandlerTest, Qe_StopsAxis1Only_LeavesAxis2Jogging) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mw", "", reply, &sf, &nr, &err)); // start Axis1
    ASSERT_TRUE(handler.handle("Mn", "", reply, &sf, &nr, &err)); // start Axis2

    ASSERT_TRUE(handler.handle("Qe", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.jogDirectionAxis1, GuideDirection::NONE)
        << "Qe should stop Axis1";
    EXPECT_EQ(simState.jogDirectionAxis2, GuideDirection::PLUS)
        << "Qe must NOT stop Axis2 — this was a pre-Phase-8 bug";
    EXPECT_EQ(simState.guideState, GuideState::ACTIVE)
        << "guideState should remain ACTIVE — Axis2 is still jogging";
}

TEST_F(GuideHandlerTest, Qn_StopsAxis2Only_LeavesAxis1Jogging) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mw", "", reply, &sf, &nr, &err)); // start Axis1
    ASSERT_TRUE(handler.handle("Mn", "", reply, &sf, &nr, &err)); // start Axis2

    ASSERT_TRUE(handler.handle("Qn", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.jogDirectionAxis2, GuideDirection::NONE)
        << "Qn should stop Axis2";
    EXPECT_EQ(simState.jogDirectionAxis1, GuideDirection::PLUS)
        << "Qn must NOT stop Axis1";
}

TEST_F(GuideHandlerTest, Q_HaltAll_ClearsBothAxesJogFields) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Mw", "", reply, &sf, &nr, &err));
    ASSERT_TRUE(handler.handle("Mn", "", reply, &sf, &nr, &err));

    ASSERT_TRUE(handler.handle("Q", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.jogDirectionAxis1, GuideDirection::NONE);
    EXPECT_EQ(simState.jogDirectionAxis2, GuideDirection::NONE);
    EXPECT_EQ(simState.jogRateDegPerSecAxis1, 0.0);
    EXPECT_EQ(simState.jogRateDegPerSecAxis2, 0.0);
}
