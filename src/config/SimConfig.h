#pragma once
// SimConfig.h — Result of parsing a Config.h file at runtime.
// All fields have safe defaults representing a minimal (no-feature) configuration.

#include <cstdint>
#include <string>

// Token sentinel values — mirror Constants.h
static constexpr int TOKEN_OFF  = -1;
static constexpr int TOKEN_ON   = -2;
static constexpr int TOKEN_HIGH = -3;
static constexpr int TOKEN_LOW  = -4;
static constexpr int TOKEN_AUTO = -5;

// Mount type numeric values — mirror Constants.h
static constexpr int MOUNT_GEM       = 1;
static constexpr int MOUNT_FORK      = 2;
static constexpr int MOUNT_ALTAZM    = 3;
static constexpr int MOUNT_ALTALT    = 4;
static constexpr int MOUNT_GEM_TA    = 5;
static constexpr int MOUNT_GEM_TAC   = 6;
static constexpr int MOUNT_FORK_TA   = 7;
static constexpr int MOUNT_FORK_TAC  = 8;
static constexpr int MOUNT_ALTAZM_UNL = 9;

// Aux feature purpose values — mirror Constants.h
static constexpr int FEAT_OFF               = -1;
static constexpr int FEAT_SWITCH            =  1;
static constexpr int FEAT_ANALOG_OUTPUT     =  2;
static constexpr int FEAT_DEW_HEATER        =  3;
static constexpr int FEAT_INTERVALOMETER    =  4;
static constexpr int FEAT_MOMENTARY_SWITCH  =  5;
static constexpr int FEAT_HIDDEN_SWITCH     =  6;
static constexpr int FEAT_COVER_SWITCH      =  7;

// Limit sense — stored as a string when the value is a named symbol
// (e.g. "LIMIT_SENSE", "HIGH", "LOW", "OFF")
struct LimitSense {
    std::string symbol;   // raw token as found in config, e.g. "LOW", "LIMIT_SENSE", "OFF"
    bool isOff() const { return symbol == "OFF" || symbol.empty(); }
};

struct SimConfig {
    // -----------------------------------------------------------------------
    // Firmware identity (fixed for simulator — represents v10.24c)
    // -----------------------------------------------------------------------
    char firmwareName[16]    = "On-Step";
    char firmwareVersion[16] = "10.24c";
    char firmwareDate[24]    = "Jan  1 2024";
    char firmwareTime[12]    = "00:00:00";
    char firmwareHardware[24]= "Simulated";
    char configName[40]      = "";        // from HOST_NAME define

    // -----------------------------------------------------------------------
    // Mount
    // -----------------------------------------------------------------------
    bool  hasMount       = false;   // true when AXIS1 and AXIS2 driver models != OFF
    int   mountType      = MOUNT_GEM; // GEM/FORK/ALTAZM/etc. — only valid if hasMount
    bool  hasGoto        = false;   // GOTO_FEATURE == ON (-2)
    bool  hasPec         = false;   // PEC_STEPS_PER_WORM_ROTATION != 0
    long  pecStepsPerWorm = 0;
    bool  hasHomeSense   = false;   // AXIS1_SENSE_HOME != OFF
    bool  hasPPS         = false;   // TIME_LOCATION_PPS_SENSE != OFF
    bool  hasBinaryStatus = true;   // always true for v10+
    bool  hasDUT1        = true;    // always supported

    // Sound
    bool  soundEnabled   = false;   // STATUS_BUZZER_DEFAULT == ON

    // -----------------------------------------------------------------------
    // Axis numeric parameters (indices 0-8, axis n = index n-1)
    // -----------------------------------------------------------------------
    double stepsPerDegree[9]   = {12800.0, 12800.0, 64.0, 0, 0, 0, 0, 0, 0};
    double limitMin[9]         = {-180.0, -90.0, 0.0, 0, 0, 0, 0, 0, 0};
    double limitMax[9]         = { 180.0,  90.0, 360.0, 50, 50, 50, 50, 50, 50};
    double slewRateBaseDesired  = 1.0;   // deg/s — SLEW_RATE_BASE_DESIRED

