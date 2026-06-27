// test_pec_handler.cpp — Unit tests for PecHandler

#include "SimTestBase.h"
#include "handlers/PecHandler.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

class PecHandlerTest : public SimTestBase {
protected:
    SimState   simState;
    PecHandler handler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);
        handler.setConfig(&cfg);
        handler.setState(&simState);

        simState.mountState   = MountState::TRACKING;
        simState.isTracking   = true;
        // Use a concrete worm step count regardless of config
        simState.pecWormSteps = 335;
    }
};

// ---------------------------------------------------------------------------
// Always-available commands
// ---------------------------------------------------------------------------

TEST_F(PecHandlerTest, GXE6_ReturnsStepsPerSiderealSecond) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "E6", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_GT(std::atof(reply), 0.0);
}

TEST_F(PecHandlerTest, GXE7_ReturnsWormSteps) {
    simState.pecWormSteps = 12345;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "E7", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_EQ(std::atol(reply), 12345L);
}

TEST_F(PecHandlerTest, GXE8_ReturnsBufferSize) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "E8", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    long sz = std::atol(reply);
    EXPECT_GT(sz, 0L);
    EXPECT_LE(sz, 720L);
}

TEST_F(PecHandlerTest, VS_MatchesGXE6) {
    char replyGX[256] = {}, replyVS[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    handler.handle("GX", "E6", replyGX, &sf, &nr, &err);
    handler.handle("VS", "",   replyVS, &sf, &nr, &err);
    EXPECT_STREQ(replyGX, replyVS);
}

TEST_F(PecHandlerTest, VW_ReturnsWormStepsAs6Digits) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    simState.pecWormSteps = 42;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("VW", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_STREQ(reply, "000042");
}

TEST_F(PecHandlerTest, QZ_Query_WhenNone_ReturnsI) {
    simState.pecState = PecState::NONE;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("$Q", "Z?", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'I');
}

// ---------------------------------------------------------------------------
// PEC control (hasPec required)
// ---------------------------------------------------------------------------

TEST_F(PecHandlerTest, QZ_Enable_WhenRecorded_SetsReadyPlay) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    simState.pecState    = PecState::NONE;
    simState.pecRecorded = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("$Q", "Z+", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.pecState, PecState::READY_PLAY);
}

TEST_F(PecHandlerTest, QZ_Enable_WhenNotRecorded_ReturnsCE0) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    simState.pecState    = PecState::NONE;
    simState.pecRecorded = false;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("$Q", "Z+", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_0);
    EXPECT_EQ(simState.pecState, PecState::NONE);
}

TEST_F(PecHandlerTest, QZ_Disable_SetsPecNone) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    simState.pecState = PecState::PLAYING;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("$Q", "Z-", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.pecState, PecState::NONE);
}

TEST_F(PecHandlerTest, QZ_ReadyRecord_WhenTracking_SetsReadyRecord) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    simState.pecState   = PecState::NONE;
    simState.isTracking = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("$Q", "Z/", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.pecState, PecState::READY_RECORD);
}

TEST_F(PecHandlerTest, QZZ_ClearsBuffer) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    simState.pecBuffer[0]  = 42;
    simState.pecBuffer[10] = -5;
    simState.pecRecorded   = true;
    simState.pecState      = PecState::PLAYING;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("$Q", "ZZ", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.pecBuffer[0],  0);
    EXPECT_EQ(simState.pecBuffer[10], 0);
    EXPECT_FALSE(simState.pecRecorded);
    EXPECT_EQ(simState.pecState, PecState::NONE);
}

TEST_F(PecHandlerTest, QZ_WriteNV_MarksRecorded) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    simState.pecRecorded = false;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("$Q", "Z!", reply, &sf, &nr, &err));
    EXPECT_TRUE(simState.pecRecorded);
}

// ---------------------------------------------------------------------------
// WR — PEC table write / shift
// ---------------------------------------------------------------------------

TEST_F(PecHandlerTest, WR_WriteEntry) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("WR", "5,+42", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.pecBuffer[5], 42);
    EXPECT_TRUE(simState.pecRecorded);
}

