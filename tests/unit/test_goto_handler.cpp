// test_goto_handler.cpp — Unit tests for GotoHandler

#include "SimTestBase.h"
#include "handlers/GotoHandler.h"
#include "state/MountStateMachine.h"

#include <cstring>

class GotoHandlerTest : public SimTestBase {
protected:
    SimState          simState;
    MountStateMachine msm;
    GotoHandler       handler;
    SimClock          simClock;

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

// ---------------------------------------------------------------------------
// Target coordinate set and get
// ---------------------------------------------------------------------------

TEST_F(GotoHandlerTest, SetAndGetTargetRA) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Sr", "06:30:00", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);

    std::memset(reply, 0, sizeof(reply));
    nr = false;
    ASSERT_TRUE(handler.handle("Gr", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_STREQ(reply, "06:30:00");
    EXPECT_NEAR(simState.targetRA, 6.5, 0.001);
    EXPECT_TRUE(simState.targetRASet);
}

TEST_F(GotoHandlerTest, SetAndGetTargetDec) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Sd", "+45*30:00", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_TRUE(simState.targetDecSet);
    EXPECT_NEAR(simState.targetDec, 45.5, 0.01);

    std::memset(reply, 0, sizeof(reply));
    ASSERT_TRUE(handler.handle("Gd", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '+');
}

TEST_F(GotoHandlerTest, SetTargetRA_InvalidFormat_ReturnsParamRange) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("Sr", "BADDATA", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_PARAM_RANGE);
}

// Phase 9: GotoHandler::formatHMS/formatDMS previously had their own
// independent rounding-carry bug (the same class as MountHandler's, fixed
// by migrating both to the shared coordformat:: utility). These values
// were confirmed during the audit to previously produce impossible output
// like "12:59:60.0000".
TEST_F(GotoHandlerTest, GrH_RoundingCarriesIntoHour) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    simState.targetRA = 12.999999999;
    simState.targetRASet = true;
    ASSERT_TRUE(handler.handle("Gr", "H", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "13:00:00.0000");
}

TEST_F(GotoHandlerTest, GdH_RoundingCarriesAt90) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    simState.targetDec = 89.9999999999;
    simState.targetDecSet = true;
    ASSERT_TRUE(handler.handle("Gd", "H", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "+90*00:00.000");
}

// ---------------------------------------------------------------------------
// Alignment
// ---------------------------------------------------------------------------

TEST_F(GotoHandlerTest, AlignStatus_NoAlignInProgress) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("A?", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    // reply[0] = max stars ('3'), reply[1] = done count, reply[2] = expected
    EXPECT_EQ(reply[0], '3');
}

TEST_F(GotoHandlerTest, AlignSequence_OneStarSuccess) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("A1", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.alignExpected, 1);
    EXPECT_FALSE(simState.alignDone);

    err = CE_NONE;
    ASSERT_TRUE(handler.handle("A+", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_TRUE(simState.alignDone);
    EXPECT_TRUE(simState.startupTrusted);
}

TEST_F(GotoHandlerTest, AlignAccept_WhenNotActive_ReturnsError) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("A+", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_ALIGN_NOT_ACTIVE);
}

// ---------------------------------------------------------------------------
// Goto
// ---------------------------------------------------------------------------

TEST_F(GotoHandlerTest, MS_InStandby_ReturnsError) {
    simState.mountState  = MountState::STANDBY;
    simState.targetRASet = true;
    simState.targetDecSet = true;
    simState.targetRA = 6.0;
    simState.targetDec = 45.0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("MS", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_TRUE(sf);
    EXPECT_NE(reply[0], '0');
}

TEST_F(GotoHandlerTest, MS_WhileTracking_WithValidTarget_SetsSlewing) {
    simState.mountState   = MountState::TRACKING;
    simState.isTracking   = true;
    simState.targetRASet  = true;
    simState.targetDecSet = true;
    simState.targetRA     = 6.0;
    simState.targetDec    = 45.0;
    simState.dateReady    = true;
    simState.timeReady    = true;
    simState.parkState    = PS_UNPARKED;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("MS", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_TRUE(sf);
    EXPECT_EQ(reply[0], '0');
    EXPECT_EQ(simState.mountState, MountState::SLEWING_GOTO);
}

TEST_F(GotoHandlerTest, MS_WhenParked_ReturnsError) {
    simState.mountState = MountState::PARKED;
    simState.parkState  = PS_PARKED;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("MS", "", reply, &sf, &nr, &err));
    EXPECT_NE(reply[0], '0');
}

// ---------------------------------------------------------------------------
// Sync
// ---------------------------------------------------------------------------

TEST_F(GotoHandlerTest, CM_Sync_WhenTracking_ReturnsNA) {
    simState.mountState   = MountState::TRACKING;
    simState.isTracking   = true;
    simState.targetRASet  = true;
    simState.targetDecSet = true;
    simState.targetRA     = simState.ra;
    simState.targetDec    = simState.dec;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("CM", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_STREQ(reply, "N/A");
}

TEST_F(GotoHandlerTest, CS_Sync_ReturnsNothing) {
    simState.mountState   = MountState::TRACKING;
    simState.targetRASet  = true;
    simState.targetDecSet = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("CS", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '\0');
}

// ---------------------------------------------------------------------------
// Distance bars
// ---------------------------------------------------------------------------

TEST_F(GotoHandlerTest, D_WhenSlewing_ReturnsBar) {
    simState.mountState = MountState::SLEWING_GOTO;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("D", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(static_cast<uint8_t>(reply[0]), 127u);
    EXPECT_FALSE(sf);
}

TEST_F(GotoHandlerTest, D_WhenTracking_ReturnsHash) {
    simState.mountState = MountState::TRACKING;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("D", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '#');
    EXPECT_TRUE(sf);
}

// ---------------------------------------------------------------------------
// GX9 settings
// ---------------------------------------------------------------------------

TEST_F(GotoHandlerTest, GX94_PierSide) {
    simState.pierSide = PIER_SIDE_EAST;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "94", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '1'); // PIER_SIDE_EAST = 1
}

TEST_F(GotoHandlerTest, GX95_AutoFlip_RoundTrip) {
    if (!cfg.isEquatorial() || !cfg.hasGoto) GTEST_SKIP() << "Meridian flip not in this config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", "95,1", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_TRUE(simState.autoFlipEnabled);

    std::memset(reply, 0, sizeof(reply));
    ASSERT_TRUE(handler.handle("GX", "95", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
}

TEST_F(GotoHandlerTest, GX96_PierSide_RoundTrip) {
    if (!cfg.isEquatorial() || !cfg.hasGoto) GTEST_SKIP() << "Pier side not applicable";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", "96,W", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.preferredPierSide, WEST);

    std::memset(reply, 0, sizeof(reply));
    ASSERT_TRUE(handler.handle("GX", "96", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'W');
}
