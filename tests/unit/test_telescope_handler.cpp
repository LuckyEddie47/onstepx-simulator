// test_telescope_handler.cpp — Unit tests for TelescopeHandler.
//
// :ERESET# is not tested here (it calls exit()) — that is verified by
// the integration test harness which monitors the process lifecycle.
// All other commands are unconditional (no config gate).

#include "SimTestBase.h"
#include "handlers/TelescopeHandler.h"

#include <cstring>

class TelescopeHandlerTest : public SimTestBase {
protected:
    SimState          simState;
    TelescopeHandler  handler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);
        // Always register — DEC-020 rule
        handler.setConfig(&cfg);
        handler.setState(&simState);
    }
};

// ---------------------------------------------------------------------------
// :B+# / :B-# — reticle brightness; no reply
// ---------------------------------------------------------------------------

TEST_F(TelescopeHandlerTest, BPlus_HandledNoReply) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("B+", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf)  << "B+# should suppress frame";
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '\0');
}

TEST_F(TelescopeHandlerTest, BMinus_HandledNoReply) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("B-", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '\0');
}

TEST_F(TelescopeHandlerTest, BX_NotConsumed) {
    // :BX# is not a valid brightness command
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("BX", "", reply, &sf, &nr, &err));
}

// ---------------------------------------------------------------------------
// :EC[s]# — echo; no reply
// ---------------------------------------------------------------------------

TEST_F(TelescopeHandlerTest, EC_HandledNoReply) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("EC", "hello world", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);
    EXPECT_FALSE(nr);
    EXPECT_EQ(reply[0], '\0');
}

TEST_F(TelescopeHandlerTest, EC_EmptyParam_Handled) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("EC", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);
}

// ---------------------------------------------------------------------------
// :ENVRESET# — NV reset message
// Framer splits ":ENVRESET#" as cmd="EN", param="VRESET"
// ---------------------------------------------------------------------------

TEST_F(TelescopeHandlerTest, ENVRESET_ReturnsMessage) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("EN", "VRESET", reply, &sf, &nr, &err));
    EXPECT_FALSE(sf);
    EXPECT_FALSE(nr);
    EXPECT_NE(std::strstr(reply, "NV memory"), nullptr)
        << "ENVRESET reply should mention NV memory: " << reply;
    EXPECT_NE(std::strstr(reply, "next boot"), nullptr)
        << "ENVRESET reply should mention next boot: " << reply;
}

// ---------------------------------------------------------------------------
// :ESPFLASH# — not simulated; returns CE_CMD_UNKNOWN
// Framer splits ":ESPFLASH#" as cmd="ES", param="PFLASH"
// ---------------------------------------------------------------------------

TEST_F(TelescopeHandlerTest, ESPFLASH_ReturnsCmdUnknown) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("ES", "PFLASH", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_CMD_UNKNOWN);
}

// ---------------------------------------------------------------------------
// Non-telescope commands not consumed
// ---------------------------------------------------------------------------

TEST_F(TelescopeHandlerTest, GU_NotConsumed) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("GU", "", reply, &sf, &nr, &err));
}

TEST_F(TelescopeHandlerTest, FA_NotConsumed) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("FA", "", reply, &sf, &nr, &err));
}
