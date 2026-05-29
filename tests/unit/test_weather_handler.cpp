// test_weather_handler.cpp — Unit tests for WeatherHandler.
//
// Weather tests are gated on cfg.hasWeather.
// :GX9F# (MCU temperature) is unconditional (hasMcuTemp always true in sim).
//
// Config profiles that exercise these tests:
//   gem_full       (hasWeather=true)
//   aux_weather    (hasWeather=true)
//   kitchen_sink   (hasWeather=true)
//   (all others)   (hasWeather=false — MCU temp test runs, weather tests skip)

#include "SimTestBase.h"
#include "handlers/WeatherHandler.h"

#include <cstdlib>
#include <cstring>

class WeatherHandlerTest : public SimTestBase {
protected:
    SimState       simState;
    WeatherHandler handler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);
        // Always register — DEC-020 rule
        handler.setConfig(&cfg);
        handler.setState(&simState);
    }
};

// ---------------------------------------------------------------------------
// :GX9F# — MCU temperature (unconditional — always runs)
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, GX9F_ReturnsMcuTemp_Integer) {
    simState.weather.mcuTemp = 27.0f;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "9F", reply, &sf, &nr, &err));
    EXPECT_FALSE(nr);          // #-terminated, not single-char
    EXPECT_EQ(std::atoi(reply), 27);
    // Must be integer format — no decimal point
    EXPECT_EQ(std::strchr(reply, '.'), nullptr)
        << "MCU temp should be integer format, got: " << reply;
}

TEST_F(WeatherHandlerTest, GX9F_AlwaysHandled_EvenWithoutWeather) {
    // :GX9F# must respond regardless of hasWeather (hasMcuTemp always true)
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    // This must return true for ALL configs
    EXPECT_TRUE(handler.handle("GX", "9F", reply, &sf, &nr, &err));
}

// ---------------------------------------------------------------------------
// :GX9A# — temperature (requires hasWeather)
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, GX9A_ReturnsTemperature_NonZero) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";

    // Default is 15.0°C — must be non-zero for probeController() to succeed
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "9A", reply, &sf, &nr, &err));
    EXPECT_NE(std::atof(reply), 0.0) << "Temperature must be non-zero for probe";
}

TEST_F(WeatherHandlerTest, GX9A_HasSignedFormat) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";
    simState.weather.temperature = 15.0f;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "9A", reply, &sf, &nr, &err));
    // Must start with '+' or '-' (signed format: %+.1f)
    EXPECT_TRUE(reply[0] == '+' || reply[0] == '-')
        << "Temperature must be signed (e.g. '+15.0'), got: " << reply;
}

TEST_F(WeatherHandlerTest, GX9A_NotHandled_WhenNoWeather) {
    if (cfg.hasWeather) GTEST_SKIP() << "Config has weather";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    EXPECT_FALSE(handler.handle("GX", "9A", reply, &sf, &nr, &err));
}

// ---------------------------------------------------------------------------
// :GX9B# — pressure
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, GX9B_ReturnsPressure) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";
    simState.weather.pressure = 1013.0f;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "9B", reply, &sf, &nr, &err));
    EXPECT_NEAR(std::atof(reply), 1013.0, 0.1);
}

// ---------------------------------------------------------------------------
// :GX9C# — humidity
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, GX9C_ReturnsHumidity) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";
    simState.weather.humidity = 60.0f;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "9C", reply, &sf, &nr, &err));
    EXPECT_NEAR(std::atof(reply), 60.0, 0.1);
}

// ---------------------------------------------------------------------------
// :GX9E# — dew point
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, GX9E_ReturnsDewPoint) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";
    simState.weather.dewPoint = 7.0f;

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("GX", "9E", reply, &sf, &nr, &err));
    EXPECT_NEAR(std::atof(reply), 7.0, 0.1);
}

// ---------------------------------------------------------------------------
// :SX9A,[v]# — set temperature (write probe — critical)
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, SX9A_WriteProbe_Returns1_SingleChar) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    // This is the exact command probeController() sends via sendCommandSingleChar
    ASSERT_TRUE(handler.handle("SX", "9A,15.0", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);           // single char, no '#'
    EXPECT_EQ(reply[0], '1');
}

TEST_F(WeatherHandlerTest, SX9A_UpdatesStoredTemperature) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", "9A,22.5", reply, &sf, &nr, &err));
    EXPECT_NEAR(simState.weather.temperature, 22.5f, 0.01f);
}

// ---------------------------------------------------------------------------
// :SX9B,[v]# — set pressure
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, SX9B_UpdatesPressure) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", "9B,1020.5", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '1');
    EXPECT_NEAR(simState.weather.pressure, 1020.5f, 0.01f);
}

// ---------------------------------------------------------------------------
// :SX9C,[v]# — set humidity
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, SX9C_UpdatesHumidity) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("SX", "9C,75.0", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '1');
    EXPECT_NEAR(simState.weather.humidity, 75.0f, 0.01f);
}

// ---------------------------------------------------------------------------
// Read-back after write
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, RoundTrip_Temperature_WriteAndRead) {
    if (!cfg.hasWeather) GTEST_SKIP() << "No weather sensor in this config";

    // Write
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    handler.handle("SX", "9A,-5.5", reply, &sf, &nr, &err);

    // Read back
    std::memset(reply, 0, 256);
    sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("GX", "9A", reply, &sf, &nr, &err));
    EXPECT_NEAR(std::atof(reply), -5.5, 0.1);
}

// ---------------------------------------------------------------------------
// Non-weather commands are not consumed
// ---------------------------------------------------------------------------

TEST_F(WeatherHandlerTest, NonWeatherCommand_NotConsumed) {
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("GU", "", reply, &sf, &nr, &err));
}

TEST_F(WeatherHandlerTest, GX9D_NotConsumed) {
    // :GX9D# is not a weather command (gap in the protocol)
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("GX", "9D", reply, &sf, &nr, &err));
}