    // Per-focuser (axis 4-9 = focuser index 0-5)
    double stepsPerMicron[6]   = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
    double focuserSlewRateBase[6] = {500.0, 500.0, 500.0, 500.0, 500.0, 500.0}; // um/s

    // Limit sense strings (axis 1..9, index 0..8)
    LimitSense senseHome[9];
    LimitSense senseLimitMin[9];
    LimitSense senseLimitMax[9];

    // -----------------------------------------------------------------------
    // Rotator (Axis 3)
    // -----------------------------------------------------------------------
    bool   hasRotator    = false;   // AXIS3_DRIVER_MODEL != OFF
    bool   hasDerotator  = false;   // hasRotator && mountType is ALTAZM/ALTAZM_UNL

    // -----------------------------------------------------------------------
    // Focusers (Axis 4-9)
    // -----------------------------------------------------------------------
    int    numFocusers   = 0;       // count of contiguous non-OFF AXIS4..9 driver models
    bool   hasFocuserTemp = false;  // FOCUSER_TEMPERATURE != OFF

    // -----------------------------------------------------------------------
    // Aux features (8 slots, 0-indexed; slot 0 = FEATURE1)
    // -----------------------------------------------------------------------
    int    featurePurpose[8] = {-1,-1,-1,-1,-1,-1,-1,-1}; // FEAT_OFF or 1..7
    char   featureName[8][11] = {                          // max 10 chars + NUL
        "FEATURE1","FEATURE2","FEATURE3","FEATURE4",
        "FEATURE5","FEATURE6","FEATURE7","FEATURE8"
    };

    // -----------------------------------------------------------------------
    // Weather
    // -----------------------------------------------------------------------
    bool   hasWeather      = false; // WEATHER != OFF
    bool   hasWeatherWrite = true;  // always true when hasWeather

    // MCU temp — always enabled in simulator
    bool   hasMcuTemp = true;

    // -----------------------------------------------------------------------
    // Site / time defaults (runtime-settable via :S* commands)
    // -----------------------------------------------------------------------
    double latitude    = 51.5;
    double longitude   = 0.0;
    double timezone    = 0.0;
    double elevation   = 100.0;

    // -----------------------------------------------------------------------
    // Limits defaults (runtime-adjustable)
    // -----------------------------------------------------------------------
    double horizonLimitMin    = -10.0;
    double horizonLimitMax    =  90.0;
    double meridianLimitEDeg  =   0.0;
    double meridianLimitWDeg  =   0.0;

    // -----------------------------------------------------------------------
    // Helper predicates
    // -----------------------------------------------------------------------

    // Returns true if this mount type is equatorial (has pier side concept)
    bool isEquatorial() const {
        return mountType == MOUNT_GEM    || mountType == MOUNT_FORK   ||
               mountType == MOUNT_GEM_TA || mountType == MOUNT_GEM_TAC ||
               mountType == MOUNT_FORK_TA || mountType == MOUNT_FORK_TAC;
    }

    // Returns true if this mount type is alt/az
    bool isAltAz() const {
        return mountType == MOUNT_ALTAZM || mountType == MOUNT_ALTAZM_UNL ||
               mountType == MOUNT_ALTALT;
    }

    // Phase 18: GEM mounts support meridian flips; FORK/ALTAZM do not.
    // Mirrors firmware's transform.meridianFlips (set in Transform.cpp).
    bool meridianFlipsEnabled() const {
        return mountType == MOUNT_GEM    ||
               mountType == MOUNT_GEM_TA ||
               mountType == MOUNT_GEM_TAC;
    }

    // Number of active features
    int activeFeatureCount() const {
        int n = 0;
        for (int i = 0; i < 8; ++i) if (featurePurpose[i] != FEAT_OFF) ++n;
        return n;
    }

    // True if any feature slot is active
    bool hasFeatures() const { return activeFeatureCount() > 0; }
};
