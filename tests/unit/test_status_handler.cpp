// test_status_handler.cpp — StatusHandler unit tests.
//
// Config-driven per DEC-001. Tests requiring mount, PEC, or PPS skip
// when those features are absent from the loaded config profile.

#include <gtest/gtest.h>
#include "SimTestBase.h"

#include "handlers/StatusHandler.h"

#include <cstring>
#include <cstdint>

class StatusHandlerTest : public SimTestBase {
protected:
    StatusHandler handler;
    SimState      state;

    char         reply[256];
    bool         suppressFrame;
    bool         numericReply;
    CommandError error;

    void SetUp() override {
        state.init(cfg);
        handler.setConfig(&cfg);
        handler.setState(&state);
    }

    bool dispatch(const char* cmd, const char* param) {
        std::memset(reply, 0, sizeof(reply));
        suppressFrame = false;
        numericReply  = false;
        error         = CE_NONE;
        return handler.handle(cmd, param, reply, &suppressFrame, &numericReply, &error);
    }
};

// ---------------------------------------------------------------------------
// :GU# — ASCII status string
// ---------------------------------------------------------------------------

TEST_F(StatusHandlerTest, GU_Handled) {
    EXPECT_TRUE(dispatch("GU", ""));
}

TEST_F(StatusHandlerTest, GU_NonEmpty) {
    dispatch("GU", "");
    EXPECT_GT(std::strlen(reply), 0u);
}

TEST_F(StatusHandlerTest, GU_NotNumericReply) {
    dispatch("GU", "");
    EXPECT_FALSE(numericReply);
}

TEST_F(StatusHandlerTest, GU_NotSuppressFrame) {
    dispatch("GU", "");
    EXPECT_FALSE(suppressFrame);
}

TEST_F(StatusHandlerTest, GU_UnparkedStateContainsLowerP) {
    state.parkState = PS_UNPARKED;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'p'), nullptr)
        << "Unparked should produce 'p' in GU string";
}

TEST_F(StatusHandlerTest, GU_ParkedStateContainsUpperP) {
    state.parkState = PS_PARKED;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'P'), nullptr)
        << "Parked should produce 'P' in GU string";
    EXPECT_EQ(std::strchr(reply, 'p'), nullptr)
        << "Parked should NOT produce lowercase 'p'";
}

TEST_F(StatusHandlerTest, GU_ParkingStateContainsI) {
    state.parkState = PS_PARKING;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'I'), nullptr)
        << "Parking should produce 'I' in GU string";
}

TEST_F(StatusHandlerTest, GU_ParkFailedStateContainsF) {
    state.parkState = PS_PARK_FAILED;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'F'), nullptr)
        << "Park failed should produce 'F' in GU string";
}

TEST_F(StatusHandlerTest, GU_NotTrackingContainsN) {
    state.isTracking = false;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'n'), nullptr)
        << "Not tracking should produce 'n'";
}

TEST_F(StatusHandlerTest, GU_TrackingDoesNotContainN) {
    state.isTracking = true;
    dispatch("GU", "");
    EXPECT_EQ(std::strchr(reply, 'n'), nullptr)
        << "Tracking should NOT produce 'n'";
}

TEST_F(StatusHandlerTest, GU_AtHomeContainsH) {
    state.isAtHome = true;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'H'), nullptr)
        << "At home should produce 'H'";
}

TEST_F(StatusHandlerTest, GU_SoundEnabledContainsZ) {
    state.soundEnabled = true;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'z'), nullptr)
        << "Sound enabled should produce 'z'";
}

TEST_F(StatusHandlerTest, GU_SoundDisabledNoZ) {
    state.soundEnabled = false;
    dispatch("GU", "");
    EXPECT_EQ(std::strchr(reply, 'z'), nullptr)
        << "Sound disabled should NOT produce 'z'";
}

TEST_F(StatusHandlerTest, GU_AlwaysEndsWithThreeDigits) {
    // Last 3 chars are: pulseRateSelect, guideRateSelect, errorCode (each '0'-'9')
    dispatch("GU", "");
    int len = static_cast<int>(std::strlen(reply));
    ASSERT_GE(len, 3) << "GU reply must have at least 3 chars";
    EXPECT_GE(reply[len-1], '0'); EXPECT_LE(reply[len-1], '9');  // error
    EXPECT_GE(reply[len-2], '0'); EXPECT_LE(reply[len-2], '9');  // guide rate
    EXPECT_GE(reply[len-3], '0'); EXPECT_LE(reply[len-3], '9');  // pulse rate
}

TEST_F(StatusHandlerTest, GU_MountTypeGEM) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";
    if (cfg.mountType != MOUNT_GEM) GTEST_SKIP() << "Mount is not GEM";
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'E'), nullptr)
        << "GEM mount should produce 'E' in GU string";
}

TEST_F(StatusHandlerTest, GU_MountTypeAltAz) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";
    if (cfg.mountType != MOUNT_ALTAZM && cfg.mountType != MOUNT_ALTAZM_UNL)
        GTEST_SKIP() << "Mount is not ALTAZM";
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'A'), nullptr)
        << "ALTAZM mount should produce 'A' in GU string";
}

