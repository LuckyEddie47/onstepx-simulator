// test_park_handler.cpp — Unit tests for ParkHandler

#include "SimTestBase.h"
#include "handlers/ParkHandler.h"
#include "state/MountStateMachine.h"
#include "state/SimClock.h"

#include <cstring>

class ParkHandlerTest : public SimTestBase {
protected:
    SimState          simState;
    SimClock          simClock;
    MountStateMachine msm;
    ParkHandler       handler;

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

TEST_F(ParkHandlerTest, hQ_SetsParkPosition) {
    simState.ra  = 3.0;
    simState.dec = 30.0;
    simState.mountState = MountState::TRACKING;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hQ", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    EXPECT_TRUE(simState.parkPositionSet);
    EXPECT_NEAR(simState.parkRA,  3.0,  0.001);
    EXPECT_NEAR(simState.parkDec, 30.0, 0.001);
}

TEST_F(ParkHandlerTest, hP_InitiatesParking) {
    simState.mountState     = MountState::TRACKING;
    simState.isTracking     = true;
    simState.parkState      = PS_UNPARKED;
    simState.parkPositionSet = true;
    simState.dateReady      = true;
    simState.timeReady      = true;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hP", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_1);
    EXPECT_EQ(simState.mountState, MountState::PARKING);
    EXPECT_EQ(simState.parkState,  PS_PARKING);
}

TEST_F(ParkHandlerTest, hP_WhenAlreadyParked_Fails) {
    simState.mountState = MountState::PARKED;
    simState.parkState  = PS_PARKED;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hP", "", reply, &sf, &nr, &err));
    EXPECT_NE(err, CE_1);
    EXPECT_NE(err, CE_NONE);
}

TEST_F(ParkHandlerTest, hR_UnparksWhenParked) {
    simState.mountState = MountState::PARKED;
    simState.parkState  = PS_PARKED;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hR", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_1);
    EXPECT_EQ(simState.mountState, MountState::TRACKING);
    EXPECT_EQ(simState.parkState,  PS_UNPARKED);
}

TEST_F(ParkHandlerTest, hR_WhenAlreadyUnparked_Succeeds) {
    // The firmware's restore() succeeds even when already unparked —
    // it simply re-enters tracking. So CE_1 is the correct result.
    simState.mountState = MountState::TRACKING;
    simState.parkState  = PS_UNPARKED;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("hR", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_1);
}

TEST_F(ParkHandlerTest, UnknownHCommand_NotConsumed) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("hX", "", reply, &sf, &nr, &err));
}

TEST_F(ParkHandlerTest, NonHCommand_NotConsumed) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("GU", "", reply, &sf, &nr, &err));
}
