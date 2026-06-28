// test_rotator_handler.cpp — Unit tests for RotatorHandler.
//
// All tests are gated on cfg.hasRotator.
// Derotator-specific tests additionally gate on cfg.hasDerotator.
//
// Config profiles that exercise these tests:
//   gem_rotator    (hasRotator=true,  hasDerotator=false)
//   fork_altaz     (hasRotator=true,  hasDerotator=true)
//   kitchen_sink   (hasRotator=true,  hasDerotator=false — GEM mount)

#include "SimTestBase.h"
#include "handlers/RotatorHandler.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

class RotatorHandlerTest : public SimTestBase {
protected:
    SimState       simState;
    RotatorHandler handler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);
        // Always register config/state so m_cfg is never null when handle()
        // is called, even for tests that immediately GTEST_SKIP().
        handler.setConfig(&cfg);
        handler.setState(&simState);
        if (!cfg.hasRotator) return;

        // Reasonable defaults for all rotator tests
        simState.rotator.angle        = 0.0;
        simState.rotator.targetAngle  = 0.0;
        simState.rotator.isMoving     = false;
        simState.rotator.limitMin     = 0.0;
        simState.rotator.limitMax     = 360.0;
        simState.rotator.gotoRate     = 3;

        // Mount position for parallactic angle tests
        simState.ra  = 6.0;
        simState.dec = 45.0;
        simState.ha  = 1.0;
        simState.sites[0].latitude = 51.5;
    }
};

// ---------------------------------------------------------------------------
// Guards — no rotator
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, NoRotator_GX98_NotConsumed) {
    if (cfg.hasRotator) GTEST_SKIP() << "Config has rotator";
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("GX", "98", reply, &sf, &nr, &err));
}

// ---------------------------------------------------------------------------
// :GX98# — rotator type
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, GX98_GemRotator_ReturnsR) {
    if (!cfg.hasRotator)    GTEST_SKIP() << "No rotator in config";
    if (cfg.hasDerotator)   GTEST_SKIP() << "Config has derotator";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "98", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'R');
}

TEST_F(RotatorHandlerTest, GX98_AltazmDerotator_ReturnsD) {
    if (!cfg.hasDerotator) GTEST_SKIP() << "Config has no derotator";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "98", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'D');
}

// ---------------------------------------------------------------------------
// :rT# — status
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rT_WhenIdle_StartsWith_S) {
    // Phase 15: firmware returns "S[D][R]n" when stopped; first char is 'S'.
    // Pre-Phase-15 the simulator returned 'I' (incorrect).
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.isMoving     = false;
    simState.rotator.continuousMoveDir = 0;
    simState.rotator.derotEnabled = false;
    simState.rotator.derotReverse = false;
    simState.rotator.gotoRate     = 3;

    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rT", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'S') << "Stopped rotator status must start with 'S'";
    EXPECT_FALSE(nr);
    // With no derot flags, reply should be "S3" (rate char at index 1)
    EXPECT_EQ(reply[1], '3') << "Rate char should be '3' for gotoRate=3";
}

TEST_F(RotatorHandlerTest, rT_WhenMoving_StartsWith_M) {
    // Phase 15: firmware returns "Mn" (M + rate char) when moving.
    // Pre-Phase-15 the simulator returned 'B' (incorrect).
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.isMoving = true;
    simState.rotator.gotoRate = 3;

    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rT", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'M') << "Moving rotator status must start with 'M'";
    EXPECT_FALSE(nr);
}

// ---------------------------------------------------------------------------
// :rA# — angle as decimal degrees
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rA_ActiveCheck_ReturnsNumeric1) {
    // Phase 15: :rA# is a presence/active check — firmware falls through to
    // the default numericReply=true path and returns "1" (CE_NONE success).
    // Pre-Phase-15 the simulator returned the current angle as a decimal string.
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";

    char reply[256] = {}; bool sf = false, nr = true; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rA", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr)  << ":rA# should be a numeric reply";
    EXPECT_EQ(err, CE_NONE) << ":rA# should succeed with CE_NONE -> framer sends 1";
}

// ---------------------------------------------------------------------------
// :rG# — angle in sDDD*MM format
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rG_GetAngle_Format) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.angle = 123.75;  // 123 degrees 45 minutes

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rG", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '+');
    // Expect "+123*45"
    EXPECT_STREQ(reply, "+123*45");
}

TEST_F(RotatorHandlerTest, rG_ZeroAngle_Format) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.angle = 0.0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rG", "", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "+000*00");
}

