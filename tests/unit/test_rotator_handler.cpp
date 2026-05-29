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

TEST_F(RotatorHandlerTest, rT_WhenIdle_ReturnsI) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.isMoving = false;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rT", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'I');
}

TEST_F(RotatorHandlerTest, rT_WhenMoving_ReturnsB) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.isMoving = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rT", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'B');
}

// ---------------------------------------------------------------------------
// :rA# — angle as decimal degrees
// ---------------------------------------------------------------------------

TEST_F(RotatorHandlerTest, rA_GetAngle) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.angle = 123.45;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rA", "", reply, &sf, &nr, &err));
    EXPECT_NEAR(std::atof(reply), 123.45, 0.01);
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

TEST_F(RotatorHandlerTest, rF_ResetsAngleToZero) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";
    simState.rotator.angle       = 123.0;
    simState.rotator.targetAngle = 200.0;
    simState.rotator.isMoving    = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rF", "", reply, &sf, &nr, &err));
    EXPECT_NEAR(simState.rotator.angle,       0.0, 0.001);
    EXPECT_NEAR(simState.rotator.targetAngle, 0.0, 0.001);
    EXPECT_FALSE(simState.rotator.isMoving);
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

TEST_F(RotatorHandlerTest, rB_SetAndGetBacklash) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rB", "42", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');

    std::memset(reply, 0, 256);
    sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("rB", "", reply, &sf, &nr, &err));
    EXPECT_EQ(std::atol(reply), 42L);
}

TEST_F(RotatorHandlerTest, rB_NegativeValue_Returns0) {
    if (!cfg.hasRotator) GTEST_SKIP() << "No rotator in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("rB", "-1", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '0');
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
