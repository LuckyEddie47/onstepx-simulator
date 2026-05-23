// test_site_handler.cpp — SiteHandler unit tests.
//
// Config-driven per DEC-001. Most tests are unconditional since site/time
// commands are always present. Tests that require a mount skip when
// !cfg.hasMount.

#include <gtest/gtest.h>
#include "SimTestBase.h"
#include "handlers/SiteHandler.h"

#include <cstring>
#include <cstdlib>
#include <cmath>

class SiteHandlerTest : public SimTestBase {
protected:
    SiteHandler  handler;
    SimState     state;
    char         reply[256];
    bool         suppressFrame;
    bool         numericReply;
    CommandError error;

    void SetUp() override {
        SimTestBase::SetUp();
        state.init(cfg);
        // Put state in a known condition — date and time NOT yet set
        state.dateReady  = false;
        state.timeReady  = false;
        state.utcHours   = 12.0;
        state.utcDate    = {2024, 6, 15};
        state.currentSite = 0;
        state.sites[0].latitude  = 51.5;
        state.sites[0].longitude = -1.0;   // west of Greenwich
        state.sites[0].timezone  = 1.0;    // UTC+1
        state.sites[0].elevation = 100.0;
        std::strncpy(state.sites[0].name, "TestSite", 15);
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
// :Gc# — always "24"
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, Gc_Handled) {
    EXPECT_TRUE(dispatch("Gc", ""));
}

TEST_F(SiteHandlerTest, Gc_Returns24) {
    dispatch("Gc", "");
    EXPECT_STREQ(reply, "24");
    EXPECT_FALSE(numericReply);
}

// ---------------------------------------------------------------------------
// :GS# — LST
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, GS_Handled) {
    EXPECT_TRUE(dispatch("GS", ""));
}

TEST_F(SiteHandlerTest, GS_NotNumericReply) {
    dispatch("GS", "");
    EXPECT_FALSE(numericReply);
}

TEST_F(SiteHandlerTest, GS_FormatHHMMSS) {
    dispatch("GS", "");
    // Expect HH:MM:SS format — two colons present
    int colons = 0;
    for (int i = 0; reply[i]; ++i) if (reply[i] == ':') ++colons;
    EXPECT_EQ(colons, 2) << "GS reply should be HH:MM:SS, got: " << reply;
}

TEST_F(SiteHandlerTest, GS_HighPrecision) {
    dispatch("GS", "H");
    // HH:MM:SS.SSSS — should contain a decimal point
    EXPECT_NE(std::strchr(reply, '.'), nullptr)
        << "GSH reply should contain decimal point, got: " << reply;
}

TEST_F(SiteHandlerTest, GS_BadParam_ReturnsParamFormError) {
    dispatch("GS", "X");
    EXPECT_EQ(error, CE_PARAM_FORM);
}

// ---------------------------------------------------------------------------
// :GL# — local time
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, GL_Handled) {
    EXPECT_TRUE(dispatch("GL", ""));
}

TEST_F(SiteHandlerTest, GL_FormatHHMMSS) {
    dispatch("GL", "");
    int colons = 0;
    for (int i = 0; reply[i]; ++i) if (reply[i] == ':') ++colons;
    EXPECT_EQ(colons, 2) << "GL reply should be HH:MM:SS, got: " << reply;
}

// ---------------------------------------------------------------------------
// :GG# — UTC offset [s]HH:MM
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, GG_Handled) {
    EXPECT_TRUE(dispatch("GG", ""));
}

TEST_F(SiteHandlerTest, GG_PositiveTzFormat) {
    state.sites[0].timezone = 1.0;
    dispatch("GG", "");
    // Should be "+01:00" or similar — contains ':' and sign
    EXPECT_NE(std::strchr(reply, ':'), nullptr)
        << "GG reply should be sHH:MM, got: " << reply;
}

TEST_F(SiteHandlerTest, GG_NegativeTz) {
    state.sites[0].timezone = -5.0;
    dispatch("GG", "");
    EXPECT_EQ(reply[0], '-') << "Negative timezone should start with '-'";
}

// ---------------------------------------------------------------------------
// :Gg# — longitude
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, Gg_Handled) {
    EXPECT_TRUE(dispatch("Gg", ""));
}

TEST_F(SiteHandlerTest, Gg_ContainsDegreeSign) {
    dispatch("Gg", "");
    EXPECT_NE(std::strchr(reply, '*'), nullptr)
        << "Gg reply should contain '*' degree separator, got: " << reply;
}

// ---------------------------------------------------------------------------
// :Gt# — latitude
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, Gt_Handled) {
    EXPECT_TRUE(dispatch("Gt", ""));
}

TEST_F(SiteHandlerTest, Gt_NorthLatitudePositiveSign) {
    state.sites[0].latitude = 51.5;
    dispatch("Gt", "");
    EXPECT_EQ(reply[0], '+') << "North latitude should be positive, got: " << reply;
}

TEST_F(SiteHandlerTest, Gt_SouthLatitudeNegativeSign) {
    state.sites[0].latitude = -33.9;
    dispatch("Gt", "");
    EXPECT_EQ(reply[0], '-') << "South latitude should be negative, got: " << reply;
}

