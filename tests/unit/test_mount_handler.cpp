// test_mount_handler.cpp — MountHandler and LimitsHandler unit tests.
// Config-driven per DEC-001.

#include <gtest/gtest.h>
#include "SimTestBase.h"
#include "handlers/MountHandler.h"
#include "handlers/LimitsHandler.h"
#include "state/MountStateMachine.h"
#include "state/SimClock.h"

#include <cstring>
#include <cmath>

// ===========================================================================
// MountHandler tests
// ===========================================================================

class MountHandlerTest : public SimTestBase {
protected:
    MountHandler      handler;
    MountStateMachine msm;
    SimClock          clock;
    SimState          state;
    char              reply[256];
    bool              suppressFrame;
    bool              numericReply;
    CommandError      error;

    void SetUp() override {
        SimTestBase::SetUp();
        state.init(cfg);
        state.dateReady    = true;
        state.timeReady    = true;
        state.ra           = 6.0;
        state.dec          = 45.0;
        state.targetRA     = 8.0;
        state.targetDec    = 30.0;
        state.targetRASet  = true;
        state.targetDecSet = true;
        state.parkState    = PS_UNPARKED;
        // Phase 11: startupTrusted required by the new trust gate in
        // validateGoto()/syncToTarget(). Set true by default so tests that
        // are testing something other than the trust precondition itself don't
        // need to set it individually — tests specifically checking the trust
        // gate (or any precondition *before* it) may override to false.
        state.startupTrusted = true;

        clock.setConfig(&cfg);
        clock.setState(&state);
        clock.setSlewMultiplier(100);

        msm.setConfig(&cfg);
        msm.setState(&state);
        msm.setClock(&clock);

        handler.setConfig(&cfg);
        handler.setState(&state);
        handler.setStateMachine(&msm);
    }

    void TearDown() override { clock.stop(); }

    bool dispatch(const char* cmd, const char* param) {
        std::memset(reply, 0, sizeof(reply));
        suppressFrame = false;
        numericReply  = false;
        error         = CE_NONE;
        return handler.handle(cmd, param, reply,
                              &suppressFrame, &numericReply, &error);
    }
};

// ---------------------------------------------------------------------------
// No-mount config
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, NoMountConfigNotHandled) {
    if (cfg.hasMount) GTEST_SKIP() << "Mount present";
    EXPECT_FALSE(dispatch("GR", ""));
}

// ---------------------------------------------------------------------------
// :GR# / :GD# — current coordinates
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, GR_Handled) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("GR", ""));
}

TEST_F(MountHandlerTest, GR_FormatHHMMSS) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("GR", "");
    int colons = 0;
    for (int i = 0; reply[i]; ++i) if (reply[i] == ':') ++colons;
    EXPECT_EQ(colons, 2) << "GR should be HH:MM:SS, got: " << reply;
    EXPECT_FALSE(numericReply);
}

TEST_F(MountHandlerTest, GR_HighPrecision) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("GR", "H");
    EXPECT_NE(std::strchr(reply, '.'), nullptr)
        << "GRH should contain decimal, got: " << reply;
}

TEST_F(MountHandlerTest, GR_ValueMatchesState) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.ra = 6.5;
    dispatch("GR", "");
    int h, m, s;
    std::sscanf(reply, "%d:%d:%d", &h, &m, &s);
    double ra = h + m / 60.0 + s / 3600.0;
    EXPECT_NEAR(ra, 6.5, 0.01);
}

TEST_F(MountHandlerTest, GD_Handled) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("GD", ""));
}

TEST_F(MountHandlerTest, GD_ContainsDegreeSign) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("GD", "");
    EXPECT_NE(std::strchr(reply, '*'), nullptr)
        << "GD should contain '*', got: " << reply;
}

TEST_F(MountHandlerTest, GD_PositiveDecSign) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.dec = 45.0;
    dispatch("GD", "");
    EXPECT_EQ(reply[0], '+') << "Positive dec should start with '+'";
}