// ---------------------------------------------------------------------------
// :rS[deg]# — absolute goto
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rS_ValidTarget_Returns1_SetsMoving) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.angle    = 0.0;
    simState.rotator.limitMin = 0.0;
    simState.rotator.limitMax = 360.0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rS", "90.0", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '1');
    EXPECT_TRUE(simState.rotator.isMoving);
    EXPECT_NEAR(simState.rotator.targetAngle, 90.0, 0.001);
}

TEST_F(RotatorHandlerTest, rS_BeyondLimit_Returns0) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.limitMax = 360.0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rS", "400.0", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '0');
    EXPECT_FALSE(simState.rotator.isMoving);
}

TEST_F(RotatorHandlerTest, rS_AlreadyAtTarget_NotMoving) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.angle    = 90.0;
    simState.rotator.limitMax = 360.0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rS", "90.0", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_FALSE(simState.rotator.isMoving);
}

// ---------------------------------------------------------------------------
// :rr[deg]# — relative goto
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rr_RelativeGoto_SetsTarget) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.angle    = 10.0;
    simState.rotator.limitMax = 360.0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rr", "+30.0", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);
    EXPECT_NEAR(simState.rotator.targetAngle, 40.0, 0.001);
    EXPECT_TRUE(simState.rotator.isMoving);
}

// ---------------------------------------------------------------------------
// :rQ# — stop
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rQ_StopsMotion) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.isMoving    = true;
    simState.rotator.angle       = 45.0;
    simState.rotator.targetAngle = 180.0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rQ", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(simState.rotator.isMoving);
    EXPECT_NEAR(simState.rotator.targetAngle, 45.0, 0.001);
}

// ---------------------------------------------------------------------------
// :rF# — reset to 0
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rF_SetsCurrentPositionToHalfTravel) {
    // Phase 15: :rF# is "set current position AS half-travel" (axis3.resetPosition).
    // It does NOT move the rotator — it redefines where we are.
    // Pre-Phase-15 the simulator set angle to 0, which was wrong.
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.limitMin    = 0.0;
    simState.rotator.limitMax    = 360.0;
    simState.rotator.angle       = 123.0;
    simState.rotator.targetAngle = 200.0;
    simState.rotator.isMoving    = true;

    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rF", "", reply, &sf, &nr, &err));
    // Half-travel = (0 + 360) / 2 = 180.0
    EXPECT_NEAR(simState.rotator.angle,       180.0, 0.001)
        << ":rF# should set current angle to half-travel (min+max)/2";
    EXPECT_NEAR(simState.rotator.targetAngle, 180.0, 0.001);
    EXPECT_FALSE(simState.rotator.isMoving);
    EXPECT_TRUE(sf);
}

// ---------------------------------------------------------------------------
// Rate setting :r[1-9]#
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, r5_SetsGotoRate5) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("r5", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.rotator.gotoRate, 5);
    EXPECT_TRUE(sf);
}

// ---------------------------------------------------------------------------
// :rB# / :rB[n]# — backlash
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rb_SetAndGetBacklash) {
    // Firmware: :rb# get steps, :rb[n]# set steps (lowercase b, not B).
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";

    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rb", "42", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr); EXPECT_EQ(reply[0], '1');

    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("rb", "", reply, &sf, &nr, &err));
    EXPECT_EQ(std::atol(reply), 42L);
}

TEST_F(RotatorHandlerTest, rb_NegativeValue_Returns0) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";

    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rb", "-1", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr); EXPECT_EQ(reply[0], '0');
}

