// LibraryHandler.cpp — Object library command handler.
//
// Protocol source: Library.command.cpp

#include "handlers/LibraryHandler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

// ---------------------------------------------------------------------------
// Constructor — populate default entries
// ---------------------------------------------------------------------------

LibraryHandler::LibraryHandler() {
    populateDefaults();
}

void LibraryHandler::populateDefaults() {
    // Catalog 0 pre-loaded with three well-known objects for testing.
    // RA in decimal hours, Dec in decimal degrees.

    // M42 — Orion Nebula, OC-adjacent (catalogued as DN)
    auto& m42 = m_catalog[0][0];
    std::strncpy(m42.name, "M42", sizeof(m42.name) - 1);
    m42.type     = LIB_DN;
    m42.ra       = (5.0 + 35.0/60.0 + 17.0/3600.0);   // 05h 35m 17s
    m42.dec      = -(5.0 + 23.0/60.0);                  // -05° 23'
    m42.occupied = true;

    // M31 — Andromeda Galaxy, SG
    auto& m31 = m_catalog[0][1];
    std::strncpy(m31.name, "M31", sizeof(m31.name) - 1);
    m31.type     = LIB_SG;
    m31.ra       = (0.0 + 42.0/60.0 + 44.0/3600.0);    // 00h 42m 44s
    m31.dec      = (41.0 + 16.0/60.0);                  // +41° 16'
    m31.occupied = true;

    // Vega — STR
    auto& vega = m_catalog[0][2];
    std::strncpy(vega.name, "Vega", sizeof(vega.name) - 1);
    vega.type     = LIB_STR;
    vega.ra       = (18.0 + 36.0/60.0 + 56.0/3600.0);  // 18h 36m 56s
    vega.dec      = (38.0 + 47.0/60.0);                 // +38° 47'
    vega.occupied = true;
}

// ---------------------------------------------------------------------------
// handle()
// ---------------------------------------------------------------------------