TEST_F(MountHandlerTest, GD_NegativeDecSign) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.dec = -30.0;
    dispatch("GD", "");
    EXPECT_EQ(reply[0], '-') << "Negative dec should start with '-'";
}

// ---------------------------------------------------------------------------
// :GA# / :GZ# — current Alt/Az
//
// Phase 9: previously these only matched bare ":GA#"/":GZ#" with no 'H'
// suffix support at all (an 'H'-suffixed command would have fallen through
// to unknown-command handling), and always rendered a 2-field, PM_LOW-style
// form ("sDD*MM"/"DDD*MM") with no seconds. Verified against firmware's
// Mount.command.cpp that the real default is 3-field PM_HIGH
// ("sDD*MM:SS"/"DDD*MM:SS"), escalating to PM_HIGHEST with an 'H' suffix —
// same shape as :GR#/:GD#. No tests existed for these two commands before
// Phase 9, which is how the gap went undetected.
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, GA_Handled) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("GA", ""));
}

TEST_F(MountHandlerTest, GA_FormatHasSecondsField) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("GA", "");
    int colons = 0;
    for (int i = 0; reply[i]; ++i) if (reply[i] == ':') ++colons;
    EXPECT_EQ(colons, 1) << "GA should be sDD*MM:SS (one colon), got: " << reply;
    EXPECT_NE(std::strchr(reply, '*'), nullptr);
}

TEST_F(MountHandlerTest, GA_HighPrecisionSuffixRecognized) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("GA", "H"))
        << "GAH must be recognized, not fall through to unknown-command";
    EXPECT_NE(std::strchr(reply, '.'), nullptr)
        << "GAH should contain a decimal point, got: " << reply;
}

TEST_F(MountHandlerTest, GA_InvalidSuffixIsParamForm) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("GA", "X");
    EXPECT_EQ(error, CE_PARAM_FORM);
}

TEST_F(MountHandlerTest, GZ_Handled) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("GZ", ""));
}

TEST_F(MountHandlerTest, GZ_FormatHasSecondsFieldAndThreeDigitDegree) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.az = 45.5;
    dispatch("GZ", "");
    int colons = 0;
    for (int i = 0; reply[i]; ++i) if (reply[i] == ':') ++colons;
    EXPECT_EQ(colons, 1) << "GZ should be DDD*MM:SS (one colon), got: " << reply;
    // Three-digit degree field, no sign character
    EXPECT_NE(reply[0], '+');
    EXPECT_NE(reply[0], '-');
}

TEST_F(MountHandlerTest, GZ_HighPrecisionSuffixRecognized) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("GZ", "H"))
        << "GZH must be recognized, not fall through to unknown-command";
    EXPECT_NE(std::strchr(reply, '.'), nullptr)
        << "GZH should contain a decimal point, got: " << reply;
}

// ---------------------------------------------------------------------------
// :GR# / :GD# — rounding-carry regression coverage (Phase 9)
//
// These specific RA/Dec values were confirmed during the audit to produce
// impossible output ("HH:59:60.0000"-style) under the pre-Phase-9
// MountHandler::formatRA/formatDec. They must now roll over correctly.
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, GRH_RoundingCarriesIntoHour) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.ra = 12.999999999;
    dispatch("GR", "H");
    EXPECT_STREQ(reply, "13:00:00.0000");
}

TEST_F(MountHandlerTest, GDH_RoundingCarriesAt90) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.dec = 89.9999999999;
    dispatch("GD", "H");
    EXPECT_STREQ(reply, "+90*00:00.000");
}

// ---------------------------------------------------------------------------
// :Gr# / :Gd# — target coordinates
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, Gr_Handled) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("Gr", ""));
}

TEST_F(MountHandlerTest, Gd_Handled) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("Gd", ""));
}

// ---------------------------------------------------------------------------
// :Sr# / :Sd# — set target coordinates
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, Sr_SetsTargetRA) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("Sr", "10:30:00"));
    EXPECT_TRUE(numericReply);
    EXPECT_EQ(error, CE_NONE);
    EXPECT_NEAR(state.targetRA, 10.5, 0.01);
    EXPECT_TRUE(state.targetRASet);
}

