// SimTestBase.cpp — Custom main(), global config store, and SimTestBase statics.
//
// Provides a custom main() that:
//   1. Strips --sim-config / --gem-config / --aux-config from argv
//   2. Parses the sim config before InitGoogleTest
//   3. Hands the cleaned argv to GTest
//
// This replaces gtest_main so unit_tests must NOT link GTest::gtest_main.
// Instead it links GTest::gtest only (see CMakeLists.txt).

#include "SimTestBase.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Global config store
// ---------------------------------------------------------------------------
SimConfig   g_simCfg;
bool        g_simCfgLoaded  = false;
std::string g_gemConfigPath;
std::string g_auxConfigPath;

// SimTestBase static member
const SimConfig& SimTestBase::cfg = g_simCfg;

// ---------------------------------------------------------------------------
// ParseSimArgs — strip our custom args, parse config
// ---------------------------------------------------------------------------
void ParseSimArgs(int* argc, char** argv) {
    std::string simConfigPath;

    int out = 1;  // argv[0] is always kept
    for (int i = 1; i < *argc; ++i) {
        std::string arg(argv[i]);

        auto startsWith = [&](const std::string& prefix) -> bool {
            return arg.rfind(prefix, 0) == 0;
        };

        if (startsWith("--sim-config=")) {
            simConfigPath = arg.substr(std::strlen("--sim-config="));
        } else if (startsWith("--gem-config=")) {
            g_gemConfigPath = arg.substr(std::strlen("--gem-config="));
        } else if (startsWith("--aux-config=")) {
            g_auxConfigPath = arg.substr(std::strlen("--aux-config="));
        } else {
            argv[out++] = argv[i];  // keep unrecognised args for GTest
        }
    }
    *argc = out;

    if (simConfigPath.empty()) {
        // Not provided — tests that need it will GTEST_SKIP()
        return;
    }

    try {
        g_simCfg = ConfigParser::parseFile(simConfigPath);
        g_simCfgLoaded = true;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[SimTestBase] Failed to parse '%s': %s\n",
                     simConfigPath.c_str(), ex.what());
        // g_simCfgLoaded remains false; tests will GTEST_SKIP()
    }
}

// ---------------------------------------------------------------------------
// Custom main — replaces gtest_main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    // Strip our args and parse config before GTest initialises
    ParseSimArgs(&argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
