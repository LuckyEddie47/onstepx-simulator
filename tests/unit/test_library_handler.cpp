// test_library_handler.cpp — Unit tests for LibraryHandler.
//
// All tests gated on cfg.hasMount && cfg.hasGoto.
// Config profiles that exercise these tests:
//   gem_full, gem_basic, gem_focuser1, gem_focuser_multi,
//   gem_rotator, kitchen_sink.

#include "SimTestBase.h"
#include "handlers/LibraryHandler.h"

#include <cstring>
#include <cstdlib>

class LibraryHandlerTest : public SimTestBase {
protected:
    SimState       simState;
    LibraryHandler handler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);
        handler.setConfig(&cfg);
        handler.setState(&simState);
    }

    bool dispatch(const char* cmd, const char* param,
                  char* reply, bool* sf, bool* nr, CommandError* err) {
        return handler.handle(cmd, param, reply, sf, nr, err);
    }
};

// ---------------------------------------------------------------------------
// Gate: no mount/goto — handler must not consume commands
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, NoMount_LI_NotConsumed) {
    if (cfg.hasMount && cfg.hasGoto) GTEST_SKIP() << "Config has mount+goto";
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("LI", "", reply, &sf, &nr, &err));
}

// ---------------------------------------------------------------------------
// :LI# — get current record
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, LI_DefaultFirstRecord_ReturnsM42) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    // Catalog 0, record 0 = M42 with type DN
    EXPECT_STREQ(reply, "M42,DN");
}

TEST_F(LibraryHandlerTest, LI_EmptyRecord_ReturnsUnknown) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    // Jump to a record that has no entry
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    // :LC[10]# — go to record 10 (unoccupied)
    handler.handle("LC", "10", reply, &sf, &nr, &err);
    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;

    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_EQ(reply[0], ',');  // empty name, comma, UNK
    EXPECT_NE(std::strstr(reply, "UNK"), nullptr);
}

// ---------------------------------------------------------------------------
// :LN# / :LB# — cursor movement
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, LN_AdvancesToNextOccupied) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    // Start at record 0 (M42), advance to record 1 (M31)
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("LN", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);  // no reply

    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "M31,SG");
}

TEST_F(LibraryHandlerTest, LB_MovesToPrevious) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    // Go forward to M31, then back to M42
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    handler.handle("LN", "", reply, &sf, &nr, &err);  // -> M31
    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    handler.handle("LB", "", reply, &sf, &nr, &err);  // -> M42

    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "M42,DN");
}

// ---------------------------------------------------------------------------
// :LC[n]# — goto record n
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, LC_JumpsToRecord2_Vega) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("LC", "2", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);

    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "Vega,STR");
}

// ---------------------------------------------------------------------------
// :LR# — get record and advance; sets goto target
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, LR_ReturnsNameTypeRADec) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("LR", "", reply, &sf, &nr, &err));
    EXPECT_FALSE(sf);
    EXPECT_FALSE(nr);

    // Must have 3 commas: name,TYPE,RA,Dec
    int commas = 0;
    for (const char* p = reply; *p; ++p) if (*p == ',') ++commas;
    EXPECT_EQ(commas, 3) << "LR# reply should have 3 commas: " << reply;
}

TEST_F(LibraryHandlerTest, LR_RAFormat_HH_MM_SS) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("LR", "", reply, &sf, &nr, &err));

    // Extract RA field (third comma-separated field)
    const char* c1 = std::strchr(reply, ',');
    ASSERT_NE(c1, nullptr);
    const char* c2 = std::strchr(c1 + 1, ',');
    ASSERT_NE(c2, nullptr);
    const char* c3 = std::strchr(c2 + 1, ',');
    ASSERT_NE(c3, nullptr);
    std::string raField(c2 + 1, c3);

    // RA must be "HH:MM:SS" format — two colons, length 8
    EXPECT_EQ(raField.size(), 8u) << "RA field: " << raField;
    EXPECT_EQ(raField[2], ':') << "RA field: " << raField;
    EXPECT_EQ(raField[5], ':') << "RA field: " << raField;
}

TEST_F(LibraryHandlerTest, LR_DecFormat_sDDstarMMcoloSS) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("LR", "", reply, &sf, &nr, &err));

    // Extract Dec field (fourth field — after last comma)
    const char* lastComma = std::strrchr(reply, ',');
    ASSERT_NE(lastComma, nullptr);
    std::string decField(lastComma + 1);

    // Dec must be "sDD*MM:SS" — sign char, '*' separator, ':' separator
    EXPECT_TRUE(decField[0] == '+' || decField[0] == '-')
        << "Dec sign: " << decField;
    EXPECT_NE(decField.find('*'), std::string::npos)
        << "Dec must contain '*': " << decField;
    EXPECT_NE(decField.find(':'), std::string::npos)
        << "Dec must contain ':': " << decField;
}

TEST_F(LibraryHandlerTest, LR_SetsGotoTarget) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    ASSERT_TRUE(handler.handle("LR", "", reply, &sf, &nr, &err));

    std::lock_guard<std::mutex> lk(simState.mutex);
    EXPECT_TRUE(simState.targetRASet);
    EXPECT_TRUE(simState.targetDecSet);
    // M42: RA ~5.588h, Dec ~-5.383°
    EXPECT_NEAR(simState.targetRA,  5.588, 0.01);
    EXPECT_NEAR(simState.targetDec, -5.383, 0.01);
}

