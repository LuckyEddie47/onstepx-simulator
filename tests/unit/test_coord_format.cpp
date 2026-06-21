// test_coord_format.cpp — Unit tests for the shared coordformat:: utility
// (src/lib/CoordFormat.h), introduced in Phase 9.
//
// These tests are config-independent (the utility has no SimState/SimConfig
// dependency) and verify behaviour against values derived directly from
// firmware's Convert::doubleToHms/doubleToDms algorithm, cross-checked
// numerically during the Phase 9 audit. See Phase9_Decision_Log.md for the
// full derivation of each expected value.

#include <gtest/gtest.h>
#include "lib/CoordFormat.h"

#include <cstring>

// ---------------------------------------------------------------------------
// doubleToHms — rounding-carry correctness (the original bug)
// ---------------------------------------------------------------------------
// These specific inputs were confirmed during the audit to produce
// impossible output ("HH:59:60.0000"-style) under the old, independently
// hand-rolled implementations. They must now roll over correctly.

TEST(CoordFormatHms, HighestPrecisionCarriesIntoHour) {
    char buf[32];
    coordformat::doubleToHms(buf, 12.999999999, false, CoordPrecision::Highest);
    EXPECT_STREQ(buf, "13:00:00.0000");
}

TEST(CoordFormatHms, HighestPrecisionCarriesPastMidnight) {
    char buf[32];
    coordformat::doubleToHms(buf, 23.9999999999, false, CoordPrecision::Highest);
    EXPECT_STREQ(buf, "24:00:00.0000");
}

TEST(CoordFormatHms, HighestPrecisionNoCarryUnaffected) {
    char buf[32];
    coordformat::doubleToHms(buf, 6.123456789, false, CoordPrecision::Highest);
    EXPECT_STREQ(buf, "06:07:24.4444");
}

TEST(CoordFormatHms, HighPrecisionRoundsNotTruncates) {
    // Firmware adds a ~0.5s rounding offset before truncating; a naive
    // truncate-only implementation would show 12:59:59 here.
    char buf[32];
    coordformat::doubleToHms(buf, 12.99999999999, false, CoordPrecision::High);
    EXPECT_STREQ(buf, "13:00:00");
}

TEST(CoordFormatHms, NeverProducesSecondsOrMinutesOf60) {
    // Property-style sweep across many fractional-boundary-adjacent values.
    char buf[32];
    for (double frac = 0.0; frac < 1.0; frac += 0.0001) {
        double hours = 5.0 + frac / 3600.0 * 3599.9999; // sweep near each second boundary
        coordformat::doubleToHms(buf, hours, false, CoordPrecision::Highest);
        ASSERT_EQ(std::strstr(buf, ":60"), nullptr)
            << "Impossible seconds=60 in output: " << buf << " (input hours=" << hours << ")";
        ASSERT_EQ(std::strstr(buf, "60:"), nullptr)
            << "Impossible minutes=60 in output: " << buf << " (input hours=" << hours << ")";
    }
}

// ---------------------------------------------------------------------------
// doubleToHms — precision modes
// ---------------------------------------------------------------------------

TEST(CoordFormatHms, LowestModeNoSeconds) {
    char buf[32];
    coordformat::doubleToHms(buf, 23.9999999999, false, CoordPrecision::Lowest);
    EXPECT_STREQ(buf, "24:00");
}

TEST(CoordFormatHms, LowModeTenthsOfMinute) {
    char buf[32];
    coordformat::doubleToHms(buf, 6.123456789, false, CoordPrecision::Low);
    EXPECT_STREQ(buf, "06:07.4");
}

TEST(CoordFormatHms, SignPresentPositive) {
    char buf[32];
    coordformat::doubleToHms(buf, 5.5, true, CoordPrecision::Lowest);
    EXPECT_EQ(buf[0], '+');
}

TEST(CoordFormatHms, SignPresentNegative) {
    char buf[32];
    coordformat::doubleToHms(buf, -5.0, true, CoordPrecision::Lowest);
    EXPECT_EQ(buf[0], '-');
    EXPECT_STREQ(buf, "-05:00");
}