// ---------------------------------------------------------------------------
// :Gv# — elevation
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, Gv_Handled) {
    EXPECT_TRUE(dispatch("Gv", ""));
}

TEST_F(SiteHandlerTest, Gv_MatchesStateElevation) {
    state.sites[0].elevation = 250.0;
    dispatch("Gv", "");
    double val = std::atof(reply);
    EXPECT_NEAR(val, 250.0, 1.0) << "Gv reply should match elevation, got: " << reply;
}

// ---------------------------------------------------------------------------
// :GM# — site name
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, GM_Handled) {
    EXPECT_TRUE(dispatch("GM", ""));
}

TEST_F(SiteHandlerTest, GM_ReturnsName) {
    dispatch("GM", "");
    EXPECT_STREQ(reply, "TestSite");
}

TEST_F(SiteHandlerTest, GM_EmptyNameReturnsNone) {
    state.sites[0].name[0] = '\0';
    dispatch("GM", "");
    EXPECT_STREQ(reply, "None");
}

// ---------------------------------------------------------------------------
// :GX80# — UT1 time
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, GX80_Handled) {
    EXPECT_TRUE(dispatch("GX", "80"));
}

TEST_F(SiteHandlerTest, GX80_FormatHHMMSS) {
    dispatch("GX", "80");
    int colons = 0;
    for (int i = 0; reply[i]; ++i) if (reply[i] == ':') ++colons;
    EXPECT_EQ(colons, 2) << "GX80 reply should be HH:MM:SS.ss, got: " << reply;
}

// ---------------------------------------------------------------------------
// :GX81# — UT1 date
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, GX81_Handled) {
    EXPECT_TRUE(dispatch("GX", "81"));
}

TEST_F(SiteHandlerTest, GX81_FormatMMDDYY) {
    dispatch("GX", "81");
    int slashes = 0;
    for (int i = 0; reply[i]; ++i) if (reply[i] == '/') ++slashes;
    EXPECT_EQ(slashes, 2) << "GX81 reply should be MM/DD/YY, got: " << reply;
}

// ---------------------------------------------------------------------------
// :GX89# — date/time ready status
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, GX89_NotReadyWhenFlagsUnset) {
    state.dateReady = false;
    state.timeReady = false;
    dispatch("GX", "89");
    EXPECT_EQ(error, CE_1) << "Should return CE_1 (not ready) when flags unset";
}

TEST_F(SiteHandlerTest, GX89_ReadyWhenBothFlagsSet) {
    state.dateReady = true;
    state.timeReady = true;
    dispatch("GX", "89");
    EXPECT_EQ(error, CE_0) << "Should return CE_0 (ready) when both flags set";
}

TEST_F(SiteHandlerTest, GX89_NotReadyWhenOnlyDateSet) {
    state.dateReady = true;
    state.timeReady = false;
    dispatch("GX", "89");
    EXPECT_EQ(error, CE_1);
}

TEST_F(SiteHandlerTest, GX89_NotReadyWhenOnlyTimeSet) {
    state.dateReady = false;
    state.timeReady = true;
    dispatch("GX", "89");
    EXPECT_EQ(error, CE_1);
}

// ---------------------------------------------------------------------------
// :SC# — set date
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, SC_Handled) {
    EXPECT_TRUE(dispatch("SC", "06/15/24"));
}

TEST_F(SiteHandlerTest, SC_NumericReply) {
    dispatch("SC", "06/15/24");
    EXPECT_TRUE(numericReply);
}

TEST_F(SiteHandlerTest, SC_SetsDatReady) {
    state.dateReady = false;
    dispatch("SC", "06/15/24");
    EXPECT_EQ(error, CE_NONE);
    EXPECT_TRUE(state.dateReady);
}

TEST_F(SiteHandlerTest, SC_ParsesDate) {
    dispatch("SC", "03/22/25");
    EXPECT_EQ(state.utcDate.m, 3);
    EXPECT_EQ(state.utcDate.d, 22);
    EXPECT_EQ(state.utcDate.y, 2025);
}

TEST_F(SiteHandlerTest, SC_FourDigitYear) {
    dispatch("SC", "01/01/2026");
    EXPECT_EQ(state.utcDate.y, 2026);
}

TEST_F(SiteHandlerTest, SC_BadFormatSetsError) {
    dispatch("SC", "notadate");
    EXPECT_EQ(error, CE_PARAM_FORM);
}

// ---------------------------------------------------------------------------
// :SL# — set local time
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, SL_Handled) {
    EXPECT_TRUE(dispatch("SL", "20:30:00"));
}

TEST_F(SiteHandlerTest, SL_SetsTimeReady) {
    state.timeReady = false;
    dispatch("SL", "20:30:00");
    EXPECT_TRUE(state.timeReady);
}

TEST_F(SiteHandlerTest, SL_ConvertsToUTC) {
    // Local time 20:30, timezone UTC+1 -> UTC = 19:30
    state.sites[0].timezone = 1.0;
    dispatch("SL", "20:30:00");
    EXPECT_NEAR(state.utcHours, 19.5, 0.01)
        << "UTC should be local - timezone offset";
}

