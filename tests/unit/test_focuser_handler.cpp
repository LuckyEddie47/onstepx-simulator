// test_focuser_handler.cpp — Unit tests for FocuserHandler.
//
// All tests are gated on cfg.numFocusers > 0.
// Multi-focuser tests additionally gate on cfg.numFocusers >= 2.
//
// Config profiles that exercise these tests:
//   gem_focuser1       (numFocusers=1)
//   gem_focuser_multi  (numFocusers=3)
//   kitchen_sink       (numFocusers=2)

#include "SimTestBase.h"
#include "handlers/FocuserHandler.h"

#include <cstdlib>
#include <cstring>

class FocuserHandlerTest : public SimTestBase {
protected:
    SimState      simState;
    FocuserHandler handler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);
        // Always register config/state so m_cfg is never null when handle()
        // is called, even for tests that immediately GTEST_SKIP().
        handler.setConfig(&cfg);
        handler.setState(&simState);
        if (cfg.numFocusers == 0) return;
    }

    // Convenience: dispatch a command and return true if handled
    bool dispatch(const char* cmd, const char* param,
                  char* reply, bool* sf, bool* nr, CommandError* err) {
        return handler.handle(cmd, param, reply, sf, nr, err);
    }
};

// ---------------------------------------------------------------------------
// Guards — no focuser
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, NoFocuser_FA_NotConsumed) {
    if (cfg.numFocusers > 0) GTEST_SKIP() << "Config has focuser(s)";
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("FA", "", reply, &sf, &nr, &err));
}

// ---------------------------------------------------------------------------
// :FA# / :FA[n]# — focuser selection
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FA_GetActive_ReturnsOne) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.activeFocuser = 0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FA", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);               // no '#'
    EXPECT_EQ(reply[0], '1');      // 1-based
}

TEST_F(FocuserHandlerTest, FA_SelectPresent_Returns1) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FA", "1", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);               // single char, no '#'
    EXPECT_EQ(reply[0], '1');
    EXPECT_EQ(simState.activeFocuser, 0);
}

TEST_F(FocuserHandlerTest, FA_SelectAbsent_Returns0) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    // Select slot beyond numFocusers
    int absentSlot = cfg.numFocusers + 1;
    if (absentSlot > 6) GTEST_SKIP() << "Cannot select beyond slot 6";

    char param[4] = { static_cast<char>('0' + absentSlot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FA", param, reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '0');
}

TEST_F(FocuserHandlerTest, FA_Select_ChangesActive) {
    if (cfg.numFocusers < 2) GTEST_SKIP() << "Need >= 2 focusers";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    // Select focuser 2
    ASSERT_TRUE(handler.handle("FA", "2", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_EQ(simState.activeFocuser, 1);

    // :FA# should now return '2'
    std::memset(reply, 0, 256);
    sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("FA", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '2');
}

// ---------------------------------------------------------------------------
// :Fa# — primary focuser present
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, Fa_ReturnsOne) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Fa", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '1');
}

// ---------------------------------------------------------------------------
// :FT# — status
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FT_StoppedReturnsS) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].isMoving  = false;
    simState.focuser[0].moveRate  = 2;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FT", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'S');
    EXPECT_EQ(reply[1], '2');
}

TEST_F(FocuserHandlerTest, FT_MovingReturnsM) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].isMoving  = true;
    simState.focuser[0].moveRate  = 3;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FT", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], 'M');
    EXPECT_EQ(reply[1], '3');
}

// ---------------------------------------------------------------------------
// :FG# / :Fg# — get position
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FG_GetPositionMicrons) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    // stepsPerMicron=0.5 -> 100 steps = 200 µm
    simState.focuser[0].stepsPerMicron = 0.5f;
    simState.focuser[0].positionSteps  = 100;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FG", "", reply, &sf, &nr, &err));
    EXPECT_EQ(std::atol(reply), 200L);
}

TEST_F(FocuserHandlerTest, Fg_GetPositionSteps) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].positionSteps = 42;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Fg", "", reply, &sf, &nr, &err));
    EXPECT_EQ(std::atol(reply), 42L);
}

// ---------------------------------------------------------------------------
// :FI# / :FM# — min / max position
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FI_GetMinMicrons) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].limitMinSteps  = 0;
    simState.focuser[0].stepsPerMicron = 0.5f;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FI", "", reply, &sf, &nr, &err));
    EXPECT_EQ(std::atol(reply), 0L);
}

TEST_F(FocuserHandlerTest, FM_GetMaxMicrons) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    // gem_focuser1: AXIS4_LIMIT_MAX=50 (mm) -> 50*1000*0.5 = 25000 steps -> 50000 µm
    simState.focuser[0].limitMaxSteps  = 25000;
    simState.focuser[0].stepsPerMicron = 0.5f;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FM", "", reply, &sf, &nr, &err));
    EXPECT_EQ(std::atol(reply), 50000L);
}

// ---------------------------------------------------------------------------
// :FS# — absolute goto microns
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FS_AbsGoto_ValidTarget_Returns1) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].stepsPerMicron = 0.5f;
    simState.focuser[0].limitMinSteps  = 0;
    simState.focuser[0].limitMaxSteps  = 25000;
    simState.focuser[0].positionSteps  = 0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FS", "10000", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '1');
    EXPECT_TRUE(simState.focuser[0].isMoving);
    EXPECT_EQ(simState.focuser[0].targetSteps, 5000L);  // 10000µm * 0.5 = 5000 steps
}

