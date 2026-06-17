#pragma once
// LibraryHandler.h — Handles object library commands.
//
// Active when cfg.hasMount && cfg.hasGoto. Returns false for all commands
// when this condition is not met.
//
// State:
//   15 catalogs (indices 0-14), each holding up to MAX_RECORDS_PER_CATALOG
//   records. A cursor tracks the current catalog and current record index.
//
// Pre-populated entries in catalog 0 (for testing without driver interaction):
//   M42  OC  05:35:17  -05*23:00
//   M31  SG  00:42:44  +41*16:00
//   Vega STR 18:36:56  +38*47:00
//
// Record struct: name[12], type (0-15), ra (hours), dec (degrees).
//
// RA/Dec wire format (matching INDI driver's Library.command.cpp parser):
//   RA:  "HH:MM:SS"
//   Dec: "sDD*MM:SS"   where s is '+' or '-'
//
// Commands handled (matching Library.command.cpp):
//   :LB#          Move to previous record; no reply
//   :LN#          Move to next record; no reply
//   :LC[n]#       Goto record n (0-based); no reply
//   :LI#          Get current record "name,TYPE"#
//   :LIG#         Get info and copy RA/Dec to goto target; no reply
//   :LR#          Get record as "name,TYPE,RA,Dec"# and advance; sets goto target
//   :LW[name,TYPE]# Write current goto target to library at cursor; '0'/'1'
//   :L$#          Move cursor to first name record; '1'
//   :LD#          Clear current record; no reply
//   :LL#          Clear current catalog; no reply
//   :L!#          Clear all catalogs; no reply
//   :L?#          Free record count "n"#
//   :Lo[n]#       Select catalog n (0-14); '0'/'1'

#include "HandlerBase.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Object type codes
// ---------------------------------------------------------------------------
enum LibObjectType : uint8_t {
    LIB_UNK = 0, LIB_OC  = 1,  LIB_GC  = 2,  LIB_PN  = 3,
    LIB_DN  = 4, LIB_SG  = 5,  LIB_EG  = 6,  LIB_IG  = 7,
    LIB_KNT = 8, LIB_SNR = 9,  LIB_GAL = 10, LIB_CN  = 11,
    LIB_STR = 12,LIB_PLA = 13, LIB_CMT = 14, LIB_AST = 15,
};

// Type code -> 3-char string (index matches LibObjectType)
static const char* const LIB_TYPE_NAMES[] = {
    "UNK","OC","GC","PN","DN","SG","EG","IG",
    "KNT","SNR","GAL","CN","STR","PLA","CMT","AST"
};
static constexpr int LIB_TYPE_COUNT = 16;

// ---------------------------------------------------------------------------
// Library record
// ---------------------------------------------------------------------------
struct LibRecord {
    char   name[12] = {};    // NUL-terminated, max 11 chars
    uint8_t type    = LIB_UNK;
    double ra       = 0.0;   // hours
    double dec      = 0.0;   // degrees
    bool   occupied = false; // false = empty slot
};

// ---------------------------------------------------------------------------
// LibraryHandler
// ---------------------------------------------------------------------------

static constexpr int LIB_NUM_CATALOGS        = 15;
static constexpr int LIB_MAX_RECORDS_PER_CAT = 64;
static constexpr int LIB_TOTAL_SLOTS         = LIB_NUM_CATALOGS * LIB_MAX_RECORDS_PER_CAT;

class LibraryHandler : public HandlerBase {
public:
    LibraryHandler();

    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error
    ) override;

private:
    // Catalog storage: catalog[c][r]
    LibRecord m_catalog[LIB_NUM_CATALOGS][LIB_MAX_RECORDS_PER_CAT];

    int m_currentCat = 0;   // active catalog index
    int m_cursor     = 0;   // current record index within active catalog

    // Populate pre-loaded test entries in catalog 0
    void populateDefaults();

    // --- cursor helpers ---
    LibRecord&       currentRecord()       { return m_catalog[m_currentCat][m_cursor]; }
    const LibRecord& currentRecord() const { return m_catalog[m_currentCat][m_cursor]; }

    // Advance cursor to next occupied record (wraps; stops at start if none found)
    void cursorNext();

    // Move cursor to previous occupied record
    void cursorPrev();

    // Move cursor to first occupied record whose name field is non-empty
    // Returns true if found
    bool cursorToFirstName();

    // Count free (unoccupied) slots across all catalogs
    int freeCount() const;

    // Find first free slot in current catalog; returns index or -1
    int findFreeSlot() const;

    // Format RA hours as "HH:MM:SS"
    static void formatRA(double hours, char* buf, int bufLen);

    // Format Dec degrees as "sDD*MM:SS"
    static void formatDec(double deg, char* buf, int bufLen);

    // Format type uint8_t as 3-char name string
    static const char* typeName(uint8_t type);

    // Parse type name string back to uint8_t; returns LIB_UNK if not found
    static uint8_t parseType(const char* str);
};