TEST_F(MountHandlerTest, Sd_SetsTargetDec) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("Sd", "+45*30:00"));
    EXPECT_TRUE(numericReply);
    EXPECT_EQ(error, CE_NONE);
    EXPECT_NEAR(state.targetDec, 45.5, 0.01);
    EXPECT_TRUE(state.targetDecSet);
}

TEST_F(MountHandlerTest, Sd_NegativeDec) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("Sd", "-30*00:00");
    EXPECT_NEAR(state.targetDec, -30.0, 0.01);
}

TEST_F(MountHandlerTest, Sr_BadParam) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("Sr", "notanra");
    EXPECT_EQ(error, CE_PARAM_RANGE);
}

// ---------------------------------------------------------------------------
// Tracking
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, TPlus_EnablesTracking) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.isTracking = false;
    dispatch("T+", "");
    EXPECT_TRUE(state.isTracking);
    EXPECT_FALSE(numericReply);
}

TEST_F(MountHandlerTest, TMinus_StopsTracking) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.isTracking = true;
    state.mountState = MountState::TRACKING;
    dispatch("T-", "");
    EXPECT_FALSE(state.isTracking);
}

TEST_F(MountHandlerTest, Ts_SetsSolarRate) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("Ts", "");
    EXPECT_NEAR(state.trackingRateHz, 60.000f, 0.001f);
}

TEST_F(MountHandlerTest, TL_SetsLunarRate) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("TL", "");
    EXPECT_NEAR(state.trackingRateHz, 57.900f, 0.001f);
}

TEST_F(MountHandlerTest, To_SetsSiderealRate) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.trackingRateHz = 57.900f;
    dispatch("To", "");
    EXPECT_NEAR(state.trackingRateHz, 60.136f, 0.001f);
}

// ---------------------------------------------------------------------------
// :MS# — goto
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, MS_ReturnsZeroOnSuccess) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("MS", ""));
    EXPECT_TRUE(suppressFrame)   << ":MS# should set suppressFrame";
    EXPECT_FALSE(numericReply)   << ":MS# should not be numericReply";
    EXPECT_EQ(reply[0], '0')    << ":MS# should return '0' on success";
}

// Phase 11: the old "no target set → '5'" precondition no longer exists
// (it had no firmware equivalent). This test is repurposed to verify the
// new FIRST precondition: untrusted mount returns '9' (CE_SLEW_ERR_UNSPECIFIED)
// which maps to '9' via gotoErrorChar(), matching firmware's Goto::request()
// unconditional trust check (Goto.cpp:81-84).
TEST_F(MountHandlerTest, MS_ReturnsNineWhenUntrusted) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.startupTrusted = false;  // override fixture default
    dispatch("MS", "");
    EXPECT_EQ(reply[0], '9') << "Untrusted mount should return '9'";
}

TEST_F(MountHandlerTest, MS_ReturnsFourWhenParked) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.parkState = PS_PARKED;
    dispatch("MS", "");
    EXPECT_EQ(reply[0], '4') << "Parked should return '4'";
}

// Phase 11: the old '3' (standby) check tested dateReady/timeReady, which
// was the wrong condition. The new standby check is !axesEnabled && !isAtHome.
// dateReady/timeReady are no longer part of the goto precondition chain.
TEST_F(MountHandlerTest, MS_ReturnsThreeWhenAxesDisabledAndNotAtHome) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.axesEnabled = false;
    state.isAtHome    = false;   // prevent auto-recovery
    dispatch("MS", "");
    EXPECT_EQ(reply[0], '3') << "Disabled axes + not at home should return '3'";
}