bool LibraryHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)error;

    // Gate: library only active when hasMount && hasGoto
    if (!m_cfg->hasMount || !m_cfg->hasGoto) return false;

    if (cmd[0] != 'L') return false;

    char sub = cmd[1];

    // :LB# — move to previous record; no reply
    if (sub == 'B' && param[0] == '\0') {
        cursorPrev();
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // :LN# — move to next record; no reply
    if (sub == 'N' && param[0] == '\0') {
        cursorNext();
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // :LC[n]# — goto record n (0-based); no reply
    if (sub == 'C' && param[0] != '\0') {
        int n = std::atoi(param);
        if (n >= 0 && n < LIB_MAX_RECORDS_PER_CAT) {
            m_cursor = n;
        }
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // :LI# — get current record "name,TYPE"#
    if (sub == 'I' && param[0] == '\0') {
        const LibRecord& r = currentRecord();
        if (!r.occupied) {
            std::snprintf(reply, 256, ",UNK");
        } else {
            std::snprintf(reply, 256, "%s,%s", r.name, typeName(r.type));
        }
        return true;
    }

    // :LIG# — get info and set goto target; no reply
    if (sub == 'I' && param[0] == 'G' && param[1] == '\0') {
        const LibRecord& r = currentRecord();
        if (r.occupied) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->targetRA    = r.ra;
            m_state->targetDec   = r.dec;
            m_state->targetRASet  = true;
            m_state->targetDecSet = true;
        }
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // :LR# — get "name,TYPE,RA,Dec"# and advance; sets goto target
    if (sub == 'R' && param[0] == '\0') {
        const LibRecord& r = currentRecord();
        char raBuf[16], decBuf[16];
        if (!r.occupied) {
            std::snprintf(reply, 256, ",UNK,00:00:00,+00*00:00");
        } else {
            formatRA(r.ra,   raBuf,  sizeof(raBuf));
            formatDec(r.dec, decBuf, sizeof(decBuf));
            std::snprintf(reply, 256, "%s,%s,%s,%s",
                          r.name, typeName(r.type), raBuf, decBuf);
            // Set goto target
            {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->targetRA    = r.ra;
                m_state->targetDec   = r.dec;
                m_state->targetRASet  = true;
                m_state->targetDecSet = true;
            }
        }
        // Advance to next record
        cursorNext();
        return true;
    }

    // :LW[name,TYPE]# — write current goto target to library; '0'/'1'
    if (sub == 'W') {
        *numericReply = true;
        // Parse "name,TYPE" from param
        const char* comma = std::strchr(param, ',');
        if (!comma) { reply[0] = '0'; return true; }

        int slot = findFreeSlot();
        if (slot < 0) { reply[0] = '0'; return true; }  // catalog full

        LibRecord& r = m_catalog[m_currentCat][slot];
        int nameLen = static_cast<int>(comma - param);
        if (nameLen >= static_cast<int>(sizeof(r.name))) nameLen = sizeof(r.name) - 1;
        std::memcpy(r.name, param, static_cast<size_t>(nameLen));
        r.name[nameLen] = '\0';
        r.type = parseType(comma + 1);
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            r.ra  = m_state->targetRA;
            r.dec = m_state->targetDec;
        }
        r.occupied = true;
        reply[0] = '1';
        return true;
    }

    // :L$# — move cursor to first name record; '1'
    if (sub == '$' && param[0] == '\0') {
        *numericReply = true;
        reply[0] = cursorToFirstName() ? '1' : '0';
        return true;
    }

    // :LD# — clear current record; no reply
    if (sub == 'D' && param[0] == '\0') {
        LibRecord& r = currentRecord();
        r = LibRecord{};
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // :LL# — clear current catalog; no reply
    if (sub == 'L' && param[0] == '\0') {
        for (int i = 0; i < LIB_MAX_RECORDS_PER_CAT; ++i) {
            m_catalog[m_currentCat][i] = LibRecord{};
        }
        m_cursor = 0;
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // :L!# — clear all catalogs; no reply
    if (sub == '!' && param[0] == '\0') {
        for (int c = 0; c < LIB_NUM_CATALOGS; ++c)
            for (int i = 0; i < LIB_MAX_RECORDS_PER_CAT; ++i)
                m_catalog[c][i] = LibRecord{};
        m_currentCat = 0;
        m_cursor     = 0;
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // :L?# — free record count "n"#
    if (sub == '?' && param[0] == '\0') {
        std::snprintf(reply, 256, "%d", freeCount());
        return true;
    }

    // :Lo[n]# — select catalog n (0-14); '0'/'1'
    if (sub == 'o') {
        *numericReply = true;
        int n = std::atoi(param);
        if (n < 0 || n >= LIB_NUM_CATALOGS) {
            reply[0] = '0';
        } else {
            m_currentCat = n;
            m_cursor     = 0;
            reply[0]     = '1';
        }
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Cursor helpers
// ---------------------------------------------------------------------------

void LibraryHandler::cursorNext() {
    // Advance to next slot (wrapping), stop after a full lap
    int start = m_cursor;
    do {
        m_cursor = (m_cursor + 1) % LIB_MAX_RECORDS_PER_CAT;
        if (m_catalog[m_currentCat][m_cursor].occupied) return;
    } while (m_cursor != start);
    // No other occupied record found — stay at start
    m_cursor = start;
}

void LibraryHandler::cursorPrev() {
    int start = m_cursor;
    do {
        m_cursor = (m_cursor - 1 + LIB_MAX_RECORDS_PER_CAT) % LIB_MAX_RECORDS_PER_CAT;
        if (m_catalog[m_currentCat][m_cursor].occupied) return;
    } while (m_cursor != start);
    m_cursor = start;
}

bool LibraryHandler::cursorToFirstName() {
    for (int i = 0; i < LIB_MAX_RECORDS_PER_CAT; ++i) {
        if (m_catalog[m_currentCat][i].occupied &&
            m_catalog[m_currentCat][i].name[0] != '\0') {
            m_cursor = i;
            return true;
        }
    }
    return false;
}

int LibraryHandler::freeCount() const {
    int free = 0;
    for (int c = 0; c < LIB_NUM_CATALOGS; ++c)
        for (int i = 0; i < LIB_MAX_RECORDS_PER_CAT; ++i)
            if (!m_catalog[c][i].occupied) ++free;
    return free;
}

int LibraryHandler::findFreeSlot() const {
    for (int i = 0; i < LIB_MAX_RECORDS_PER_CAT; ++i)
        if (!m_catalog[m_currentCat][i].occupied) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

void LibraryHandler::formatRA(double hours, char* buf, int bufLen) {
    // "HH:MM:SS"
    hours = std::fmod(hours, 24.0);
    if (hours < 0.0) hours += 24.0;
    int h  = static_cast<int>(hours);
    int m  = static_cast<int>((hours - h) * 60.0);
    int s  = static_cast<int>(((hours - h) * 60.0 - m) * 60.0 + 0.5);
    if (s == 60) { s = 0; ++m; }
    if (m == 60) { m = 0; ++h; }
    std::snprintf(buf, static_cast<size_t>(bufLen), "%02d:%02d:%02d", h, m, s);
}

void LibraryHandler::formatDec(double deg, char* buf, int bufLen) {
    // "sDD*MM:SS"
    char sign = (deg < 0.0) ? '-' : '+';
    double absDeg = std::fabs(deg);
    int d  = static_cast<int>(absDeg);
    int m  = static_cast<int>((absDeg - d) * 60.0);
    int s  = static_cast<int>(((absDeg - d) * 60.0 - m) * 60.0 + 0.5);
    if (s == 60) { s = 0; ++m; }
    if (m == 60) { m = 0; ++d; }
    std::snprintf(buf, static_cast<size_t>(bufLen),
                  "%c%02d*%02d:%02d", sign, d, m, s);
}

const char* LibraryHandler::typeName(uint8_t type) {
    if (type < LIB_TYPE_COUNT) return LIB_TYPE_NAMES[type];
    return "UNK";
}

uint8_t LibraryHandler::parseType(const char* str) {
    for (int i = 0; i < LIB_TYPE_COUNT; ++i) {
        if (std::strcmp(str, LIB_TYPE_NAMES[i]) == 0)
            return static_cast<uint8_t>(i);
    }
    return LIB_UNK;
}
