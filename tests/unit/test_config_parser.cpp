// test_config_parser.cpp — ConfigParser unit tests.
//
// This suite is the ONLY one that is NOT config-driven (DEC-001).
// It tests the parser itself against the two reference config files,
// whose paths are passed via --gem-config and --aux-config arguments
// (set by the config_parser_selftest ctest entry in CMakeLists.txt).
//
// This test MUST pass before any other suite is run.

#include <gtest/gtest.h>
#include "SimTestBase.h"
#include "config/ConfigParser.h"
#include "config/SimConfig.h"

// ---------------------------------------------------------------------------
// GEM full config tests
// ---------------------------------------------------------------------------

class ConfigParserGemTest : public ::testing::Test {
protected:
    SimConfig cfg;
    void SetUp() override {
        if (g_gemConfigPath.empty()) {
            GTEST_SKIP() << "gem config path not set (pass --gem-config=<path>)";
        }
        ASSERT_NO_THROW(cfg = ConfigParser::parseFile(g_gemConfigPath))
            << "Failed to parse: " << g_gemConfigPath;
    }
};

TEST_F(ConfigParserGemTest, HasMount) {
    EXPECT_TRUE(cfg.hasMount);
}

TEST_F(ConfigParserGemTest, MountTypeIsGem) {
    EXPECT_EQ(cfg.mountType, MOUNT_GEM);
}

TEST_F(ConfigParserGemTest, HasGoto) {
    EXPECT_TRUE(cfg.hasGoto);
}

TEST_F(ConfigParserGemTest, HasPec) {
    EXPECT_TRUE(cfg.hasPec);
    EXPECT_EQ(cfg.pecStepsPerWorm, 12800L);
}

TEST_F(ConfigParserGemTest, HasHomeSense) {
    EXPECT_TRUE(cfg.hasHomeSense);
}

TEST_F(ConfigParserGemTest, HasWeather) {
    EXPECT_TRUE(cfg.hasWeather);
}

TEST_F(ConfigParserGemTest, NoRotator) {
    EXPECT_FALSE(cfg.hasRotator);
}

TEST_F(ConfigParserGemTest, OneFocuser) {
    EXPECT_EQ(cfg.numFocusers, 1);
}

TEST_F(ConfigParserGemTest, NoFeatures) {
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(cfg.featurePurpose[i], FEAT_OFF)
            << "FEATURE" << (i+1) << " should be OFF";
    }
}

TEST_F(ConfigParserGemTest, Axis1StepsPerDegree) {
    EXPECT_NEAR(cfg.stepsPerDegree[0], 12800.0, 1.0);
}

TEST_F(ConfigParserGemTest, Axis2StepsPerDegree) {
    EXPECT_NEAR(cfg.stepsPerDegree[1], 12800.0, 1.0);
}

TEST_F(ConfigParserGemTest, Axis1Limits) {
    EXPECT_NEAR(cfg.limitMin[0], -220.0, 1.0);
    EXPECT_NEAR(cfg.limitMax[0],  220.0, 1.0);
}

TEST_F(ConfigParserGemTest, SlewRateBaseDesired) {
    EXPECT_NEAR(cfg.slewRateBaseDesired, 3.0, 0.1);
}

TEST_F(ConfigParserGemTest, SoundEnabled) {
    EXPECT_TRUE(cfg.soundEnabled);
}

TEST_F(ConfigParserGemTest, ConfigName) {
    EXPECT_STREQ(cfg.configName, "OnStepG11");
}

TEST_F(ConfigParserGemTest, FocuserStepsPerMicron) {
    EXPECT_GT(cfg.stepsPerMicron[0], 0.0);
}

TEST_F(ConfigParserGemTest, Axis1SenseLimitStoredAsString) {
    EXPECT_FALSE(cfg.senseLimitMin[0].symbol.empty());
    EXPECT_FALSE(cfg.senseLimitMin[0].isOff());
}