TEST_F(SiteHandlerTest, SL_BadFormatSetsError) {
    dispatch("SL", "notatime");
    EXPECT_EQ(error, CE_PARAM_FORM);
}

// ---------------------------------------------------------------------------
// :SG# — set timezone
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, SG_Handled) {
    EXPECT_TRUE(dispatch("SG", "+05:30"));
}

TEST_F(SiteHandlerTest, SG_SetsTz) {
    dispatch("SG", "+05:30");
    EXPECT_NEAR(state.sites[0].timezone, 5.5, 0.01);
}

TEST_F(SiteHandlerTest, SG_NegativeTz) {
    dispatch("SG", "-05");
    EXPECT_NEAR(state.sites[0].timezone, -5.0, 0.01);
}

// ---------------------------------------------------------------------------
// :St# — set latitude
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, St_Handled) {
    EXPECT_TRUE(dispatch("St", "+51*30"));
}

TEST_F(SiteHandlerTest, St_SetsLatitude) {
    dispatch("St", "+51*30");
    EXPECT_NEAR(state.sites[0].latitude, 51.5, 0.1);
}

TEST_F(SiteHandlerTest, St_NegativeLatitude) {
    dispatch("St", "-33*54");
    EXPECT_NEAR(state.sites[0].latitude, -33.9, 0.1);
}

// ---------------------------------------------------------------------------
// :Sg# — set longitude
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, Sg_Handled) {
    EXPECT_TRUE(dispatch("Sg", "001*30"));
}

// ---------------------------------------------------------------------------
// :Sv# — set elevation
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, Sv_Handled) {
    EXPECT_TRUE(dispatch("Sv", "350.0"));
}

TEST_F(SiteHandlerTest, Sv_SetsElevation) {
    dispatch("Sv", "350.0");
    EXPECT_NEAR(state.sites[0].elevation, 350.0, 1.0);
}

// ---------------------------------------------------------------------------
// :SU# — DUT1 (accept silently)
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, SU_Handled) {
    EXPECT_TRUE(dispatch("SU", "0.3"));
}

TEST_F(SiteHandlerTest, SU_NoError) {
    dispatch("SU", "0.3");
    EXPECT_EQ(error, CE_NONE);
}

// ---------------------------------------------------------------------------
// :SM# / :SN# / :SO# / :SP# — set site names
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, SM_SetsSiteName) {
    dispatch("SM", "MyObservatory");
    EXPECT_STREQ(state.sites[0].name, "MyObservatory");
}

TEST_F(SiteHandlerTest, SM_TooLongNameSetsError) {
    dispatch("SM", "ThisNameIsTooLongForSite");
    EXPECT_EQ(error, CE_PARAM_RANGE);
}

// ---------------------------------------------------------------------------
// :W# — site selection
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, W0_SelectsSite0) {
    state.currentSite = 2;
    // Framer produces cmd="W0" for :W0# (cmd[0]='W', cmd[1]='0', param="")
    char cmd[3] = {'W', '0', 0};
    std::memset(reply, 0, sizeof(reply));
    suppressFrame = false; numericReply = false; error = CE_NONE;
    handler.handle(cmd, "", reply, &suppressFrame, &numericReply, &error);
    EXPECT_EQ(state.currentSite, 0);
}

TEST_F(SiteHandlerTest, WQuery_ReturnsCurrentSite) {
    state.currentSite = 2;
    // Framer produces cmd="W?" for :W?# (cmd[0]='W', cmd[1]='?', param="")
    dispatch("W?", "");
    EXPECT_STREQ(reply, "2");
}

// Proper W[0-3] dispatch: cmd[0]='W', cmd[1]='1'
TEST_F(SiteHandlerTest, W1_SelectsSite1) {
    state.currentSite = 0;
    // handler.handle(cmd="W1", param="")
    char cmd[3] = {'W', '1', 0};
    std::memset(reply, 0, sizeof(reply));
    suppressFrame = false; numericReply = false; error = CE_NONE;
    handler.handle(cmd, "", reply, &suppressFrame, &numericReply, &error);
    EXPECT_EQ(state.currentSite, 1);
}

TEST_F(SiteHandlerTest, W3_SelectsSite3) {
    state.currentSite = 0;
    char cmd[3] = {'W', '3', 0};
    std::memset(reply, 0, sizeof(reply));
    suppressFrame = false; numericReply = false; error = CE_NONE;
    handler.handle(cmd, "", reply, &suppressFrame, &numericReply, &error);
    EXPECT_EQ(state.currentSite, 3);
}

// ---------------------------------------------------------------------------
// Non-site commands not handled
// ---------------------------------------------------------------------------

TEST_F(SiteHandlerTest, NonSiteCommandNotHandled) {
    EXPECT_FALSE(dispatch("GV", "P"));
    EXPECT_FALSE(dispatch("GU", ""));
    EXPECT_FALSE(dispatch("MS", ""));
}
