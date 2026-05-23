// test_firmware_handler.cpp — FirmwareHandler unit tests.
//
// Firmware identity is not config-dependent (DEC-001).
// All tests run unconditionally regardless of ONSTEPX_SIM_CONFIG.

#include <gtest/gtest.h>
#include "SimTestBase.h"

#include "handlers/FirmwareHandler.h"

#include <cstring>
#include <cstdlib>

class FirmwareHandlerTest : public SimTestBase {
protected:
    FirmwareHandler handler;
    SimState        state;

    char         reply[256];
    bool         suppressFrame;
    bool         numericReply;
    CommandError error;

    void SetUp() override {
        state.init(cfg);
        handler.setConfig(&cfg);
        handler.setState(&state);
    }

    // Dispatch a GV* command and return whether it was handled
    bool dispatch(const char* param) {
        std::memset(reply, 0, sizeof(reply));
        suppressFrame = false;
        numericReply  = false;
        error         = CE_NONE;
        return handler.handle("GV", param, reply, &suppressFrame, &numericReply, &error);
    }
};

// ---------------------------------------------------------------------------
// :GVP# — product name
// ---------------------------------------------------------------------------

TEST_F(FirmwareHandlerTest, GVP_Handled) {
    EXPECT_TRUE(dispatch("P"));
}

TEST_F(FirmwareHandlerTest, GVP_ReturnsOnStep) {
    dispatch("P");
    // Driver does exact strcmp("On-Step") — must match precisely
    EXPECT_STREQ(reply, "On-Step");
}

TEST_F(FirmwareHandlerTest, GVP_NotNumericReply) {
    dispatch("P");
    EXPECT_FALSE(numericReply);
}

TEST_F(FirmwareHandlerTest, GVP_NotSuppressFrame) {
    dispatch("P");
    EXPECT_FALSE(suppressFrame);
}

// ---------------------------------------------------------------------------
// :GVN# — version string
// ---------------------------------------------------------------------------

TEST_F(FirmwareHandlerTest, GVN_Handled) {
    EXPECT_TRUE(dispatch("N"));
}

TEST_F(FirmwareHandlerTest, GVN_MajorVersionAtLeast10) {
    dispatch("N");
    // Driver calls strtol(version, ...) and checks major >= 10
    int major = static_cast<int>(std::strtol(reply, nullptr, 10));
    EXPECT_GE(major, 10) << "Version major must be >= 10, got: " << reply;
}

TEST_F(FirmwareHandlerTest, GVN_VersionIs10_24c) {
    dispatch("N");
    EXPECT_STREQ(reply, "10.24c");
}

// ---------------------------------------------------------------------------
// :GVD# — firmware date
// ---------------------------------------------------------------------------

TEST_F(FirmwareHandlerTest, GVD_Handled) {
    EXPECT_TRUE(dispatch("D"));
}

TEST_F(FirmwareHandlerTest, GVD_NonEmpty) {
    dispatch("D");
    EXPECT_GT(std::strlen(reply), 0u) << "Firmware date should not be empty";
}

// ---------------------------------------------------------------------------
// :GVT# — firmware time
// ---------------------------------------------------------------------------

TEST_F(FirmwareHandlerTest, GVT_Handled) {
    EXPECT_TRUE(dispatch("T"));
}

TEST_F(FirmwareHandlerTest, GVT_NonEmpty) {
    dispatch("T");
    EXPECT_GT(std::strlen(reply), 0u) << "Firmware time should not be empty";
}

// ---------------------------------------------------------------------------
// :GVC# — config name (from HOST_NAME)
// ---------------------------------------------------------------------------

TEST_F(FirmwareHandlerTest, GVC_Handled) {
    EXPECT_TRUE(dispatch("C"));
}

TEST_F(FirmwareHandlerTest, GVC_MatchesConfigName) {
    dispatch("C");
    EXPECT_STREQ(reply, cfg.configName)
        << "GVC reply should match configName from parsed config";
}

// ---------------------------------------------------------------------------
// :GVH# — hardware description
// ---------------------------------------------------------------------------

TEST_F(FirmwareHandlerTest, GVH_Handled) {
    EXPECT_TRUE(dispatch("H"));
}

TEST_F(FirmwareHandlerTest, GVH_NonEmpty) {
    dispatch("H");
    EXPECT_GT(std::strlen(reply), 0u) << "Hardware string should not be empty";
}

TEST_F(FirmwareHandlerTest, GVH_ContainsSimulated) {
    dispatch("H");
    EXPECT_STREQ(reply, "Simulated");
}

// ---------------------------------------------------------------------------
// :GVM# — general message
// ---------------------------------------------------------------------------

TEST_F(FirmwareHandlerTest, GVM_Handled) {
    EXPECT_TRUE(dispatch("M"));
}

TEST_F(FirmwareHandlerTest, GVM_ContainsNameAndVersion) {
    dispatch("M");
    EXPECT_NE(std::strstr(reply, "On-Step"), nullptr)
        << "GVM reply should contain 'On-Step'";
    EXPECT_NE(std::strstr(reply, "10.24c"), nullptr)
        << "GVM reply should contain version '10.24c'";
}

// ---------------------------------------------------------------------------
// Unknown GV sub-command — must NOT be handled by FirmwareHandler
// ---------------------------------------------------------------------------

TEST_F(FirmwareHandlerTest, GV_UnknownSubCommandNotHandled) {
    EXPECT_FALSE(dispatch("X")) << "Unknown GVX should not be handled";
    EXPECT_FALSE(dispatch("Z")) << "Unknown GVZ should not be handled";
    EXPECT_FALSE(dispatch(""))  << "Empty param should not be handled";
}

// ---------------------------------------------------------------------------
// Non-GV commands — must not be handled
// ---------------------------------------------------------------------------

TEST_F(FirmwareHandlerTest, NonGVCommandNotHandled) {
    std::memset(reply, 0, sizeof(reply));
    suppressFrame = false; numericReply = false; error = CE_NONE;
    EXPECT_FALSE(handler.handle("GU", "", reply, &suppressFrame, &numericReply, &error));
    EXPECT_FALSE(handler.handle("MS", "", reply, &suppressFrame, &numericReply, &error));
    EXPECT_FALSE(handler.handle("hP", "", reply, &suppressFrame, &numericReply, &error));
}