// ---------------------------------------------------------------------------
// :r+# / :r-# — derotator enable/disable
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rPlus_EnableDerotator_WhenHasDerotator) {
    if (!cfg.hasDerotator) GTEST_SKIP() << "Config has no derotator";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    simState.rotator.derotEnabled = false;
    ASSERT_TRUE(handler.handle("r+", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(simState.rotator.derotEnabled);
}

TEST_F(RotatorHandlerTest, rPlus_GemRotator_SilentlyIgnored) {
    if (!cfg.hasRotator)  GTEST_SKIP() << "No rotator in config";
    if (cfg.hasDerotator) GTEST_SKIP() << "Config has derotator; test is for GEM/FORK";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    simState.rotator.derotEnabled = false;
    ASSERT_TRUE(handler.handle("r+", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(simState.rotator.derotEnabled);  // unchanged
}

TEST_F(RotatorHandlerTest, rMinus_DisablesDerotator) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.derotEnabled = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("r-", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(simState.rotator.derotEnabled);
}

// ---------------------------------------------------------------------------
// :rR# — reverse toggle
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rR_TogglesReverse) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.derotReverse = false;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rR", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(simState.rotator.derotReverse);

    std::memset(reply, 0, 256);
    sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("rR", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(simState.rotator.derotReverse);
}

// ---------------------------------------------------------------------------
// :rP# — goto parallactic angle (smoke test — just verify it doesn't crash
//         and sets a moving state when angle differs from PA)
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rP_SetsTargetAngle) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";

    // Place mount at a position that gives non-zero PA
    simState.ha  = 2.0;   // 30 degrees west of meridian
    simState.dec = 30.0;
    simState.sites[0].latitude = 51.5;
    simState.rotator.angle = 0.0;
    simState.rotator.limitMin = -180.0;
    simState.rotator.limitMax =  360.0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rP", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);
    // PA at HA=2h, Dec=30°, lat=51.5° is non-trivial;
    // just verify targetAngle was written and is within limits
    EXPECT_GE(simState.rotator.targetAngle, simState.rotator.limitMin);
    EXPECT_LE(simState.rotator.targetAngle, simState.rotator.limitMax);
}

// ---------------------------------------------------------------------------
// :rZ# — sync to zero
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rZ_SyncsAngleToZero) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.angle = 99.0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rZ", "", reply, &sf, &nr, &err));
    EXPECT_NEAR(simState.rotator.angle, 0.0, 0.001);
}

// ---------------------------------------------------------------------------
// Non-rotator command is not consumed
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, NonRotatorCommand_NotConsumed) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    EXPECT_FALSE(handler.handle("GU", "", reply, &sf, &nr, &err));
}

// ---------------------------------------------------------------------------
// Phase 15 — New and corrected rotator commands (audit 4.6)
// ---------------------------------------------------------------------------

// -- :rI# get minimum position -----------------------------------------------

TEST_F(RotatorHandlerTest, Phase15_rI_GetLimitMin) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.limitMin = -10.0;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rI", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(std::atol(reply), -10L) << ":rI# should return rounded limitMin";
}

TEST_F(RotatorHandlerTest, Phase15_rI_ZeroMin) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.limitMin = 0.0;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rI", "", reply, &sf, &nr, &err));
    EXPECT_EQ(std::atol(reply), 0L);
}

// -- :rM# get maximum position -----------------------------------------------

TEST_F(RotatorHandlerTest, Phase15_rM_GetLimitMax) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.limitMax = 360.0;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rM", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(std::atol(reply), 360L) << ":rM# should return rounded limitMax";
}

// -- :rD# get degrees per step ------------------------------------------------

TEST_F(RotatorHandlerTest, Phase15_rD_GetDegreesPerStep) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.stepsPerDegree = 100.0;  // → 0.01000 deg/step
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rD", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    double val = std::atof(reply);
    EXPECT_NEAR(val, 0.01000, 1e-5) << ":rD# should return 1/stepsPerDegree";
}

// -- :rW# get working slew rate ----------------------------------------------

TEST_F(RotatorHandlerTest, Phase15_rW_GetSlewRate_Rate3) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.gotoRate = 3;   // → 1.0 deg/s
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rW", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    // Rate 3 → 1.0 deg/s (table index 3)
    double val = std::atof(reply);
    EXPECT_NEAR(val, 1.0, 0.05) << ":rW# with gotoRate=3 should return ~1.0";
}

// -- :rc# set continuous mode (no-op) ----------------------------------------

TEST_F(RotatorHandlerTest, Phase15_rc_NoopSetsContinuousMode) {
    if (!cfg.hasRotator) GTEST_SKIP();
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rc", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf)  << ":rc# should set suppressFrame (returns nothing)";
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '\0');
    EXPECT_EQ(err, CE_NONE);
}

// -- :r># / :r<# continuous move --------------------------------------------

TEST_F(RotatorHandlerTest, Phase15_rCW_SetsContinuousMoveForward) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.angle   = 90.0;
    simState.rotator.isParked = false;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("r>", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf)  << ":r># should set suppressFrame";
    EXPECT_FALSE(nr);
    EXPECT_EQ(simState.rotator.continuousMoveDir, +1)
        << ":r># should set continuousMoveDir=+1 (CW)";
    EXPECT_TRUE(simState.rotator.isMoving);
    EXPECT_EQ(err, CE_NONE);
}