// Phase 11: the already-slewing case now correctly returns '5' (CE_SLEW_ERR_SLEW,
// "already in goto") per firmware's validate(): `if (state != GS_NONE) return
// CE_SLEW_IN_SLEW`, which maps to index 20, giving (20-16)+'1' = '5'.
// Previously returned '7' (hardware fault) — that was wrong, now fixed.
TEST_F(MountHandlerTest, MS_ReturnsFiveWhenAlreadySlewing) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.parkState    = PS_UNPARKED;
    state.mountState   = MountState::SLEWING_GOTO;
    dispatch("MS", "");
    EXPECT_EQ(reply[0], '5')
        << "Already-slewing should return '5' (CE_SLEW_ERR_SLEW) — "
           "Phase 11: corrected from '7' which was wrong since Phase 10";
}

// ---------------------------------------------------------------------------
// :CM# — sync
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, CM_SyncsAndReturnsNA) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("CM", "");
    EXPECT_STREQ(reply, "N/A");
    EXPECT_FALSE(numericReply);
}

TEST_F(MountHandlerTest, CM_UpdatesPosition) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.targetRA  = 12.0;
    state.targetDec = -45.0;
    dispatch("CM", "");
    EXPECT_NEAR(state.ra,  12.0,  0.001);
    EXPECT_NEAR(state.dec, -45.0, 0.001);
}

// ===========================================================================
// LimitsHandler tests
// ===========================================================================

class LimitsHandlerTest : public SimTestBase {
protected:
    LimitsHandler handler;
    SimState      state;
    char          reply[256];
    bool          suppressFrame;
    bool          numericReply;
    CommandError  error;

    void SetUp() override {
        SimTestBase::SetUp();
        state.init(cfg);
        state.horizonMin        = -10.0;
        state.horizonMax        =  90.0;
        state.meridianLimitEDeg =   5.0;
        state.meridianLimitWDeg =  10.0;
        state.axis1LimitMin     = -180.0;
        state.axis1LimitMax     =  180.0;
        state.axis2LimitMin     =  -90.0;
        state.axis2LimitMax     =   90.0;
        handler.setConfig(&cfg);
        handler.setState(&state);
    }

    bool dispatch(const char* cmd, const char* param) {
        std::memset(reply, 0, sizeof(reply));
        suppressFrame = false;
        numericReply  = false;
        error         = CE_NONE;
        return handler.handle(cmd, param, reply,
                              &suppressFrame, &numericReply, &error);
    }
};

TEST_F(LimitsHandlerTest, Gh_ReturnsHorizonMin) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("Gh", ""));
    EXPECT_NE(std::strchr(reply, '*'), nullptr) << "Gh should contain '*', got: " << reply;
}

TEST_F(LimitsHandlerTest, Gh_ValueMatchesState) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.horizonMin = -5.0;
    dispatch("Gh", "");
    // reply is "+NN*" or "-NN*"
    int val = std::atoi(reply);
    EXPECT_EQ(val, -5);
}

TEST_F(LimitsHandlerTest, Go_ReturnsHorizonMax) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    EXPECT_TRUE(dispatch("Go", ""));
    EXPECT_NE(std::strchr(reply, '*'), nullptr);
}

TEST_F(LimitsHandlerTest, Go_ValueMatchesState) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.horizonMax = 85.0;
    dispatch("Go", "");
    int val = std::atoi(reply);
    EXPECT_EQ(val, 85);
}

TEST_F(LimitsHandlerTest, Sh_SetsHorizonMin) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("Sh", "-5");
    EXPECT_EQ(error, CE_NONE);
    EXPECT_NEAR(state.horizonMin, -5.0, 0.5);
}

TEST_F(LimitsHandlerTest, Sh_RejectsOutOfRange) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("Sh", "-50");
    EXPECT_EQ(error, CE_PARAM_RANGE);
}

TEST_F(LimitsHandlerTest, So_SetsHorizonMax) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("So", "85");
    EXPECT_EQ(error, CE_NONE);
    EXPECT_NEAR(state.horizonMax, 85.0, 0.5);
}

