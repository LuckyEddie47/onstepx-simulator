#pragma once
// CoordFormat.h — Shared HMS/DMS coordinate formatting, matching firmware
// Convert::doubleToHms / Convert::doubleToDms exactly (src/lib/convert/Convert.cpp).
//
// Phase 9: extracted because the same rounding-carry bug was found
// independently re-implemented (and independently broken) in
// MountHandler::formatRA/formatDec, SiteHandler::doubleToHms/doubleToDms,
// and GotoHandler::formatHMS/formatDMS. Firmware adds a small rounding
// offset to the *whole value* before splitting into h/m/s (or d/m/s), so a
// carry from rounding seconds propagates correctly into minutes/hours, even
// past midnight/90 degrees. The three prior implementations instead
// rounded only the seconds remainder independently (typically via
// "%07.4f"/"%06.3f"), which can produce impossible output like
// "12:59:60.0000" instead of "13:00:00.0000" whenever the fractional
// second happens to round up to 60.
//
// Verified against firmware source directly (not assumed):
//   - PM_HIGHEST: value += 0.0000000139 (hms) / 0.000000139 (dms) before
//     splitting; decimal computed separately as a *remainder*, not via an
//     independently-rounded float format specifier.
//   - PM_HIGH: value += 0.000139 before splitting; no decimal field.
//   - PM_LOW (hms): "HH:MM.T" — T = floor(seconds/6), i.e. tenths of a
//     minute, not seconds.
//   - PM_LOW (dms): "sDD*MM" / "DDD*MM" (fullRange) — no seconds field.
//   - PM_LOWEST (hms): "HH:MM" — no seconds field, no rounding offset.
//   - PM_LOWEST (dms): unreachable in firmware (Goto/Mount/Site never pass
//     it to doubleToDms); implemented here identically to PM_HIGH for
//     defensive correctness, matching the fallthrough firmware's own
//     untouched format-string would produce if it ever were reached.
//
// All functions write a NUL-terminated string into buf (caller-owned,
// must be at least 32 bytes — every produced string is well under that).

#include <cmath>
#include <cstdio>

enum class CoordPrecision { Lowest, Low, High, Highest };

namespace coordformat {

// Format hours (RA, time-of-day, timezone, etc.) as:
//   PM_LOWEST:  HH:MM
//   PM_LOW:     HH:MM.T            (T = tenths of a minute)
//   PM_HIGH:    HH:MM:SS
//   PM_HIGHEST: HH:MM:SS.SSSS
// signPresent: if true, prefixes '+'/'-'; hours is wrapped to [0,24) either
// way (matching firmware: doubleToHms never emits an hour >= 24 or < 0,
// regardless of signPresent — sign and magnitude are handled separately,
// same as firmware's `if (value<0){value=-value; sign="-";}`).
inline void doubleToHms(char* buf, double hours, bool signPresent, CoordPrecision p) {
    char sign[2] = "";
    double value = hours;
    if (signPresent) {
        if (value < 0.0) { value = -value; sign[0] = '-'; sign[1] = '\0'; }
        else             { sign[0] = '+';  sign[1] = '\0'; }
    } else if (value < 0.0) {
        value = -value; // firmware still takes abs() even without a sign char
    }

    if (p == CoordPrecision::Highest) value += 0.0000000139;
    else                              value += 0.000139;

    double hour    = std::floor(value);
    double minute  = (value - hour) * 60.0;
    double second  = (minute - std::floor(minute)) * 60.0;
    double decimal = 0.0;

    if (p == CoordPrecision::Highest) {
        decimal = (second - std::floor(second)) * 10000.0;
        std::sprintf(buf, "%s%02d:%02d:%02d.%04d",
                     sign, static_cast<int>(hour), static_cast<int>(minute),
                     static_cast<int>(second), static_cast<int>(decimal));
    } else if (p == CoordPrecision::High) {
        std::sprintf(buf, "%s%02d:%02d:%02d",
                     sign, static_cast<int>(hour), static_cast<int>(minute),
                     static_cast<int>(second));
    } else if (p == CoordPrecision::Low) {
        double tenthsOfMinute = second / 6.0;
        std::sprintf(buf, "%s%02d:%02d.%01d",
                     sign, static_cast<int>(hour), static_cast<int>(minute),
                     static_cast<int>(tenthsOfMinute));
    } else { // Lowest
        std::sprintf(buf, "%s%02d:%02d",
                     sign, static_cast<int>(hour), static_cast<int>(minute));
    }
}

// Format degrees (Dec, Alt, Az, Lat, Long) as:
//   PM_LOW:     sDD*MM   or  DDD*MM   (fullRange)
//   PM_HIGH:    sDD*MM:SS  or  DDD*MM:SS   (fullRange)
//   PM_HIGHEST: sDD*MM:SS.SSS  (fullRange has no effect at PM_HIGHEST in
//               firmware — the fullRange digit-width change to the degree
//               field still applies, but PM_HIGHEST itself doesn't change
//               behaviour based on fullRange beyond that field width)
//   PM_LOWEST:  unreachable in firmware; implemented identically to
//               PM_HIGH here (see file header).
// fullRange: degree field is %03d (0-360, for azimuth) instead of %02d
// (0-90, for declination/altitude/latitude).
// signPresent: if true, prefixes '+'/'-'.
inline void doubleToDms(char* buf, double deg, bool fullRange, bool signPresent,
                         CoordPrecision p) {
    char sign[2] = "";
    double value = deg;
    if (signPresent) {
        if (value < 0.0) { value = -value; sign[0] = '-'; sign[1] = '\0'; }
        else             { sign[0] = '+';  sign[1] = '\0'; }
    } else if (value < 0.0) {
        value = -value;
    }

    if (p == CoordPrecision::Highest) value += 0.000000139;
    else                              value += 0.000139;

    double degWhole = std::floor(value);
    double minute   = (value - degWhole) * 60.0;
    double second   = (minute - std::floor(minute)) * 60.0;
    double decimal  = 0.0;

    int degWidth = fullRange ? 3 : 2;

    if (p == CoordPrecision::Highest) {
        decimal = (second - std::floor(second)) * 1000.0;
        if (degWidth == 3)
            std::sprintf(buf, "%s%03d*%02d:%02d.%03d", sign, static_cast<int>(degWhole),
                         static_cast<int>(minute), static_cast<int>(second),
                         static_cast<int>(decimal));
        else
            std::sprintf(buf, "%s%02d*%02d:%02d.%03d", sign, static_cast<int>(degWhole),
                         static_cast<int>(minute), static_cast<int>(second),
                         static_cast<int>(decimal));
    } else if (p == CoordPrecision::Low) {
        if (degWidth == 3)
            std::sprintf(buf, "%s%03d*%02d", sign, static_cast<int>(degWhole),
                         static_cast<int>(minute));
        else
            std::sprintf(buf, "%s%02d*%02d", sign, static_cast<int>(degWhole),
                         static_cast<int>(minute));
    } else { // High or Lowest (Lowest is unreachable in firmware; same as High here)
        if (degWidth == 3)
            std::sprintf(buf, "%s%03d*%02d:%02d", sign, static_cast<int>(degWhole),
                         static_cast<int>(minute), static_cast<int>(second));
        else
            std::sprintf(buf, "%s%02d*%02d:%02d", sign, static_cast<int>(degWhole),
                         static_cast<int>(minute), static_cast<int>(second));
    }
}

} // namespace coordformat