TEST_F(LibraryHandlerTest, LR_AdvancesCursor) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    // :LR# reads M42 and advances to M31; next :LI# should show M31
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    handler.handle("LR", "", reply, &sf, &nr, &err);

    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "M31,SG");
}

// ---------------------------------------------------------------------------
// :LIG# — set goto target from current record without advancing
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, LIG_SetsTargetWithoutAdvancing) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("LI", "G", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);

    // Cursor should still be at record 0 (M42)
    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "M42,DN");

    std::lock_guard<std::mutex> lk(simState.mutex);
    EXPECT_TRUE(simState.targetRASet);
}

// ---------------------------------------------------------------------------
// :Lo[n]# — select catalog
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, Lo_SelectValidCatalog_Returns1) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Lo", "3", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '1');
}

TEST_F(LibraryHandlerTest, Lo_SelectInvalidCatalog_Returns0) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("Lo", "15", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '0');
}

TEST_F(LibraryHandlerTest, Lo_ResetsCursorToZero) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    handler.handle("LN", "", reply, &sf, &nr, &err);  // advance cursor from 0
    handler.handle("Lo", "5", reply, &sf, &nr, &err); // select catalog 5
    // After selecting a catalog, cursor resets to 0
    // Verify by reading LI (catalog 5, record 0 is empty — expect ,UNK)
    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_NE(std::strstr(reply, "UNK"), nullptr);
}

// ---------------------------------------------------------------------------
// :LW[name,TYPE]# — write entry; :LD# — clear entry
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, LW_WritesEntryToCurrentCatalog) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    // Set a goto target first
    {
        std::lock_guard<std::mutex> lk(simState.mutex);
        simState.targetRA    = 10.5;
        simState.targetDec   = 30.0;
        simState.targetRASet  = true;
        simState.targetDecSet = true;
    }

    // Switch to an empty catalog to avoid overwriting defaults
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    handler.handle("Lo", "1", reply, &sf, &nr, &err);

    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LW", "TestObj,OC", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '1');

    // Verify by reading it back
    handler.handle("L$", "", reply, &sf, &nr, &err);  // find first name
    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "TestObj,OC");
}

TEST_F(LibraryHandlerTest, LD_ClearsCurrentRecord) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    // Clear M42 (record 0, catalog 0)
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("LD", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);

    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_NE(std::strstr(reply, "UNK"), nullptr) << "Cleared record should show UNK";
}

// ---------------------------------------------------------------------------
// :LL# / :L!# — clear catalog / all catalogs
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, LL_ClearsCurrentCatalog) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("LL", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);

    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_NE(std::strstr(reply, "UNK"), nullptr);
}

TEST_F(LibraryHandlerTest, LBang_ClearsAllCatalogs) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("L!", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(sf);

    // Switch to catalog 5, verify empty
    handler.handle("Lo", "5", reply, &sf, &nr, &err);
    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_NE(std::strstr(reply, "UNK"), nullptr);
}

// ---------------------------------------------------------------------------
// :L?# — free record count
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, LQuery_FreeCount_LessThanTotal) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    ASSERT_TRUE(handler.handle("L?", "", reply, &sf, &nr, &err));
    int free = std::atoi(reply);
    int total = LIB_NUM_CATALOGS * LIB_MAX_RECORDS_PER_CAT;
    EXPECT_GT(free, 0);
    EXPECT_LT(free, total) << "3 pre-populated entries should reduce free count";
}

TEST_F(LibraryHandlerTest, LQuery_FreeCount_IncreasesAfterClear) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    handler.handle("L?", "", reply, &sf, &nr, &err);
    int freeBefore = std::atoi(reply);

    // Clear all
    handler.handle("L!", "", reply, &sf, &nr, &err);
    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    handler.handle("L?", "", reply, &sf, &nr, &err);
    int freeAfter = std::atoi(reply);

    EXPECT_GT(freeAfter, freeBefore);
}

// ---------------------------------------------------------------------------
// :L$# — move to first name record
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, LDollar_FindsFirstNamedRecord) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    // Go to record 2 first, then jump back to first name
    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;

    handler.handle("LC", "2", reply, &sf, &nr, &err);

    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("L$", "", reply, &sf, &nr, &err));
    EXPECT_TRUE(nr);
    EXPECT_EQ(reply[0], '1');

    // Should now point at M42 (first named record in catalog 0)
    std::memset(reply, 0, 256); sf = false; nr = false; err = CE_NONE;
    ASSERT_TRUE(handler.handle("LI", "", reply, &sf, &nr, &err));
    EXPECT_STREQ(reply, "M42,DN");
}

// ---------------------------------------------------------------------------
// Non-library command not consumed
// ---------------------------------------------------------------------------

TEST_F(LibraryHandlerTest, NonLibraryCommand_NotConsumed) {
    if (!cfg.hasMount || !cfg.hasGoto) GTEST_SKIP() << "No mount+goto in config";

    char reply[256] = {};
    bool sf = false, nr = false;
    CommandError err = CE_NONE;
    EXPECT_FALSE(handler.handle("GU", "", reply, &sf, &nr, &err));
}
