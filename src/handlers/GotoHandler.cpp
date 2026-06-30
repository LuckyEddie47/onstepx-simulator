// GotoHandler.cpp — Goto, sync, alignment, and target command handler.
//
// Protocol source: Goto.command.cpp
// All coordinate values stored in SimState in decimal degrees (RA in hours).

#include "handlers/GotoHandler.h"
#include "lib/CoordFormat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// handle()
// ---------------------------------------------------------------------------

bool GotoHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    // -----------------------------------------------------------------------
    // A - Alignment commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'A') {

        // :AW# — Align Write to EEPROM (no-op in sim, always succeeds)
        if (cmd[1] == 'W' && param[0] == '\0') {
            return true;
        }

        // :A?# — Get alignment status  "mno#"
        // m = max stars, n = stars done so far, o = stars expected
        if (cmd[1] == '?' && param[0] == '\0') {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            // Max stars: firmware uses a compile-time constant; sim uses 3 as a
            // safe default when the config doesn't specify.
            int maxStars = 3;
            reply[0] = static_cast<char>('0' + maxStars);
            reply[1] = static_cast<char>('0' + m_state->alignDoneCount);
            reply[2] = static_cast<char>('0' + m_state->alignExpected);
            reply[3] = '\0';
            *numericReply = false;
            return true;
        }

        // :A[n]# — Start n-star alignment (n = '1'..'9')
        if (cmd[1] >= '1' && cmd[1] <= '9' && param[0] == '\0') {
            int n = cmd[1] - '0';
            if (n < 1 || n > 9) {
                *error = CE_PARAM_RANGE;
                return true;
            }
            {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->alignExpected  = n;
                m_state->alignDoneCount = 0;
                m_state->alignDone      = false;
            }
            m_msm->resetHome();
            m_msm->startTracking();
            return true;
        }

        // :A+# — Accept current alignment star
        if (cmd[1] == '+' && param[0] == '\0') {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            if (m_state->alignExpected == 0 || m_state->alignDone) {
                *error = CE_ALIGN_NOT_ACTIVE;
                return true;
            }
            m_state->alignDoneCount++;
            if (m_state->alignDoneCount >= m_state->alignExpected) {
                m_state->alignDone      = true;
                m_state->alignExpected  = 0;
                m_state->startupTrusted = true;
            }
            return true;
        }

        *error = CE_CMD_UNKNOWN;
        return true;
    }

    // -----------------------------------------------------------------------
    // C - Sync commands  :CS#  :CM#
    // -----------------------------------------------------------------------
    if (cmd[0] == 'C' && (cmd[1] == 'S' || cmd[1] == 'M') && param[0] == '\0') {
        *numericReply = false;

        // Phase 11: if an alignment sequence is currently active (at least
        // one :A[n]# was issued and it isn't complete yet), firmware routes
        // :CS#/:CM# to Goto::alignAddStar(sync=true) instead of a normal
        // sync — verified directly against Goto.command.cpp:107-116. This
        // matches firmware's documented LX200 behaviour where the sync
        // commands during alignment register a star rather than updating
        // the position. Reply for :CM# is "N/A" on success, same as a
        // normal sync (firmware's alignAddStar returns CE_NONE on success,
        // and the :CM# branch below writes "N/A" for CE_NONE regardless).
        if (alignActive()) {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->alignDoneCount++;
            if (m_state->alignDoneCount >= m_state->alignExpected) {
                m_state->alignDone      = true;
                m_state->alignExpected  = 0;
                m_state->startupTrusted = true;
            }
            if (cmd[1] == 'M') std::strcpy(reply, "N/A");
            return true;
        }

        CommandError e = m_msm->syncToTarget();
        if (e == CE_NONE) {
            // Phase 17: firmware Goto.cpp:232 calls limits.enabled(true) after sync.
            std::lock_guard<std::mutex> lk(m_state->mutex);
            if (m_state->startupTrusted && m_state->dateReady && m_state->timeReady)
                m_state->limitsEnabled = true;
        }
        if (cmd[1] == 'M') {
            if (e == CE_NONE) {
                std::strcpy(reply, "N/A");
            } else if (e >= CE_SLEW_ERR_BELOW_HORIZON && e <= CE_SLEW_ERR_UNSPECIFIED) {
                reply[0] = 'E';
                reply[1] = static_cast<char>((e - CE_SLEW_ERR_BELOW_HORIZON) + '1');
                reply[2] = '\0';
            } else {
                std::strcpy(reply, "E9");
            }
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // D# — Distance bars: '\x7f#' if slewing, else '#'
    // -----------------------------------------------------------------------
    if (cmd[0] == 'D' && cmd[1] == '\0') {
        *numericReply = false;
        MountState ms;
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            ms = m_state->mountState;
        }
        if (ms == MountState::SLEWING_GOTO) {
            reply[0] = static_cast<char>(127);
            reply[1] = '\0';
        } else {
            reply[0] = '#';
            reply[1] = '\0';
            *suppressFrame = true;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // G - Get target coordinates and settings
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G') {

        // :Gr[H]# — Get target RA
        if (cmd[1] == 'r' && (param[0] == '\0' || param[1] == '\0')) {
            if (param[0] != '\0' && param[0] != 'H' && param[0] != 'a') {
                *error = CE_PARAM_FORM;
                return true;
            }
            bool highest = (param[0] == 'H' || param[0] == 'a');
            double ra;
            { std::lock_guard<std::mutex> lk(m_state->mutex); ra = m_state->targetRA; }
            formatHMS(reply, ra, highest);
            *numericReply = false;
            return true;
        }

        // :Gd[H]# — Get target Dec
        if (cmd[1] == 'd' && (param[0] == '\0' || param[1] == '\0')) {
            if (param[0] != '\0' && param[0] != 'H' && param[0] != 'e') {
                *error = CE_PARAM_FORM;
                return true;
            }
            bool highest = (param[0] == 'H' || param[0] == 'e');
            double dec;
            { std::lock_guard<std::mutex> lk(m_state->mutex); dec = m_state->targetDec; }
            formatDMS(reply, dec, true, highest);
            *numericReply = false;
            return true;
        }

        // :GX9[n]# — Get goto settings
        if (cmd[1] == 'X' && param[0] == '9' && param[2] == '\0') {
            *numericReply = false;
            std::lock_guard<std::mutex> lk(m_state->mutex);
            switch (param[1]) {
                case '4': // pier side: 0=None,1=East,2=West
                    if (m_cfg->isEquatorial()) {
                        std::snprintf(reply, 256, "%d", static_cast<int>(m_state->pierSide));
                    } else {
                        std::snprintf(reply, 256, "%d N", static_cast<int>(m_state->pierSide));
                    }
                    break;
                case '5': // autoMeridianFlip
                    std::snprintf(reply, 256, "%d", m_state->autoFlipEnabled ? 1 : 0);
                    break;
                case '6': { // preferred pier side: E/W/B
                    const char* sides = "BEW"; // BEST=0, EAST=1, WEST=2
                    int idx = static_cast<int>(m_state->preferredPierSide);
                    reply[0] = (idx >= 0 && idx <= 2) ? sides[idx] : 'B';
                    reply[1] = '\0';
                    break;
                }
                case '7': // current slew rate in deg/s
                    std::snprintf(reply, 256, "%.1f", m_state->slewRateDegPerSec);
                    break;
                default:
                    return false;
            }
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // M - Goto commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'M') {

        // :MS# — Goto target object
        if (cmd[1] == 'S' && param[0] == '\0') {
            CommandError e = m_msm->beginGoto();
            reply[0] = (e == CE_NONE) ? '0' : slewErrorChar(e);
            reply[1] = '\0';
            *numericReply = false;
            *suppressFrame = true;
            *error = e;
            return true;
        }

        // :MA# — Goto Alt/Az target
        if (cmd[1] == 'A' && param[0] == '\0') {
            CommandError e = m_msm->beginGoto();
            reply[0] = (e == CE_NONE) ? '0' : slewErrorChar(e);
            reply[1] = '\0';
            *numericReply = false;
            *suppressFrame = true;
            *error = e;
            return true;
        }

        // :MD# — Get destination pier side (0=East,1=West,2=Unknown)
        if (cmd[1] == 'D' && param[0] == '\0') {
            *numericReply = false;
            *suppressFrame = true;
            std::lock_guard<std::mutex> lk(m_state->mutex);
            if (m_state->preferredPierSide == EAST)
                reply[0] = '0';
            else if (m_state->preferredPierSide == WEST)
                reply[0] = '1';
            else
                reply[0] = '2';
            reply[1] = '\0';
            return true;
        }

        // :MN[e|w]# — Goto specific pier side (equatorial + goto only)
        if (cmd[1] == 'N') {
            if (!m_cfg->isEquatorial() || !m_cfg->hasGoto) {
                *error = CE_CMD_UNKNOWN;
                return true;
            }
            if (param[0] == 'e' && param[1] == '\0') {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->preferredPierSide = EAST;
            } else if (param[0] == 'w' && param[1] == '\0') {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->preferredPierSide = WEST;
            } else if (param[0] != '\0') {
                *error = CE_CMD_UNKNOWN;
                return true;
            }
            CommandError e = m_msm->beginGoto();
            reply[0] = (e == CE_NONE) ? '0' : slewErrorChar(e);
            reply[1] = '\0';
            *numericReply = false;
            *suppressFrame = true;
            *error = e;
            return true;
        }

        // :MP# — Polar alignment goto (equatorial mounts only)
        if (cmd[1] == 'P' && param[0] == '\0') {
            if (!m_cfg->isEquatorial()) {
                *error = CE_CMD_UNKNOWN;
                return true;
            }
            CommandError e = m_msm->beginGoto();
            reply[0] = (e == CE_NONE) ? '0' : slewErrorChar(e);
            reply[1] = '\0';
            *numericReply = false;
            *suppressFrame = true;
            *error = e;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // S - Set target coordinates and settings
    // -----------------------------------------------------------------------
    if (cmd[0] == 'S') {

        // :Sr[HH:MM.T]# or :Sr[HH:MM:SS]# or :Sr[HH:MM:SS.SSSS]#
        if (cmd[1] == 'r') {
            double hours = 0.0;
            if (!parseHMS(param, hours)) {
                *error = CE_PARAM_RANGE;
                return true;
            }
            {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->targetRA    = hours;
                m_state->targetRASet = true;
            }
            return true;
        }

        // :Sd[sDD*MM]# or :Sd[sDD*MM:SS]# or :Sd[sDD*MM:SS.SSS]#
        if (cmd[1] == 'd') {
            double deg = 0.0;
            if (!parseDMS(param, deg, true)) {
                *error = CE_PARAM_RANGE;
                return true;
            }
            {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->targetDec    = deg;
                m_state->targetDecSet = true;
            }
            return true;
        }

        // :SX9[n],[v]# — Set goto settings
        if (cmd[1] == 'X' && param[0] == '9') {
            if (param[2] != ',') { *error = CE_PARAM_FORM; return true; }
            const char* val = &param[3];
            switch (param[1]) {
                case '2': { // set slew rate
                    double rate = std::atof(val);
                    if (rate > 0.0) {
                        std::lock_guard<std::mutex> lk(m_state->mutex);
                        m_state->slewRateDegPerSec = rate;
                    }
                    break;
                }
                case '3': { // slew rate preset 1..5
                    double factor = 1.0;
                    switch (val[0]) {
                        case '5': factor = 0.50;  break;
                        case '4': factor = 0.667; break;
                        case '3': factor = 1.00;  break;
                        case '2': factor = 1.50;  break;
                        case '1': factor = 2.00;  break;
                        default: *error = CE_PARAM_RANGE; return true;
                    }
                    {
                        std::lock_guard<std::mutex> lk(m_state->mutex);
                        m_state->slewRateDegPerSec = m_cfg->slewRateBaseDesired * factor;
                    }
                    *numericReply = false;
                    break;
                }
                case '5': { // autoMeridianFlip 0/1
                    if (!m_cfg->isEquatorial() || !m_cfg->hasGoto) {
                        *error = CE_CMD_UNKNOWN;
                        return true;
                    }
                    if (val[0] != '0' && val[0] != '1') { *error = CE_PARAM_RANGE; return true; }
                    std::lock_guard<std::mutex> lk(m_state->mutex);
                    m_state->autoFlipEnabled = (val[0] == '1');
                    break;
                }
                case '6': { // preferred pier side E/W/B
                    if (!m_cfg->isEquatorial() || !m_cfg->hasGoto) {
                        *error = CE_CMD_UNKNOWN;
                        return true;
                    }
                    std::lock_guard<std::mutex> lk(m_state->mutex);
                    switch (val[0]) {
                        case 'E': m_state->preferredPierSide = EAST; break;
                        case 'W': m_state->preferredPierSide = WEST; break;
                        case 'B': m_state->preferredPierSide = BEST; break;
                        default:  *error = CE_PARAM_RANGE; return true;
                    }
                    break;
                }
                case '8': { // pauseAtHome 0/1
                    if (!m_cfg->isEquatorial() || !m_cfg->hasGoto) {
                        *error = CE_CMD_UNKNOWN;
                        return true;
                    }
                    if (val[0] != '0' && val[0] != '1') { *error = CE_PARAM_RANGE; return true; }
                    std::lock_guard<std::mutex> lk(m_state->mutex);
                    m_state->pauseAtHomeEnabled = (val[0] == '1');
                    break;
                }
                case '9': { // continue if paused at home
                    if (!m_cfg->isEquatorial() || !m_cfg->hasGoto) {
                        *error = CE_CMD_UNKNOWN;
                        return true;
                    }
                    if (val[0] == '1') {
                        std::lock_guard<std::mutex> lk(m_state->mutex);
                        m_state->homePaused = false;
                    } else {
                        *error = CE_PARAM_RANGE;
                    }
                    break;
                }
                default:
                    return false;
            }
            return true;
        }

        return false;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Format helpers
// ---------------------------------------------------------------------------

// Phase 9: delegates to the shared coordformat:: utility (src/lib/CoordFormat.h),
// which replicates firmware's Convert::doubleToHms/doubleToDms exactly,
// including rounding-carry behaviour this hand-rolled version previously
// lacked. See MountHandler.cpp's equivalent comment for the verified
// failing cases this fixes.
void GotoHandler::formatHMS(char* buf, double hours, bool highest) {
    coordformat::doubleToHms(buf, hours, false,
        highest ? CoordPrecision::Highest : CoordPrecision::High);
}

void GotoHandler::formatDMS(char* buf, double deg, bool sign, bool highest) {
    coordformat::doubleToDms(buf, deg, !sign, sign,
        highest ? CoordPrecision::Highest : CoordPrecision::High);
}

bool GotoHandler::parseHMS(const char* s, double& hours) {
    int h = 0, m = 0;
    double sec = 0.0;
    if (std::sscanf(s, "%d:%d:%lf", &h, &m, &sec) == 3) {
        hours = h + m / 60.0 + sec / 3600.0;
        return (h >= 0 && h < 24 && m >= 0 && m < 60 && sec >= 0.0 && sec < 60.0);
    }
    double mf = 0.0;
    if (std::sscanf(s, "%d:%lf", &h, &mf) == 2) {
        hours = h + mf / 60.0;
        return (h >= 0 && h < 24 && mf >= 0.0 && mf < 60.0);
    }
    return false;
}

bool GotoHandler::parseDMS(const char* s, double& deg, bool sign) {
    char sgn = '+';
    const char* p = s;
    if (sign && (*p == '+' || *p == '-')) { sgn = *p; ++p; }
    char buf[64] = {};
    std::strncpy(buf, p, 63);
    for (char* c = buf; *c; ++c) if (*c == '*') *c = ' ';
    int d = 0, m = 0;
    double sec = 0.0;
    if (std::sscanf(buf, "%d %d:%lf", &d, &m, &sec) >= 2) {
        deg = d + m / 60.0 + sec / 3600.0;
        if (sgn == '-') deg = -deg;
        return (d >= 0 && d <= 90 && m >= 0 && m < 60 && sec >= 0.0 && sec < 60.0);
    }
    return false;
}

char GotoHandler::slewErrorChar(CommandError e) {
    if (e >= CE_SLEW_ERR_BELOW_HORIZON && e <= CE_SLEW_ERR_UNSPECIFIED)
        return static_cast<char>((e - CE_SLEW_ERR_BELOW_HORIZON) + '1');
    return '9';
}

bool GotoHandler::alignActive() const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return (m_state->alignExpected > 0 && !m_state->alignDone);
}