TEST_F(ConfigParserGemTest, FirmwareIdentityFields) {
    EXPECT_STREQ(cfg.firmwareName,    "On-Step");
    EXPECT_STREQ(cfg.firmwareVersion, "10.24c");
    EXPECT_STRNE(cfg.firmwareDate,    "");
    EXPECT_STRNE(cfg.firmwareTime,    "");
}

// ---------------------------------------------------------------------------
// AUX config tests
// ---------------------------------------------------------------------------

class ConfigParserAuxTest : public ::testing::Test {
protected:
    SimConfig cfg;
    void SetUp() override {
        if (g_auxConfigPath.empty()) {
            GTEST_SKIP() << "aux config path not set (pass --aux-config=<path>)";
        }
        ASSERT_NO_THROW(cfg = ConfigParser::parseFile(g_auxConfigPath))
            << "Failed to parse: " << g_auxConfigPath;
    }
};

TEST_F(ConfigParserAuxTest, NoMount) {
    EXPECT_FALSE(cfg.hasMount);
}

TEST_F(ConfigParserAuxTest, NoWeather) {
    EXPECT_FALSE(cfg.hasWeather);
}

TEST_F(ConfigParserAuxTest, NoRotator) {
    EXPECT_FALSE(cfg.hasRotator);
}

TEST_F(ConfigParserAuxTest, NoFocusers) {
    EXPECT_EQ(cfg.numFocusers, 0);
}

TEST_F(ConfigParserAuxTest, FirstFiveSlotOff) {
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(cfg.featurePurpose[i], FEAT_OFF)
            << "FEATURE" << (i+1) << " should be OFF";
    }
}

TEST_F(ConfigParserAuxTest, Feature6IsSwitch) {
    EXPECT_EQ(cfg.featurePurpose[5], FEAT_SWITCH);
}

TEST_F(ConfigParserAuxTest, Feature7IsSwitch) {
    EXPECT_EQ(cfg.featurePurpose[6], FEAT_SWITCH);
}

TEST_F(ConfigParserAuxTest, Feature8IsSwitch) {
    EXPECT_EQ(cfg.featurePurpose[7], FEAT_SWITCH);
}

TEST_F(ConfigParserAuxTest, Feature6Name) {
    EXPECT_STREQ(cfg.featureName[5], "CAMERA PSU");
}

TEST_F(ConfigParserAuxTest, Feature7Name) {
    EXPECT_STREQ(cfg.featureName[6], "OTA FAN");
}

TEST_F(ConfigParserAuxTest, Feature8Name) {
    EXPECT_STREQ(cfg.featureName[7], "DEW HEATER");
}

TEST_F(ConfigParserAuxTest, ConfigName) {
    EXPECT_STREQ(cfg.configName, "OnStepAux");
}

// ---------------------------------------------------------------------------
// Token edge-case tests (inline strings — no file needed)
// ---------------------------------------------------------------------------

class ConfigParserTokenTest : public ::testing::Test {};

TEST_F(ConfigParserTokenTest, OffTokenDisablesAxis) {
    auto cfg = ConfigParser::parseString(
        "#define AXIS1_DRIVER_MODEL OFF\n"
        "#define AXIS2_DRIVER_MODEL OFF\n");
    EXPECT_FALSE(cfg.hasMount);
}

TEST_F(ConfigParserTokenTest, OnTokenEnablesGoto) {
    auto cfg = ConfigParser::parseString(
        "#define AXIS1_DRIVER_MODEL TMC2209\n"
        "#define AXIS2_DRIVER_MODEL TMC2209\n"
        "#define MOUNT_TYPE GEM\n"
        "#define GOTO_FEATURE ON\n");
    EXPECT_TRUE(cfg.hasGoto);
}

