// test_features_handler.cpp — Unit tests for FeaturesHandler.
//
// Feature tests are gated per slot and per purpose type.
// The :GXY0# mask test runs unconditionally and asserts the mask
// matches the parsed config — it is always valid.
//
// Helper: findSlot(purpose) returns the first 0-based slot index with
// a matching purpose, or -1 if none present. Used to skip tests that
// require a specific feature type.
//
// Config profiles that exercise these tests:
//   aux_features    (3 x SWITCH at slots 5,6,7)
//   kitchen_sink    (all 8 purpose types across 8 slots)
//   aux_weather     (DEW_HEATER×2)
//   (all others)    (0 features — mask test runs, all others skip)

#include "SimTestBase.h"
#include "handlers/FeaturesHandler.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

// Feature purpose constants — mirror Constants.h values
static constexpr int FP_OFF              = -1;
static constexpr int FP_SWITCH           =  1;
static constexpr int FP_ANALOG_OUTPUT    =  2;
static constexpr int FP_DEW_HEATER       =  3;
static constexpr int FP_INTERVALOMETER   =  4;
static constexpr int FP_MOMENTARY_SWITCH =  5;
static constexpr int FP_HIDDEN_SWITCH    =  6;
static constexpr int FP_COVER_SWITCH     =  7;

class FeaturesHandlerTest : public SimTestBase {
protected:
    SimState        simState;
    FeaturesHandler handler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);
        // Always register — DEC-020 rule
        handler.setConfig(&cfg);
        handler.setState(&simState);
        // Initialise feature state from config purposes
        for (int i = 0; i < 8; ++i) {
            simState.feature[i].purpose = cfg.featurePurpose[i];
        }
    }

    // Find first slot (0-based) with the given purpose, or -1
    int findSlot(int purpose) const {
        for (int i = 0; i < 8; ++i)
            if (cfg.featurePurpose[i] == purpose) return i;
        return -1;
    }

    // True if any visible slot exists (non-OFF, non-HIDDEN)
    bool hasVisibleFeature() const {
        for (int i = 0; i < 8; ++i) {
            int p = cfg.featurePurpose[i];
            if (p != FP_OFF && p != FP_HIDDEN_SWITCH) return true;
        }
        return false;
    }

    // Convenience dispatcher
    bool dispatch(const char* cmd, const char* param,
                  char* reply, bool* sf, bool* nr, CommandError* err) {
        return handler.handle(cmd, param, reply, sf, nr, err);
    }
};

// ---------------------------------------------------------------------------
// :GXY0# — active features mask (unconditional — always runs)
// ---------------------------------------------------------------------------

TEST_F(FeaturesHandlerTest, GXY0_MaskLength8) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "Y0", reply, &sf, &nr, &err));
    EXPECT_EQ(std::strlen(reply), 8u);
}

TEST_F(FeaturesHandlerTest, GXY0_MaskMatchesConfig) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "Y0", reply, &sf, &nr, &err));

    for (int i = 0; i < 8; ++i) {
        char expected = (cfg.featurePurpose[i] != FP_OFF) ? '1' : '0';
        EXPECT_EQ(reply[i], expected)
            << "Mask mismatch at slot " << i
            << " purpose=" << cfg.featurePurpose[i];
    }
}

TEST_F(FeaturesHandlerTest, GXY0_OnlyZeroAndOneChars) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "Y0", reply, &sf, &nr, &err));
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(reply[i] == '0' || reply[i] == '1')
            << "Unexpected char '" << reply[i] << "' at index " << i;
    }
}

// ---------------------------------------------------------------------------
// :GXY[n]# — feature info
// ---------------------------------------------------------------------------

