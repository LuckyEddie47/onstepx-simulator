// SiteHandler.cpp — Site and time command handler
// Source reference: Site.command.cpp

#include "SiteHandler.h"
#include "lib/CoordFormat.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr double PI = 3.14159265358979323846;

static double wrapHours(double h) {
    while (h >= 24.0) h -= 24.0;
    while (h <   0.0) h += 24.0;
    return h;
}

static double wrapAmPm(double h) {
    // Range [0, 12)
    while (h >= 12.0) h -= 12.0;
    while (h <   0.0) h += 12.0;
    return h;
}

// GMST in hours (matches SimClock::gmst)
static double gmst(double utcHours, int y, int m, int d) {
    auto julianDay = [](int yr, int mo, int da) -> double {
        int a = (14 - mo) / 12;
        int yy = yr + 4800 - a;
        int mm = mo + 12 * a - 3;
        return da + (153*mm + 2)/5 + 365*yy + yy/4 - yy/100 + yy/400 - 32045.0;
    };
    double jd = julianDay(y, m, d) + utcHours / 24.0;
    double t  = (jd - 2451545.0) / 36525.0;
    double gmstDeg = 280.46061837
                   + 360.98564736629 * (jd - 2451545.0)
                   + 0.000387933 * t * t
                   - t * t * t / 38710000.0;
    double h = gmstDeg / 15.0;
    while (h >= 24.0) h -= 24.0;
    while (h <   0.0) h += 24.0;
    return h;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

double SiteHandler::getLST() const {
    double g = gmst(m_state->utcHours,
                    m_state->utcDate.y,
                    m_state->utcDate.m,
                    m_state->utcDate.d);
    double lon = m_state->sites[m_state->currentSite].longitude; // degrees, east positive
    return wrapHours(g + lon / 15.0);
}

double SiteHandler::getLocalTime() const {
    return wrapHours(m_state->utcHours -
                     m_state->sites[m_state->currentSite].timezone);
}

// Format hours as HH:MM:SS or HH:MM:SS.SSSS (highPrec)
// Phase 9: delegates to the shared coordformat:: utility (src/lib/CoordFormat.h),
// which replicates firmware's Convert::doubleToHms/doubleToDms exactly,
// including rounding-carry behaviour this hand-rolled version previously
// lacked. See MountHandler.cpp's equivalent comment for the verified
// failing cases this fixes.
void SiteHandler::doubleToHms(char* buf, double hours, bool showSign, bool highPrec) const {
    coordformat::doubleToHms(buf, hours, showSign,
        highPrec ? CoordPrecision::Highest : CoordPrecision::High);
}

// Format degrees as sDD*MM:SS or sDD*MM:SS.SSS (highPrec)
// azimuth=true: DDD*MM:SS (no sign, 3-digit degrees) — N/A for any current
// SiteHandler call site (azimuth lives in MountHandler), kept for API
// compatibility with existing callers in this file.
//
// Phase 9 correction: the non-highPrec branch previously produced only
// "sDD*MM" / "DDD*MM" (2-field, no seconds) for :Gt#/:Gg# — but firmware's
// real call for both uses `precisionMode`, which is only ever PM_HIGH
// (3-field, sDD*MM:SS) or PM_HIGHEST (with 'H' suffix), never PM_LOW.
// Verified against Site.command.cpp directly. The 2-field form is now only
// reachable via an explicit PM_LOW caller (see :GG# below), not as the
// default for lat/long.
void SiteHandler::doubleToDms(char* buf, double deg, bool showSign,
                               bool azimuth, bool highPrec) const {
    coordformat::doubleToDms(buf, deg, azimuth, showSign,
        highPrec ? CoordPrecision::Highest : CoordPrecision::High);
}

// Parse MM/DD/YY or MM/DD/YYYY
bool SiteHandler::parseDate(const char* p, int* y, int* m, int* d) const {
    int mo, da, yr;
    if (std::sscanf(p, "%d/%d/%d", &mo, &da, &yr) != 3) return false;
    if (yr < 100) yr += 2000;
    *m = mo; *d = da; *y = yr;
    return (mo >= 1 && mo <= 12 && da >= 1 && da <= 31);
}

// Parse HH:MM:SS or HH:MM:SS.SSS
bool SiteHandler::parseTime(const char* p, double* hours) const {
    int h, mi;
    double s = 0.0;
    if (std::sscanf(p, "%d:%d:%lf", &h, &mi, &s) < 2) return false;
    *hours = h + mi / 60.0 + s / 3600.0;
    return true;
}

// Parse sDD*MM or sDD*MM:SS or sDD*MM:SS.SSS
// latitude=true: signed; false: unsigned (longitude handled separately)
bool SiteHandler::parseDms(const char* p, double* deg, bool latitude) const {
    bool neg = false;
    const char* s = p;
    if (*s == '-') { neg = true; ++s; }
    else if (*s == '+') { ++s; }

    int d, m;
    double sec = 0.0;
    // Accept both '*' and ':' as DMS separators
    char buf[64];
    std::strncpy(buf, s, 63); buf[63] = '\0';
    // Replace '*' with ':'
    for (char* c = buf; *c; ++c) if (*c == '*') *c = ':';

    int n = std::sscanf(buf, "%d:%d:%lf", &d, &m, &sec);
    if (n < 2) return false;
    *deg = d + m / 60.0 + sec / 3600.0;
    if (neg) *deg = -(*deg);

    (void)latitude;
    return true;
}

// Parse timezone: [s]HH or [s]HH:MM (MM = 00, 30, 45)
bool SiteHandler::parseTz(const char* p, double* hours) const {
    bool neg = false;
    const char* s = p;
    if (*s == '-') { neg = true; ++s; }
    else if (*s == '+') { ++s; }

    int h = 0, m = 0;
    std::sscanf(s, "%d:%d", &h, &m);
    *hours = h + m / 60.0;
    if (neg) *hours = -(*hours);
    return true;
}

// ---------------------------------------------------------------------------
// handle()
// ---------------------------------------------------------------------------

bool SiteHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)suppressFrame;

    // -----------------------------------------------------------------------
    // G — Get commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G') {

        // :Ga# — 12-hour local time HH:MM:SS#
        if (cmd[1] == 'a' && param[0] == '\0') {
            double t = wrapAmPm(getLocalTime());
            doubleToHms(reply, t, false, false);
            *numericReply = false;
            return true;
        }

        // :GC# — local date MM/DD/YY#
        if (cmd[1] == 'C' && param[0] == '\0') {
            // Local date = UTC date adjusted for timezone crossing
            double localH = m_state->utcHours -
                            m_state->sites[m_state->currentSite].timezone;
            int y = m_state->utcDate.y;
            int m = m_state->utcDate.m;
            int d = m_state->utcDate.d;
            if (localH < 0) { d--; if (d < 1) { m--; if (m<1){m=12;y--;} d=28; } }
            if (localH >= 24) { d++; } // approximate
            std::sprintf(reply, "%02d/%02d/%02d", m, d, y % 100);
            *numericReply = false;
            return true;
        }

        // :Gc# — always "24" (24-hour format)
        if (cmd[1] == 'c' && param[0] == '\0') {
            std::strcpy(reply, "24");
            *numericReply = false;
            return true;
        }

        // :GG# — UTC offset [s]HH:MM#
        // Firmware: doubleToHms(reply, location.timezone, true, PM_LOWEST) —
        // verified directly against Site.command.cpp. Previously this called
        // the local (now-removed) PM_HIGH-only doubleToHms and then
        // hand-trimmed the seconds field off by re-deriving h/m from
        // scratch — a second, independent rounding implementation. Now
        // uses the shared utility's actual PM_LOWEST mode directly.
        if (cmd[1] == 'G' && param[0] == '\0') {
            double tz = m_state->sites[m_state->currentSite].timezone;
            coordformat::doubleToHms(reply, tz, true, CoordPrecision::Lowest);
            *numericReply = false;
            return true;
        }

        // :Gg# — longitude sDDD*MM#
        // :GgH# — longitude sDD*MM:SS.SSS# (high precision)
        if (cmd[1] == 'g' && (param[0] == '\0' || param[1] == '\0')) {
            if (param[0] != '\0' && param[0] != 'H') {
                *error = CE_PARAM_FORM; return true;
            }
            bool hiPrec = (param[0] == 'H');
            double lon = m_state->sites[m_state->currentSite].longitude;
            // East is negative per LX200 convention
            doubleToDms(reply, -lon, true, false, hiPrec);
            *numericReply = false;
            return true;
        }

        // :GL# — local time HH:MM:SS#
        // :GLH# — high precision HH:MM:SS.SSSS#
        if (cmd[1] == 'L' && (param[0] == '\0' || param[1] == '\0')) {
            if (param[0] != '\0' && param[0] != 'H') {
                *error = CE_PARAM_FORM; return true;
            }
            bool hiPrec = (param[0] == 'H');
            doubleToHms(reply, getLocalTime(), false, hiPrec);
            *numericReply = false;
            return true;
        }

        // :GM# :GN# :GO# :GP# — site names 1-4
        if ((cmd[1]=='M'||cmd[1]=='N'||cmd[1]=='O'||cmd[1]=='P') && param[0]=='\0') {
            int idx = cmd[1] - 'M';
            const char* name = m_state->sites[idx].name;
            std::strcpy(reply, (name[0] == '\0') ? "None" : name);
            *numericReply = false;
            return true;
        }

        // :GS# — LST HH:MM:SS#
        // :GSH# — high precision
        if (cmd[1] == 'S' && (param[0] == '\0' || param[1] == '\0')) {
            if (param[0] != '\0' && param[0] != 'H') {
                *error = CE_PARAM_FORM; return true;
            }
            bool hiPrec = (param[0] == 'H');
            doubleToHms(reply, getLST(), false, hiPrec);
            *numericReply = false;
            return true;
        }

        // :Gt# — latitude sDD*MM#
        // :GtH# — high precision
        if (cmd[1] == 't' && (param[0] == '\0' || param[1] == '\0')) {
            if (param[0] != '\0' && param[0] != 'H') {
                *error = CE_PARAM_FORM; return true;
            }
            bool hiPrec = (param[0] == 'H');
            double lat = m_state->sites[m_state->currentSite].latitude;
            doubleToDms(reply, lat, true, false, hiPrec);
            *numericReply = false;
            return true;
        }

        // :Gv# — elevation in meters
        if (cmd[1] == 'v' && param[0] == '\0') {
            std::sprintf(reply, "%3.1f",
                         m_state->sites[m_state->currentSite].elevation);
            *numericReply = false;
            return true;
        }

        // :GX8n# — UTC time/date/ready
        if (cmd[1] == 'X' && param[0] == '8' && param[2] == '\0') {
            *numericReply = false;

            // :GX80# — UT1 time HH:MM:SS#
            // Phase 9 correction: previously called doubleToHms with
            // highPrec=true unconditionally, producing a PM_HIGHEST-style
            // 4-decimal field ("HH:MM:SS.ssss"). Verified directly against
            // Site.command.cpp: firmware calls
            // convert.doubleToHms(reply, rangeHours(getTime()), false, PM_HIGH)
            // — no decimal field at all, no 'H'-suffix escalation exists
            // for this command. The doc comment in firmware's own source
            // ("HH:MM:SS.ss#") is stale relative to its actual PM_HIGH call.
            if (param[1] == '0') {
                doubleToHms(reply, m_state->utcHours, false, false);
                return true;
            }

            // :GX81# — UT1 date MM/DD/YY#
            if (param[1] == '1') {
                std::sprintf(reply, "%02d/%02d/%02d",
                             m_state->utcDate.m,
                             m_state->utcDate.d,
                             m_state->utcDate.y % 100);
                return true;
            }

            // :GX89# — date/time ready: CE_0=ready, CE_1=not ready
            if (param[1] == '9') {
                if (m_state->dateReady && m_state->timeReady)
                    *error = CE_0;
                else
                    *error = CE_1;
                return true;
            }

            return false;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // S — Set commands
    // -----------------------------------------------------------------------
    if (cmd[0] == 'S') {
        *numericReply = true;

        // :SC[MM/DD/YY]# or :SC[MM/DD/YYYY]#
        if (cmd[1] == 'C') {
            int y, m, d;
            if (parseDate(param, &y, &m, &d)) {
                m_state->utcDate.y = y;
                m_state->utcDate.m = m;
                m_state->utcDate.d = d;
                m_state->dateReady = true;
            } else {
                *error = CE_PARAM_FORM;
            }
            return true;
        }

        // :SG[sHH]# or :SG[sHH:MM]#
        if (cmd[1] == 'G') {
            double tz;
            if (parseTz(param, &tz)) {
                if (tz >= -13.75 && tz <= 12.0) {
                    m_state->sites[m_state->currentSite].timezone = tz;
                    m_state->utcOffset = tz;
                } else {
                    *error = CE_PARAM_RANGE;
                }
            } else {
                *error = CE_PARAM_FORM;
            }
            return true;
        }

        // :Sg[(s)DDD*MM]# etc. — longitude, east negative per LX200
        if (cmd[1] == 'g') {
            double degs;
            bool neg = (param[0] == '-');
            const char* p2 = (param[0]=='-'||param[0]=='+') ? param+1 : param;
            if (parseDms(p2, &degs, false)) {
                if (degs >= -180.0 && degs <= 360.0) {
                    if (degs >= 180.0) degs -= 360.0;
                    if (neg) degs = -degs;
                    m_state->sites[m_state->currentSite].longitude = degs;
                } else {
                    *error = CE_PARAM_RANGE;
                }
            } else {
                *error = CE_PARAM_FORM;
            }
            return true;
        }

        // :SL[HH:MM:SS]# or :SL[HH:MM:SS.SSS]#
        if (cmd[1] == 'L') {
            double hours;
            if (parseTime(param, &hours)) {
                // UTC = local time - timezone offset  (timezone is hours-ahead-of-UTC)
                double tz = m_state->sites[m_state->currentSite].timezone;
                m_state->utcHours  = wrapHours(hours - tz);
                m_state->timeReady = true;
            } else {
                *error = CE_PARAM_FORM;
            }
            return true;
        }

        // :SM# :SN# :SO# :SP# — site names
        if (cmd[1]=='M'||cmd[1]=='N'||cmd[1]=='O'||cmd[1]=='P') {
            int idx = cmd[1] - 'M';
            if (std::strlen(param) <= 15) {
                std::strncpy(m_state->sites[idx].name, param, 15);
                m_state->sites[idx].name[15] = '\0';
            } else {
                *error = CE_PARAM_RANGE;
            }
            return true;
        }

        // :St[sDD*MM]# etc. — latitude
        if (cmd[1] == 't') {
            double degs;
            if (parseDms(param, &degs, true)) {
                if (degs >= -90.0 && degs <= 90.0) {
                    m_state->sites[m_state->currentSite].latitude = degs;
                } else {
                    *error = CE_PARAM_RANGE;
                }
            } else {
                *error = CE_PARAM_FORM;
            }
            return true;
        }

        // :SU[s.s]# — DUT1 (accept, no-op in sim)
        if (cmd[1] == 'U') {
            char* end;
            double dut1 = std::strtod(param, &end);
            if (end == param || dut1 < -0.9 || dut1 > 0.9) {
                *error = CE_PARAM_FORM;
            }
            // DUT1 accepted and ignored — sim uses UTC = UT1
            return true;
        }

        // :Sv[sn.n]# — elevation in meters
        if (cmd[1] == 'v') {
            char* end;
            double elev = std::strtod(param, &end);
            if (end == param) {
                *error = CE_PARAM_RANGE;
            } else {
                m_state->sites[m_state->currentSite].elevation = elev;
            }
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // W — Site selection
    // -----------------------------------------------------------------------
    if (cmd[0] == 'W') {
        // :W[0-3]# — switch site
        if (cmd[1] >= '0' && cmd[1] <= '3' && param[0] == '\0') {
            m_state->currentSite = cmd[1] - '0';
            *numericReply  = false;
            *suppressFrame = true;   // Phase 12B: firmware returns nothing
            return true;
        }

        // :W?# — query current site index
        if (cmd[1] == '?' && param[0] == '\0') {
            std::sprintf(reply, "%d", m_state->currentSite);
            *numericReply = false;
            return true;
        }

        return false;
    }

    return false;
}