TEST(CoordFormatHms, NoSignPresentOmitsSignCharacter) {
    char buf[32];
    coordformat::doubleToHms(buf, 6.5, false, CoordPrecision::High);
    EXPECT_NE(buf[0], '+');
    EXPECT_NE(buf[0], '-');
    EXPECT_EQ(buf[0], '0'); // starts directly with the hour digits
}

// ---------------------------------------------------------------------------
// doubleToDms — rounding-carry correctness (the original bug)
// ---------------------------------------------------------------------------

TEST(CoordFormatDms, HighestPrecisionCarriesIntoDegree) {
    char buf[32];
    coordformat::doubleToDms(buf, 12.999999999, false, true, CoordPrecision::Highest);
    EXPECT_STREQ(buf, "+13*00:00.000");
}

TEST(CoordFormatDms, HighestPrecisionCarriesAt90) {
    char buf[32];
    coordformat::doubleToDms(buf, 89.9999999999, false, true, CoordPrecision::Highest);
    EXPECT_STREQ(buf, "+90*00:00.000");
}

TEST(CoordFormatDms, HighestPrecisionCarriesNegative) {
    char buf[32];
    coordformat::doubleToDms(buf, -45.9999999, false, true, CoordPrecision::Highest);
    EXPECT_STREQ(buf, "-46*00:00.000");
}

TEST(CoordFormatDms, NeverProducesMinutesOrSecondsOf60) {
    char buf[32];
    for (double frac = 0.0; frac < 1.0; frac += 0.0001) {
        double deg = 45.0 + frac;
        coordformat::doubleToDms(buf, deg, false, true, CoordPrecision::Highest);
        ASSERT_EQ(std::strstr(buf, ":60"), nullptr)
            << "Impossible seconds=60 in output: " << buf << " (input deg=" << deg << ")";
        ASSERT_EQ(std::strstr(buf, "*60"), nullptr)
            << "Impossible minutes=60 in output: " << buf << " (input deg=" << deg << ")";
    }
}

// ---------------------------------------------------------------------------
// doubleToDms — precision modes and fullRange
// ---------------------------------------------------------------------------

TEST(CoordFormatDms, LowModeNoSeconds) {
    char buf[32];
    coordformat::doubleToDms(buf, 45.5, false, true, CoordPrecision::Low);
    EXPECT_STREQ(buf, "+45*30");
}

TEST(CoordFormatDms, FullRangeThreeDigitDegree) {
    char buf[32];
    coordformat::doubleToDms(buf, 45.5, true, false, CoordPrecision::High);
    EXPECT_STREQ(buf, "045*30:00");
}

TEST(CoordFormatDms, NotFullRangeTwoDigitDegree) {
    char buf[32];
    coordformat::doubleToDms(buf, 45.5, false, true, CoordPrecision::High);
    EXPECT_STREQ(buf, "+45*30:00");
}

TEST(CoordFormatDms, NoSignOmitsSignCharacter) {
    char buf[32];
    coordformat::doubleToDms(buf, 45.5, true, false, CoordPrecision::High);
    EXPECT_NE(buf[0], '+');
    EXPECT_NE(buf[0], '-');
}

// ---------------------------------------------------------------------------
// Reference values cross-checked independently against a from-scratch
// Python re-derivation of firmware's algorithm during the Phase 9 audit
// (not just re-deriving the same C++ logic).
// ---------------------------------------------------------------------------

TEST(CoordFormatHms, KnownGoodValueOneThird) {
    char buf[32];
    coordformat::doubleToHms(buf, 1.0 / 3.0, false, CoordPrecision::Highest);
    EXPECT_STREQ(buf, "00:20:00.0000");
}

TEST(CoordFormatDms, KnownGoodValueSmallNegative) {
    char buf[32];
    coordformat::doubleToDms(buf, -0.00001, false, true, CoordPrecision::Highest);
    EXPECT_STREQ(buf, "-00*00:00.036");
}
