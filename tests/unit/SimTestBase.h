#pragma once
// SimTestBase.h — Shared config-driven GTest fixture base (DEC-001).
//
// Config delivery: the config file path is passed to unit_tests as a custom
// command-line argument by ctest (DEC-002):
//
//   unit_tests --sim-config=configs/gem_full.h
//
// This argument is consumed before GTest sees argv, so GTest never complains
// about it. The config is parsed once and stored in g_simCfg.
//
// For the parser self-test, two additional arguments are accepted:
//   --gem-config=<path>   path to gem_full.h
//   --aux-config=<path>   path to aux_features.h
//
// Skip pattern for optional features (Critical Note 13):
//   TEST_F(MyHandlerTest, SomeFeatureTest) {
//       if (!cfg.hasPec) GTEST_SKIP() << "PEC not in this config";
//       ...
//   }
//
// NEVER use ASSERT_TRUE(cfg.hasPec) as a skip guard — it fails rather
// than skips, breaking the ctest run for that config profile.

#include <gtest/gtest.h>
#include "config/ConfigParser.h"
#include "config/SimConfig.h"
#include "state/SimState.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Global config store — populated by ParseSimArgs() before tests run
// ---------------------------------------------------------------------------
extern SimConfig    g_simCfg;
extern bool         g_simCfgLoaded;
extern std::string  g_gemConfigPath;   // for ConfigParserGemTest
extern std::string  g_auxConfigPath;   // for ConfigParserAuxTest

// Called from a custom main() in SimTestBase.cpp before RUN_ALL_TESTS().
// Strips recognised --sim-config / --gem-config / --aux-config args from
// argc/argv so GTest never sees them.
void ParseSimArgs(int* argc, char** argv);

// ---------------------------------------------------------------------------
// SimTestBase — base fixture for all handler test suites
// ---------------------------------------------------------------------------
class SimTestBase : public ::testing::Test {
protected:
    static const SimConfig& cfg;

    void SetUp() override {
        if (!g_simCfgLoaded) {
            GTEST_SKIP() << "Config not loaded — pass --sim-config=<path>";
        }
    }
};