TEST_F(RotatorHandlerTest, Phase15_rCCW_SetsContinuousMoveReverse) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.angle    = 90.0;
    simState.rotator.isParked = false;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("r<", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);
    EXPECT_EQ(simState.rotator.continuousMoveDir, -1)
        << ":r<# should set continuousMoveDir=-1 (CCW)";
    EXPECT_TRUE(simState.rotator.isMoving);
}

TEST_F(RotatorHandlerTest, Phase15_rQ_ClearsContinuousMove) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.continuousMoveDir = +1;
    simState.rotator.isMoving          = true;
    simState.rotator.angle             = 90.0;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rQ", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.rotator.continuousMoveDir, 0)
        << ":rQ# should clear continuousMoveDir";
    EXPECT_FALSE(simState.rotator.isMoving);
}

TEST_F(RotatorHandlerTest, Phase15_rCW_WhenParked_ReturnsCE_PARKED) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.isParked = true;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("r>", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_PARKED) << ":r># when parked should return CE_PARKED";
    EXPECT_EQ(simState.rotator.continuousMoveDir, 0);
}

// -- :rC# goto half-travel position -----------------------------------------

TEST_F(RotatorHandlerTest, Phase15_rC_GotoHalfTravel) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.limitMin = 0.0;
    simState.rotator.limitMax = 360.0;
    simState.rotator.angle    = 0.0;
    simState.rotator.isParked = false;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rC", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf)  << ":rC# should set suppressFrame";
    EXPECT_FALSE(nr);
    EXPECT_NEAR(simState.rotator.targetAngle, 180.0, 0.001)
        << ":rC# should set target to half-travel (min+max)/2 = 180";
    EXPECT_TRUE(simState.rotator.isMoving)
        << ":rC# should begin motion when not already at target";
    EXPECT_EQ(err, CE_NONE);
}

TEST_F(RotatorHandlerTest, Phase15_rC_WhenParked_ReturnsCE_PARKED) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.isParked = true;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rC", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_PARKED);
    EXPECT_FALSE(simState.rotator.isMoving);
}

TEST_F(RotatorHandlerTest, Phase15_rF_vs_rC_Distinction) {
    // :rF# redefines current position as half-travel (no motion).
    // :rC# moves TO half-travel (motion starts).
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.limitMin = 0.0;
    simState.rotator.limitMax = 360.0;
    simState.rotator.angle    = 50.0;
    simState.rotator.isParked = false;

    // :rF# — should redefine position, not move
    {   char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
        ASSERT_TRUE(handler.handle("rF", "", reply, &sf, &nr, &err));
        EXPECT_NEAR(simState.rotator.angle, 180.0, 0.001)
            << ":rF# sets current pos to half-travel";
        EXPECT_FALSE(simState.rotator.isMoving)
            << ":rF# must NOT start motion"; }

    // Reset and do :rC# — should move to half-travel
    simState.rotator.angle    = 50.0;
    simState.rotator.isMoving = false;
    {   char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
        ASSERT_TRUE(handler.handle("rC", "", reply, &sf, &nr, &err));
        EXPECT_NEAR(simState.rotator.angle, 50.0, 0.001)
            << ":rC# must not change current angle immediately";
        EXPECT_NEAR(simState.rotator.targetAngle, 180.0, 0.001)
            << ":rC# target is half-travel";
        EXPECT_TRUE(simState.rotator.isMoving)
            << ":rC# must start motion"; }
}

// -- :rT# derot flags in status string --------------------------------------

TEST_F(RotatorHandlerTest, Phase15_rT_Stopped_WithDerotEnabled) {
    if (!cfg.hasDerotator) GTEST_SKIP() << "No derotator";
    simState.rotator.isMoving     = false;
    simState.rotator.continuousMoveDir = 0;
    simState.rotator.derotEnabled = true;
    simState.rotator.derotReverse = false;
    simState.rotator.gotoRate     = 3;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rT", "", reply, &sf, &nr, &err));
    // Firmware: "S" + "D" (derot enabled) + rate char → "SD3"
    EXPECT_EQ(reply[0], 'S');
    EXPECT_NE(std::strchr(reply, 'D'), nullptr) << "D flag expected when derot enabled";
}

TEST_F(RotatorHandlerTest, Phase15_rT_ContinuousMove_ShowsMoving) {
    if (!cfg.hasRotator) GTEST_SKIP();
    simState.rotator.isMoving          = false;   // goto not active
    simState.rotator.continuousMoveDir = +1;      // but continuous move is
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("rT", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'M') << "Active continuous move should show M status";
}