TEST_F(FocuserHandlerTest, FS_AbsGoto_BeyondMax_Returns0) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].stepsPerMicron = 0.5f;
    simState.focuser[0].limitMinSteps  = 0;
    simState.focuser[0].limitMaxSteps  = 1000;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FS", "99999999", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '0');
    EXPECT_FALSE(simState.focuser[0].isMoving);
}

// ---------------------------------------------------------------------------
// :FQ# — stop
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FQ_StopsMotion) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].isMoving    = true;
    simState.focuser[0].positionSteps = 100;
    simState.focuser[0].targetSteps   = 500;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FQ", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(simState.focuser[0].isMoving);
    EXPECT_EQ(simState.focuser[0].targetSteps, simState.focuser[0].positionSteps);
}

// ---------------------------------------------------------------------------
// :FZ# — set zero
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FZ_SetsPositionZero) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].positionSteps = 999;
    simState.focuser[0].targetSteps   = 500;
    simState.focuser[0].isMoving      = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FZ", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.focuser[0].positionSteps, 0L);
    EXPECT_EQ(simState.focuser[0].targetSteps,   0L);
    EXPECT_FALSE(simState.focuser[0].isMoving);
}

// ---------------------------------------------------------------------------
// Rate commands
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, F2_SetsMoveRate2) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("F2", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.focuser[0].moveRate, 2);
    EXPECT_TRUE(sf);
}

TEST_F(FocuserHandlerTest, F7_SetsGotoRate3) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    // :F7# -> gotoRate preset 7 -> internal rate = 7-4 = 3
    ASSERT_TRUE(handler.handle("F7", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.focuser[0].gotoRate, 3);
    EXPECT_TRUE(sf);
}

// ---------------------------------------------------------------------------
// :FW# — working rate
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FW_ReturnsNonZero) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].gotoRate = 3;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FW", "", reply, &sf, &nr, &err));
    EXPECT_GT(std::atof(reply), 0.0);
}

// ---------------------------------------------------------------------------
// Backlash
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FB_SetAndGetBacklashMicrons) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].stepsPerMicron = 0.5f;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    // Set 100 µm backlash
    ASSERT_TRUE(handler.handle("FB", "100", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');

    // Read back
    std::memset(reply, 0, 256);
    sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("FB", "", reply, &sf, &nr, &err));
    EXPECT_EQ(std::atol(reply), 100L);
}

// ---------------------------------------------------------------------------
// TCF
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FC_SetAndGetCoef) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FC", "1.23456", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');

    std::memset(reply, 0, 256);
    sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("FC", "", reply, &sf, &nr, &err));
    EXPECT_NEAR(std::atof(reply), 1.23456, 0.0001);
}

TEST_F(FocuserHandlerTest, Fc_EnableTcf) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].tcfEnabled = false;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Fc", "1", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_TRUE(simState.focuser[0].tcfEnabled);
}

TEST_F(FocuserHandlerTest, Fc_GetEnabled_CE1WhenEnabled) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].tcfEnabled = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Fc", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_1);
}

// ---------------------------------------------------------------------------
// Multi-focuser: per-slot operation via :Fn[cmd]#
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, MultiSlot_F2G_ReadsSlot2Position) {
    if (cfg.numFocusers < 2) GTEST_SKIP() << "Need >= 2 focusers";
    simState.focuser[0].positionSteps  = 10;
    simState.focuser[0].stepsPerMicron = 0.5f;
    simState.focuser[1].positionSteps  = 200;
    simState.focuser[1].stepsPerMicron = 0.5f;
    simState.activeFocuser = 0;

    // :F2G# should read focuser 2 (slot 1), not the active focuser (slot 0)
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("F2", "G", reply, &sf, &nr, &err));
    // 200 steps * (1/0.5) = 400 µm
    EXPECT_EQ(std::atol(reply), 400L);
}

TEST_F(FocuserHandlerTest, MultiSlot_ActiveFocuserUnchangedByDirectSlot) {
    if (cfg.numFocusers < 2) GTEST_SKIP() << "Need >= 2 focusers";
    simState.activeFocuser = 0;

    // Operating on slot 2 via :F2[cmd]# must not change activeFocuser
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    handler.handle("F2", "T", reply, &sf, &nr, &err);
    EXPECT_EQ(simState.activeFocuser, 0);
}

// ---------------------------------------------------------------------------
// :FH# and :Fh# — home position
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, FH_SetHomePosition) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].positionSteps = 300;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("FH", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.focuser[0].homePositionSteps, 300L);
}

TEST_F(FocuserHandlerTest, Fh_GotoHome) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";
    simState.focuser[0].homePositionSteps = 150;
    simState.focuser[0].positionSteps     = 0;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Fh", "", reply, &sf, &nr, &err));
    EXPECT_EQ(simState.focuser[0].targetSteps, 150L);
    EXPECT_TRUE(simState.focuser[0].isMoving);
}

// ---------------------------------------------------------------------------
// Non-F command is not consumed
// ---------------------------------------------------------------------------

TEST_F(FocuserHandlerTest, NonFCommand_NotConsumed) {
    if (cfg.numFocusers == 0) GTEST_SKIP() << "No focusers in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    EXPECT_FALSE(handler.handle("GU", "", reply, &sf, &nr, &err));
}