TEST_F(LimitsHandlerTest, So_RejectsOutOfRange) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("So", "95");
    EXPECT_EQ(error, CE_PARAM_RANGE);
}

TEST_F(LimitsHandlerTest, GXE9_ReturnsMeridianEInMinutes) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.meridianLimitEDeg = 5.0;
    dispatch("GX", "E9");
    // 5.0 deg * 4 = 20 minutes
    EXPECT_STREQ(reply, "20");
}

TEST_F(LimitsHandlerTest, GXEA_ReturnsMeridianWInMinutes) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.meridianLimitWDeg = 10.0;
    dispatch("GX", "EA");
    EXPECT_STREQ(reply, "40");
}

TEST_F(LimitsHandlerTest, SXE9_SetsMeridianE) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("SX", "E9,20");  // 20 minutes = 5 degrees
    EXPECT_EQ(error, CE_NONE);
    EXPECT_NEAR(state.meridianLimitEDeg, 5.0, 0.1);
}

TEST_F(LimitsHandlerTest, SXEA_SetsMeridianW) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("SX", "EA,40");  // 40 minutes = 10 degrees
    EXPECT_EQ(error, CE_NONE);
    EXPECT_NEAR(state.meridianLimitWDeg, 10.0, 0.1);
}

TEST_F(LimitsHandlerTest, GXEe_ReturnsAxis1Min) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.axis1LimitMin = -180.0;
    dispatch("GX", "Ee");
    EXPECT_STREQ(reply, "-180");
}

TEST_F(LimitsHandlerTest, GXEw_ReturnsAxis1Max) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    state.axis1LimitMax = 180.0;
    dispatch("GX", "Ew");
    EXPECT_STREQ(reply, "180");
}

// ---------------------------------------------------------------------------
// Phase 12B — T tracking-rate commands that return nothing in firmware
// Firmware's T block: switch covers S/K/L/Q/+/-/R → *numericReply=false → nothing.
// Te/Td/Tn/T1/T2 are NOT in the switch → numericReply stays true → "1"/"0".
// ---------------------------------------------------------------------------

TEST_F(MountHandlerTest, Phase12B_TMinus_SetsSupressFrame) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    dispatch("Te", "");  // enable tracking first so T- has something to stop
    ASSERT_TRUE(dispatch("T-", ""));
    EXPECT_TRUE(suppressFrame)  << ":T-# should set suppressFrame (no reply)";
    EXPECT_FALSE(numericReply);
    EXPECT_EQ(reply[0], '\0');
}

TEST_F(MountHandlerTest, Phase12B_TPlus_SetsSupressFrame) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    ASSERT_TRUE(dispatch("T+", ""));
    EXPECT_TRUE(suppressFrame)  << ":T+# should set suppressFrame (no reply)";
    EXPECT_FALSE(numericReply);
    EXPECT_EQ(reply[0], '\0');
}

TEST_F(MountHandlerTest, Phase12B_Ts_SetsSupressFrame) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    ASSERT_TRUE(dispatch("Ts", ""));
    EXPECT_TRUE(suppressFrame); EXPECT_FALSE(numericReply); EXPECT_EQ(reply[0], '\0');
}

TEST_F(MountHandlerTest, Phase12B_To_SetsSupressFrame) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    ASSERT_TRUE(dispatch("To", ""));
    EXPECT_TRUE(suppressFrame); EXPECT_FALSE(numericReply); EXPECT_EQ(reply[0], '\0');
}

TEST_F(MountHandlerTest, Phase12B_TL_SetsSupressFrame) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    ASSERT_TRUE(dispatch("TL", ""));
    EXPECT_TRUE(suppressFrame); EXPECT_FALSE(numericReply); EXPECT_EQ(reply[0], '\0');
}

TEST_F(MountHandlerTest, Phase12B_TK_SetsSupressFrame) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount";
    ASSERT_TRUE(dispatch("TK", ""));
    EXPECT_TRUE(suppressFrame); EXPECT_FALSE(numericReply); EXPECT_EQ(reply[0], '\0');
}