TEST_F(PecHandlerTest, WR_WriteEntry_OutOfRange_Rejected) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("WR", "5,+200", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_PARAM_RANGE);
}

TEST_F(PecHandlerTest, WRPlus_ShiftsForward) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("WR", "+", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
}

// ---------------------------------------------------------------------------
// VR — PEC table read
// ---------------------------------------------------------------------------

TEST_F(PecHandlerTest, VR_ReadEntry) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    simState.pecBuffer[3] = -7;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("VR", "3", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
    EXPECT_STREQ(reply, "-007");
}

// ---------------------------------------------------------------------------
// SXE7 — Set worm steps
// ---------------------------------------------------------------------------

TEST_F(PecHandlerTest, SXE7_SetsWormSteps) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", "E7,9999", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_EQ(simState.pecWormSteps, 9999L);
}

// ---------------------------------------------------------------------------
// hasPec = false: non-gated commands still work, gated ones do not
// ---------------------------------------------------------------------------

TEST_F(PecHandlerTest, NoPec_QZQuery_StillReturnsI) {
    if (cfg.hasPec) GTEST_SKIP() << "Config has PEC — use gem_full for noPec variant";
    simState.pecState = PecState::NONE;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("$Q", "Z?", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'I');
}

TEST_F(PecHandlerTest, NoPec_GXE6_StillWorks) {
    if (cfg.hasPec) GTEST_SKIP() << "Config has PEC";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "E6", reply, &sf, &nr, &err));
    EXPECT_GT(std::atof(reply), 0.0);
}

TEST_F(PecHandlerTest, NoPec_GX91_NotConsumed) {
    if (cfg.hasPec) GTEST_SKIP() << "Config has PEC";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    EXPECT_FALSE(handler.handle("GX", "91", reply, &sf, &nr, &err));
}

// ---------------------------------------------------------------------------
// Phase 12B — PEC no-reply commands set suppressFrame=true
// ---------------------------------------------------------------------------

TEST_F(PecHandlerTest, Phase12B_QZMinus_SetsSupressFrame) {
    if (!cfg.hasPec) GTEST_SKIP() << "No PEC";
    char reply[256] = {}; bool sf = false, nr = true; CommandError err = CE_NONE;
    // $QZ- disables PEC; firmware returns nothing
    ASSERT_TRUE(handler.handle("$Q", "Z-", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf)   << "$QZ-# should set suppressFrame (no reply)";
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '\0');
    EXPECT_EQ(err, CE_NONE);
}

TEST_F(PecHandlerTest, Phase12B_QZZ_SetsSupressFrame) {
    if (!cfg.hasPec) GTEST_SKIP() << "No PEC";
    char reply[256] = {}; bool sf = false, nr = true; CommandError err = CE_NONE;
    // $QZZ clears buffer; firmware returns nothing
    ASSERT_TRUE(handler.handle("$Q", "ZZ", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf)   << "$QZZ# should set suppressFrame (no reply)";
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '\0');
    EXPECT_EQ(err, CE_NONE);
}

TEST_F(PecHandlerTest, Phase12B_QZBang_SetsSupressFrame) {
    if (!cfg.hasPec) GTEST_SKIP() << "No PEC";
    char reply[256] = {}; bool sf = false, nr = true; CommandError err = CE_NONE;
    // $QZ! writes to NV; firmware returns nothing
    ASSERT_TRUE(handler.handle("$Q", "Z!", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf)   << "$QZ!# should set suppressFrame (no reply)";
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '\0');
    EXPECT_EQ(err, CE_NONE);
}

TEST_F(PecHandlerTest, Phase12B_WREntry_Success_SetsSupressFrame) {
    if (!cfg.hasPec) GTEST_SKIP() << "No PEC";
    // :WR[0,-1]# — write entry 0, value -1; firmware returns nothing on success
    char reply[256] = {}; bool sf = false, nr = true; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("WR", "0,-1", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf)   << "WR[n,sn]# success should set suppressFrame (no reply)";
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '\0');
    EXPECT_EQ(err, CE_NONE);
}
