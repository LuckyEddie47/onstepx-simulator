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
    // Phase 14: firmware requires parkPositionSet, trusted, axesEnabled,
    // no goto/guide in progress. Set all preconditions correctly.
    simState.mountState      = MountState::TRACKING;
    simState.isTracking      = true;
    simState.parkState       = PS_UNPARKED;
    simState.parkPositionSet = true;
    simState.startupTrusted  = true;
    simState.axesEnabled     = true;
    simState.dateReady       = true;
    simState.timeReady       = true;

    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
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

// ---------------------------------------------------------------------------
// Phase 14 — Park precondition chain (audit 2.3)
// Firmware reference: Park::request() in Park.cpp lines 88-112.
// ---------------------------------------------------------------------------

// Helper: puts state into the minimum valid condition for :hP# to succeed.
static void setParkReadyState(SimState& s) {
    s.parkPositionSet = true;
    s.parkState       = PS_UNPARKED;
    s.startupTrusted  = true;
    s.axesEnabled     = true;
    s.gotoState       = GotoState::NONE;
    s.guideState      = GuideState::NONE;
}

TEST_F(ParkHandlerTest, Phase14_hP_NoParkPosition_RejectsCE_NO_PARK_POSITION_SET) {
    // Firmware line 88: if (!settings.saved) return CE_NO_PARK_POSITION_SET
    setParkReadyState(simState);
    simState.parkPositionSet = false;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hP", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NO_PARK_POSITION_SET)
        << "hP# without a saved park position must return CE_NO_PARK_POSITION_SET";
    EXPECT_NE(simState.parkState, PS_PARKING) << "Park must not begin";
}

TEST_F(ParkHandlerTest, Phase14_hP_ParkFailed_RejectsCE_PARK_FAILED) {
    // Firmware line 91: if (state == PS_PARK_FAILED) return CE_PARK_FAILED
    setParkReadyState(simState);
    simState.parkState = PS_PARK_FAILED;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hP", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_PARK_FAILED);
}

TEST_F(ParkHandlerTest, Phase14_hP_NotTrusted_RejectsUnspecified) {
    // Firmware line 93: if (!startupAuthority.trusted()) return CE_SLEW_ERR_UNSPECIFIED
    setParkReadyState(simState);
    simState.startupTrusted = false;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hP", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_SLEW_ERR_UNSPECIFIED)
        << "hP# without startup authority must return CE_SLEW_ERR_UNSPECIFIED";
}

TEST_F(ParkHandlerTest, Phase14_hP_AxesDisabled_RejectsInStandby) {
    // Firmware line 96: if (!mount.isEnabled()) return CE_SLEW_ERR_IN_STANDBY
    setParkReadyState(simState);
    simState.axesEnabled = false;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hP", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_SLEW_ERR_IN_STANDBY)
        << "hP# with axes disabled must return CE_SLEW_ERR_IN_STANDBY";
}

TEST_F(ParkHandlerTest, Phase14_hP_GotoInProgress_RejectsMountInMotion) {
    // Firmware line 97: if (goTo.state != GS_NONE) return CE_SLEW_IN_MOTION
    setParkReadyState(simState);
    simState.gotoState = GotoState::GOTO;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hP", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_MOUNT_IN_MOTION)
        << "hP# during a goto must return CE_MOUNT_IN_MOTION";
}

TEST_F(ParkHandlerTest, Phase14_hP_GuideInProgress_RejectsMountInMotion) {
    // Firmware line 98: if (guide.state != GU_NONE) return CE_SLEW_IN_MOTION
    setParkReadyState(simState);
    simState.guideState = GuideState::ACTIVE;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hP", "", reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_MOUNT_IN_MOTION)
        << "hP# during a guide must return CE_MOUNT_IN_MOTION";
}

TEST_F(ParkHandlerTest, Phase14_hP_AlreadyParked_ReturnsNone) {
    // Firmware line 89: if (state == PS_PARKED) return CE_NONE (success, no-op)
    setParkReadyState(simState);
    simState.parkState = PS_PARKED;
    char reply[256] = {}; bool sf = false, nr = false; CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("hP", "", reply, &sf, &nr, &err));
    // Firmware maps CE_NONE to CE_1 (success) via Park.command.cpp:
    //   if (e == CE_NONE) *commandError = CE_1;
    // ParkHandler does the same mapping.
    EXPECT_EQ(err, CE_1)
        << "hP# when already parked should return CE_1 (success, no-op)";
}
