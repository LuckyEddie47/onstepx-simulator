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
    simState.mountState = MountState::TRACKING;
    simState.parkState  = PS_UNPARKED;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hC", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);
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