TEST_F(ConfigParserTokenTest, NumericPecSteps) {
    auto cfg = ConfigParser::parseString(
        "#define PEC_STEPS_PER_WORM_ROTATION 9600\n");
    EXPECT_TRUE(cfg.hasPec);
    EXPECT_EQ(cfg.pecStepsPerWorm, 9600L);
}

TEST_F(ConfigParserTokenTest, ZeroPecStepsDisablesPec) {
    auto cfg = ConfigParser::parseString(
        "#define PEC_STEPS_PER_WORM_ROTATION 0\n");
    EXPECT_FALSE(cfg.hasPec);
}

TEST_F(ConfigParserTokenTest, FloatStepsPerDegree) {
    auto cfg = ConfigParser::parseString(
        "#define AXIS1_DRIVER_MODEL TMC2209\n"
        "#define AXIS2_DRIVER_MODEL TMC2209\n"
        "#define AXIS1_STEPS_PER_DEGREE 6400.5\n");
    EXPECT_NEAR(cfg.stepsPerDegree[0], 6400.5, 0.01);
}

TEST_F(ConfigParserTokenTest, InlineCommentStripped) {
    auto cfg = ConfigParser::parseString(
        "#define AXIS1_DRIVER_MODEL TMC2209 // comment\n"
        "#define AXIS2_DRIVER_MODEL TMC2209\n");
    EXPECT_TRUE(cfg.hasMount);
}

TEST_F(ConfigParserTokenTest, QuotedHostName) {
    auto cfg = ConfigParser::parseString(
        "#define HOST_NAME \"MyScope\"\n");
    EXPECT_STREQ(cfg.configName, "MyScope");
}

TEST_F(ConfigParserTokenTest, MissingOptionalFieldsUseDefaults) {
    auto cfg = ConfigParser::parseString(
        "#define AXIS1_DRIVER_MODEL TMC2209\n"
        "#define AXIS2_DRIVER_MODEL TMC2209\n");
    EXPECT_FALSE(cfg.hasGoto);
    EXPECT_FALSE(cfg.hasPec);
    EXPECT_FALSE(cfg.hasWeather);
    EXPECT_EQ(cfg.numFocusers, 0);
}

TEST_F(ConfigParserTokenTest, FocuserContiguityRule) {
    auto cfg = ConfigParser::parseString(
        "#define AXIS4_DRIVER_MODEL TMC2209\n"
        "#define AXIS5_DRIVER_MODEL OFF\n"
        "#define AXIS6_DRIVER_MODEL TMC2209\n");
    EXPECT_EQ(cfg.numFocusers, 1);
}

TEST_F(ConfigParserTokenTest, UnresolvedSymbolStoredVerbatim) {
    auto cfg = ConfigParser::parseString(
        "#define AXIS1_SENSE_LIMIT_MIN LIMIT_SENSE\n");
    EXPECT_EQ(cfg.senseLimitMin[0].symbol, "LIMIT_SENSE");
    EXPECT_FALSE(cfg.senseLimitMin[0].isOff());
}

TEST_F(ConfigParserTokenTest, AutoTokenParsed) {
    EXPECT_NO_THROW(ConfigParser::parseString("#define SOME_FEATURE AUTO\n"));
}

TEST_F(ConfigParserTokenTest, FeaturePurposeSwitchByName) {
    auto cfg = ConfigParser::parseString(
        "#define FEATURE6_PURPOSE SWITCH\n"
        "#define FEATURE6_NAME \"TEST\"\n");
    EXPECT_EQ(cfg.featurePurpose[5], FEAT_SWITCH);
    EXPECT_STREQ(cfg.featureName[5], "TEST");
}

TEST_F(ConfigParserTokenTest, FeaturePurposeMomentarySwitch) {
    auto cfg = ConfigParser::parseString(
        "#define FEATURE1_PURPOSE MOMENTARY_SWITCH\n");
    EXPECT_EQ(cfg.featurePurpose[0], FEAT_MOMENTARY_SWITCH);
}