TEST_F(StatusHandlerTest, GU_PecNoneCharSlash) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    if (!cfg.isEquatorial()) GTEST_SKIP() << "PEC char only on equatorial mounts";
    state.pecState    = PecState::NONE;
    state.pecRecorded = false;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, '/'), nullptr)
        << "PEC NONE state should produce '/' in GU string";
}

TEST_F(StatusHandlerTest, GU_PecPlayingCharCaret) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    if (!cfg.isEquatorial()) GTEST_SKIP() << "PEC char only on equatorial mounts";
    state.pecState    = PecState::PLAYING;
    state.pecRecorded = true;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, '^'), nullptr)
        << "PEC PLAYING state should produce '^'";
}

TEST_F(StatusHandlerTest, GU_PpsSync) {
    if (!cfg.hasPPS) GTEST_SKIP() << "PPS not in this config";
    state.ppsSynced = true;
    dispatch("GU", "");
    EXPECT_NE(std::strchr(reply, 'S'), nullptr)
        << "PPS synced should produce 'S'";
}

// ---------------------------------------------------------------------------
// :Gu# — 9-byte binary status
// ---------------------------------------------------------------------------

TEST_F(StatusHandlerTest, Gu_Handled) {
    EXPECT_TRUE(dispatch("Gu", ""));
}

TEST_F(StatusHandlerTest, Gu_SuppressFrame) {
    dispatch("Gu", "");
    EXPECT_TRUE(suppressFrame) << ":Gu# must set suppressFrame (no '#' appended)";
}

TEST_F(StatusHandlerTest, Gu_ExactlyNineBytes) {
    dispatch("Gu", "");
    EXPECT_EQ(std::strlen(reply), 9u)
        << ":Gu# must return exactly 9 bytes (all >= 0x80, so no NUL in range)";
}

TEST_F(StatusHandlerTest, Gu_AllBytesHaveBit7Set) {
    dispatch("Gu", "");
    for (int i = 0; i < 9; ++i) {
        EXPECT_GE(static_cast<uint8_t>(reply[i]), 0x80u)
            << "Byte[" << i << "] must have bit7 set (>= 0x80)";
    }
}

TEST_F(StatusHandlerTest, Gu_Byte5_UnparkedState) {
    state.parkState = PS_UNPARKED;
    dispatch("Gu", "");
    uint8_t b5 = static_cast<uint8_t>(reply[5]);
    EXPECT_EQ(b5, static_cast<uint8_t>(PS_UNPARKED | 0x80))
        << "Byte[5] should be PS_UNPARKED(0) | 0x80 = 0x80";
}

TEST_F(StatusHandlerTest, Gu_Byte5_ParkedState) {
    state.parkState = PS_PARKED;
    dispatch("Gu", "");
    uint8_t b5 = static_cast<uint8_t>(reply[5]);
    EXPECT_EQ(b5, static_cast<uint8_t>(PS_PARKED | 0x80))
        << "Byte[5] should be PS_PARKED(2) | 0x80 = 0x82";
}

TEST_F(StatusHandlerTest, Gu_Byte5_ParkingState) {
    state.parkState = PS_PARKING;
    dispatch("Gu", "");
    uint8_t b5 = static_cast<uint8_t>(reply[5]);
    EXPECT_EQ(b5, static_cast<uint8_t>(PS_PARKING | 0x80));
}

TEST_F(StatusHandlerTest, Gu_Byte0_NotTrackingBit0) {
    state.isTracking = false;
    dispatch("Gu", "");
    uint8_t b0 = static_cast<uint8_t>(reply[0]);
    EXPECT_NE(b0 & 0x01, 0u) << "Byte[0] bit0 should be set when not tracking";
}

TEST_F(StatusHandlerTest, Gu_Byte0_TrackingBit0Clear) {
    state.isTracking = true;
    dispatch("Gu", "");
    uint8_t b0 = static_cast<uint8_t>(reply[0]);
    EXPECT_EQ(b0 & 0x01, 0u) << "Byte[0] bit0 should be clear when tracking";
}

TEST_F(StatusHandlerTest, Gu_Byte2_SoundBit3) {
    state.soundEnabled = true;
    dispatch("Gu", "");
    uint8_t b2 = static_cast<uint8_t>(reply[2]);
    EXPECT_NE(b2 & 0x08, 0u) << "Byte[2] bit3 should be set when sound enabled";
}

TEST_F(StatusHandlerTest, Gu_Byte4_PecState) {
    if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
    state.pecState    = PecState::PLAYING;
    state.pecRecorded = true;
    dispatch("Gu", "");
    uint8_t b4 = static_cast<uint8_t>(reply[4]);
    uint8_t pecVal = b4 & 0x0F;
    EXPECT_EQ(pecVal, static_cast<uint8_t>(PecState::PLAYING));
    EXPECT_NE(b4 & 0x40, 0u) << "Byte[4] bit6 should be set when pecRecorded";
}

