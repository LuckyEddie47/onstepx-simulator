#pragma once
// SiderealConstants.h — Shared sidereal-rate constants.
//
// Phase 8: extracted from SimClock.cpp so GuideHandler can convert a guide
// rate index (sidereal multiple) into deg/s using the exact same constants
// SimClock uses for tracking advancement, avoiding duplicated literals.

namespace sidereal {

// Mean sidereal rate in Hz, matching firmware's TRACK_BASE_DEFAULT-style
// reference value used elsewhere in this simulator (trackingRateHz default).
constexpr double HZ = 60.136;

// Mean sidereal angular rate: one full rotation (360 deg) per sidereal day
// (86164.0905 mean solar seconds).
constexpr double RATE_DEG_PER_SEC = 360.0 / 86164.0905;

} // namespace sidereal