TEST_F(FeaturesHandlerTest, GXYn_AbsentSlot_ReturnsCE0) {
    // Find a slot that is OFF
    int offSlot = -1;
    for (int i = 0; i < 8; ++i)
        if (cfg.featurePurpose[i] == FP_OFF) { offSlot = i; break; }
    if (offSlot < 0) GTEST_SKIP() << "No OFF slots in this config";

    char param[4] = { 'Y', static_cast<char>('1' + offSlot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_0);
}

TEST_F(FeaturesHandlerTest, GXYn_PresentSlot_ReturnsNameAndPurpose) {
    if (!hasVisibleFeature()) GTEST_SKIP() << "No visible features in this config";

    // Find first visible slot
    int slot = -1;
    for (int i = 0; i < 8; ++i)
        if (cfg.featurePurpose[i] != FP_OFF && cfg.featurePurpose[i] != FP_HIDDEN_SWITCH)
            { slot = i; break; }

    char param[4] = { 'Y', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_NONE);
    // Reply must contain a comma (name,purpose format)
    EXPECT_NE(std::strchr(reply, ','), nullptr)
        << "Expected 'name,purpose' format, got: " << reply;
}

TEST_F(FeaturesHandlerTest, GXYn_MomentarySwitch_ReportsPurpose1) {
    int slot = findSlot(FP_MOMENTARY_SWITCH);
    if (slot < 0) GTEST_SKIP() << "No MOMENTARY_SWITCH in this config";

    char param[4] = { 'Y', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    // Find the purpose field after the comma
    const char* comma = std::strchr(reply, ',');
    ASSERT_NE(comma, nullptr);
    EXPECT_EQ(std::atoi(comma + 1), FP_SWITCH)
        << "MOMENTARY_SWITCH should report purpose=1 (SWITCH)";
}

TEST_F(FeaturesHandlerTest, GXYn_CoverSwitch_ReportsPurpose1) {
    int slot = findSlot(FP_COVER_SWITCH);
    if (slot < 0) GTEST_SKIP() << "No COVER_SWITCH in this config";

    char param[4] = { 'Y', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    const char* comma = std::strchr(reply, ',');
    ASSERT_NE(comma, nullptr);
    EXPECT_EQ(std::atoi(comma + 1), FP_SWITCH)
        << "COVER_SWITCH should report purpose=1 (SWITCH)";
}

TEST_F(FeaturesHandlerTest, GXYn_HiddenSwitch_ReturnsCE0) {
    int slot = findSlot(FP_HIDDEN_SWITCH);
    if (slot < 0) GTEST_SKIP() << "No HIDDEN_SWITCH in this config";

    char param[4] = { 'Y', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    // HIDDEN_SWITCH is not reported — same as OFF
    EXPECT_EQ(err, CE_0);
}

// ---------------------------------------------------------------------------
// :GXX[n]# — feature value reads
// ---------------------------------------------------------------------------

TEST_F(FeaturesHandlerTest, GXXn_Switch_InitiallyZero) {
    int slot = findSlot(FP_SWITCH);
    if (slot < 0) GTEST_SKIP() << "No SWITCH in this config";

    char param[4] = { 'X', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "0");
}

TEST_F(FeaturesHandlerTest, GXXn_AnalogOutput_InitiallyZero) {
    int slot = findSlot(FP_ANALOG_OUTPUT);
    if (slot < 0) GTEST_SKIP() << "No ANALOG_OUTPUT in this config";

    char param[4] = { 'X', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    EXPECT_EQ(std::atol(reply), 0L);
}

TEST_F(FeaturesHandlerTest, GXXn_DewHeater_HasCorrectFormat) {
    int slot = findSlot(FP_DEW_HEATER);
    if (slot < 0) GTEST_SKIP() << "No DEW_HEATER in this config";

    char param[4] = { 'X', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    // Format: "enabled,zero,span,deltaT" — must have 3 commas
    int commas = 0;
    for (const char* p = reply; *p; ++p) if (*p == ',') ++commas;
    EXPECT_EQ(commas, 3) << "DEW_HEATER reply should have 3 commas: " << reply;
}

TEST_F(FeaturesHandlerTest, GXXn_Intervalometer_HasCorrectFormat) {
    int slot = findSlot(FP_INTERVALOMETER);
    if (slot < 0) GTEST_SKIP() << "No INTERVALOMETER in this config";

    char param[4] = { 'X', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    // Format: "currentCount,exposure,delay,count" — must have 3 commas
    int commas = 0;
    for (const char* p = reply; *p; ++p) if (*p == ',') ++commas;
    EXPECT_EQ(commas, 3) << "INTERVALOMETER reply should have 3 commas: " << reply;
}

TEST_F(FeaturesHandlerTest, GXXn_AbsentSlot_ReturnsCE0) {
    int offSlot = -1;
    for (int i = 0; i < 8; ++i)
        if (cfg.featurePurpose[i] == FP_OFF) { offSlot = i; break; }
    if (offSlot < 0) GTEST_SKIP() << "No OFF slots in this config";

    char param[4] = { 'X', static_cast<char>('1' + offSlot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    EXPECT_EQ(err, CE_0);
}

// ---------------------------------------------------------------------------
// :SXX[n],V[v]# — set switch / analog / enable
// ---------------------------------------------------------------------------

TEST_F(FeaturesHandlerTest, SXXn_Switch_SetAndGet) {
    int slot = findSlot(FP_SWITCH);
    if (slot < 0) GTEST_SKIP() << "No SWITCH in this config";

    // Build :SXX[n],V1#
    char setParam[16];
    std::snprintf(setParam, sizeof(setParam), "X%c,V1", static_cast<char>('1' + slot));
    char getParam[4] = { 'X', static_cast<char>('1' + slot), '\0' };

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    // Set value to 1
    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '1');

    // Read back
    std::memset(reply, 0, 256);
    sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("GX", getParam, reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "1");
}

TEST_F(FeaturesHandlerTest, SXXn_Switch_ClearToZero) {
    int slot = findSlot(FP_SWITCH);
    if (slot < 0) GTEST_SKIP() << "No SWITCH in this config";

    simState.feature[slot].value = 1;

    char setParam[16];
    std::snprintf(setParam, sizeof(setParam), "X%c,V0", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(simState.feature[slot].value, 0L);
}

TEST_F(FeaturesHandlerTest, SXXn_AnalogOutput_SetValue) {
    int slot = findSlot(FP_ANALOG_OUTPUT);
    if (slot < 0) GTEST_SKIP() << "No ANALOG_OUTPUT in this config";

    char setParam[16];
    std::snprintf(setParam, sizeof(setParam), "X%c,V128", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_EQ(simState.feature[slot].value, 128L);
}

TEST_F(FeaturesHandlerTest, SXXn_AnalogOutput_ClampedAt255) {
    int slot = findSlot(FP_ANALOG_OUTPUT);
    if (slot < 0) GTEST_SKIP() << "No ANALOG_OUTPUT in this config";

    char setParam[16];
    std::snprintf(setParam, sizeof(setParam), "X%c,V999", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(simState.feature[slot].value, 255L);
}

// ---------------------------------------------------------------------------
// :SXX[n],Z/S# — dew heater zero/span
// ---------------------------------------------------------------------------

TEST_F(FeaturesHandlerTest, SXXn_DewHeater_SetZero) {
    int slot = findSlot(FP_DEW_HEATER);
    if (slot < 0) GTEST_SKIP() << "No DEW_HEATER in this config";

    char setParam[24];
    std::snprintf(setParam, sizeof(setParam), "X%c,Z3.5", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_NEAR(simState.feature[slot].dewZero, 3.5f, 0.01f);
}

TEST_F(FeaturesHandlerTest, SXXn_DewHeater_SetSpan) {
    int slot = findSlot(FP_DEW_HEATER);
    if (slot < 0) GTEST_SKIP() << "No DEW_HEATER in this config";

    char setParam[24];
    std::snprintf(setParam, sizeof(setParam), "X%c,S8.0", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_NEAR(simState.feature[slot].dewSpan, 8.0f, 0.01f);
}

TEST_F(FeaturesHandlerTest, SXXn_DewHeater_Enable) {
    int slot = findSlot(FP_DEW_HEATER);
    if (slot < 0) GTEST_SKIP() << "No DEW_HEATER in this config";
    simState.feature[slot].dewEnabled = false;

    char setParam[16];
    std::snprintf(setParam, sizeof(setParam), "X%c,V1", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_TRUE(simState.feature[slot].dewEnabled);
}

// ---------------------------------------------------------------------------
// :SXX[n],E/D/C# — intervalometer settings
// ---------------------------------------------------------------------------

TEST_F(FeaturesHandlerTest, SXXn_Intervalometer_SetExposure) {
    int slot = findSlot(FP_INTERVALOMETER);
    if (slot < 0) GTEST_SKIP() << "No INTERVALOMETER in this config";

    char setParam[24];
    std::snprintf(setParam, sizeof(setParam), "X%c,E30.0", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_NEAR(simState.feature[slot].intvExposure, 30.0f, 0.01f);
}

TEST_F(FeaturesHandlerTest, SXXn_Intervalometer_SetDelay) {
    int slot = findSlot(FP_INTERVALOMETER);
    if (slot < 0) GTEST_SKIP() << "No INTERVALOMETER in this config";

    char setParam[24];
    std::snprintf(setParam, sizeof(setParam), "X%c,D10.0", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_NEAR(simState.feature[slot].intvDelay, 10.0f, 0.01f);
}

TEST_F(FeaturesHandlerTest, SXXn_Intervalometer_SetCount) {
    int slot = findSlot(FP_INTERVALOMETER);
    if (slot < 0) GTEST_SKIP() << "No INTERVALOMETER in this config";

    char setParam[16];
    std::snprintf(setParam, sizeof(setParam), "X%c,C25", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '1');
    EXPECT_EQ(simState.feature[slot].intvCount, 25);
}

TEST_F(FeaturesHandlerTest, SXXn_Intervalometer_CountOutOfRange_Returns0) {
    int slot = findSlot(FP_INTERVALOMETER);
    if (slot < 0) GTEST_SKIP() << "No INTERVALOMETER in this config";

    char setParam[16];
    std::snprintf(setParam, sizeof(setParam), "X%c,C300", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '0');
}

TEST_F(FeaturesHandlerTest, SXXn_Intervalometer_ExposureOutOfRange_Returns0) {
    int slot = findSlot(FP_INTERVALOMETER);
    if (slot < 0) GTEST_SKIP() << "No INTERVALOMETER in this config";

    char setParam[24];
    std::snprintf(setParam, sizeof(setParam), "X%c,E9999.0", static_cast<char>('1' + slot));
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", setParam, reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], '0');
}

// ---------------------------------------------------------------------------
// Intervalometer exposure format (variable decimal places)
// ---------------------------------------------------------------------------

TEST_F(FeaturesHandlerTest, Intervalometer_ExposureFormat_LongExposure_NoDecimal) {
    int slot = findSlot(FP_INTERVALOMETER);
    if (slot < 0) GTEST_SKIP() << "No INTERVALOMETER in this config";

    // Set exposure >= 60s
    simState.feature[slot].intvExposure = 120.0f;
    simState.feature[slot].intvDelay    = 5.0f;
    simState.feature[slot].intvCount    = 0;

    char param[4] = { 'X', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    // Second field (exposure) should have no decimal point for >= 60s
    const char* comma1 = std::strchr(reply, ',');
    ASSERT_NE(comma1, nullptr);
    const char* comma2 = std::strchr(comma1 + 1, ',');
    ASSERT_NE(comma2, nullptr);
    // exposure field is between comma1+1 and comma2
    std::string expField(comma1 + 1, comma2);
    EXPECT_EQ(expField.find('.'), std::string::npos)
        << "Exposure >= 60s should have no decimal: " << expField;
}

TEST_F(FeaturesHandlerTest, Intervalometer_ExposureFormat_SubSecond_ThreeDecimal) {
    int slot = findSlot(FP_INTERVALOMETER);
    if (slot < 0) GTEST_SKIP() << "No INTERVALOMETER in this config";

    simState.feature[slot].intvExposure = 0.25f;

    char param[4] = { 'X', static_cast<char>('1' + slot), '\0' };
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", param, reply, &sf, &nr, &err));
    const char* comma1 = std::strchr(reply, ',');
    ASSERT_NE(comma1, nullptr);
    const char* comma2 = std::strchr(comma1 + 1, ',');
    ASSERT_NE(comma2, nullptr);
    std::string expField(comma1 + 1, comma2);
    // Count decimal places
    size_t dotPos = expField.find('.');
    ASSERT_NE(dotPos, std::string::npos) << "Sub-second exposure must have decimal";
    EXPECT_EQ(expField.size() - dotPos - 1, 3u)
        << "Sub-second should have 3 dp: " << expField;
}

// ---------------------------------------------------------------------------
// Round-trip: set via SXX, read back via GXX
// ---------------------------------------------------------------------------

TEST_F(FeaturesHandlerTest, RoundTrip_Switch_SetAndVerify) {
    int slot = findSlot(FP_SWITCH);
    if (slot < 0) GTEST_SKIP() << "No SWITCH in this config";

    char setParam[16], getParam[4];
    std::snprintf(setParam, sizeof(setParam), "X%c,V1", static_cast<char>('1' + slot));
    std::snprintf(getParam, sizeof(getParam), "X%c", static_cast<char>('1' + slot));

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    handler.handle("SX", setParam, reply, &sf, &nr, &err);
    std::memset(reply, 0, 256);
    sf = false; nr = false; err = CE_NONE;
    handler.handle("GX", getParam, reply, &sf, &nr, &err);
    EXPECT_STREQ(reply, "1");
}

// ---------------------------------------------------------------------------
// Non-feature commands are not consumed
// ---------------------------------------------------------------------------

TEST_F(FeaturesHandlerTest, NonFeatureCommand_NotConsumed) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("GU", "", reply, &sf, &nr, &err));
}

TEST_F(FeaturesHandlerTest, GXY0_IsAlwaysConsumed) {
    // Even with no features, GXY0 must be handled (not returned false)
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_TRUE(handler.handle("GX", "Y0", reply, &sf, &nr, &err));
}