TEST_F(StatusHandlerTest, Gu_Byte8_ErrorCode) {
    state.errorCode = 3;
    dispatch("Gu", "");
    uint8_t b8 = static_cast<uint8_t>(reply[8]);
    EXPECT_EQ(b8 & 0x0F, 3u) << "Byte[8] low nibble should be the error code";
}

// ---------------------------------------------------------------------------
// :GW# — brief mount status
// ---------------------------------------------------------------------------

TEST_F(StatusHandlerTest, GW_Handled) {
    EXPECT_TRUE(dispatch("GW", ""));
}

TEST_F(StatusHandlerTest, GW_ThreeChars) {
    dispatch("GW", "");
    EXPECT_EQ(std::strlen(reply), 3u) << ":GW# reply should be exactly 3 chars";
}

TEST_F(StatusHandlerTest, GW_TrackingChar) {
    state.isTracking = true;
    dispatch("GW", "");
    EXPECT_EQ(reply[1], 'T') << "Second char should be 'T' when tracking";
}

TEST_F(StatusHandlerTest, GW_NotTrackingChar) {
    state.isTracking = false;
    dispatch("GW", "");
    EXPECT_EQ(reply[1], 'N') << "Second char should be 'N' when not tracking";
}

TEST_F(StatusHandlerTest, GW_ParkedStatus) {
    state.parkState  = PS_PARKED;
    state.isAtHome   = false;
    state.alignDone  = false;
    dispatch("GW", "");
    EXPECT_EQ(reply[2], 'P') << "Third char should be 'P' when parked";
}

TEST_F(StatusHandlerTest, GW_AlignDoneStatus) {
    state.parkState  = PS_UNPARKED;
    state.isAtHome   = false;
    state.alignDone  = true;
    dispatch("GW", "");
    EXPECT_EQ(reply[2], '1') << "Third char should be '1' when align done";
}

TEST_F(StatusHandlerTest, GW_MountTypeGEM) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount in this config";
    if (cfg.mountType != MOUNT_GEM) GTEST_SKIP() << "Mount is not GEM";
    dispatch("GW", "");
    EXPECT_EQ(reply[0], 'G') << "First char should be 'G' for GEM";
}

// ---------------------------------------------------------------------------
// :Gm# — pier side
// ---------------------------------------------------------------------------

TEST_F(StatusHandlerTest, Gm_Handled) {
    EXPECT_TRUE(dispatch("Gm", ""));
}

TEST_F(StatusHandlerTest, Gm_EastPierSide) {
    state.pierSide = PIER_SIDE_EAST;
    dispatch("Gm", "");
    EXPECT_EQ(reply[0], 'E');
}

TEST_F(StatusHandlerTest, Gm_WestPierSide) {
    state.pierSide = PIER_SIDE_WEST;
    dispatch("Gm", "");
    EXPECT_EQ(reply[0], 'W');
}

TEST_F(StatusHandlerTest, Gm_NonePierSide) {
    state.pierSide = PIER_SIDE_NONE;
    dispatch("Gm", "");
    EXPECT_EQ(reply[0], 'N');
}

// ---------------------------------------------------------------------------
// :SX97,n# — buzzer control
// ---------------------------------------------------------------------------

TEST_F(StatusHandlerTest, SX97_Handled) {
    EXPECT_TRUE(dispatch("SX", "97,0"));
}

TEST_F(StatusHandlerTest, SX97_NumericReply) {
    dispatch("SX", "97,0");
    EXPECT_TRUE(numericReply) << ":SX97# should produce single-char numeric reply";
}

TEST_F(StatusHandlerTest, SX97_OffReturnsOne) {
    dispatch("SX", "97,0");
    EXPECT_EQ(reply[0], '1');
    EXPECT_FALSE(state.soundEnabled);
}

TEST_F(StatusHandlerTest, SX97_OnReturnsOne) {
    dispatch("SX", "97,1");
    EXPECT_EQ(reply[0], '1');
    EXPECT_TRUE(state.soundEnabled);
}

TEST_F(StatusHandlerTest, SX97_BeepReturnsOne) {
    dispatch("SX", "97,2");
    EXPECT_EQ(reply[0], '1');
}

TEST_F(StatusHandlerTest, SX97_AlertReturnsOne) {
    dispatch("SX", "97,3");
    EXPECT_EQ(reply[0], '1');
}

TEST_F(StatusHandlerTest, SX97_ClickReturnsOne) {
    dispatch("SX", "97,4");
    EXPECT_EQ(reply[0], '1');
}

TEST_F(StatusHandlerTest, SX97_InvalidValueReturnsZero) {
    dispatch("SX", "97,5");
    EXPECT_EQ(reply[0], '0') << "Value 5 is out of range — should return '0'";
}

TEST_F(StatusHandlerTest, SX97_OtherSXNotHandled) {
    // :SX96# should not be handled by StatusHandler
    EXPECT_FALSE(dispatch("SX", "96,0"));
}
