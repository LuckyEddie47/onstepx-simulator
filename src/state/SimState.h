#pragma once
// SimState.h — Central mutable simulator state.
//
// Phase 1: FirmwareHandler / Status stub fields.
// Phase 2: Mount, site/time, limits, PEC fields.
// Phase 3: Goto, park, home, guide, PEC state machines.
// Phase 4: FocuserState[6], RotatorState, FeatureState[8], WeatherState;
//          activeFocuser index.
// Phase 8: Jog (:Mn/:Ms/:Me/:Mw#) and pulse guide (:Mg/:MG#) motion fields.
//          Custom guide rate storage for :RA#/:RE#.
//
// Thread safety: mutex protects all fields. Use std::lock_guard<std::mutex>.
//
// NOTE: std::mutex is neither copyable nor movable. SimState therefore cannot
// be returned by value or assigned. Use init() to initialise an existing
// instance in-place, not to construct a new one by value.

#include <cstdint>
#include <ctime>
#include <mutex>

#include "config/SimConfig.h"

// ---------------------------------------------------------------------------
// Enumerations — mirror firmware values exactly
// ---------------------------------------------------------------------------

enum ParkState : uint8_t {
    PS_UNPARKED    = 0,
    PS_PARKING     = 1,
    PS_PARKED      = 2,
    PS_PARK_FAILED = 3,
    PS_UNPARKING   = 4,
};

enum PierSide : uint8_t {
    PIER_SIDE_NONE  = 0,
    PIER_SIDE_EAST  = 1,
    PIER_SIDE_WEST  = 2,
};

enum class MountState : uint8_t {
    STANDBY,
    TRACKING,
    SLEWING_GOTO,
    SLEWING_HOME,
    GUIDING,
    PARKING,
    PARKED,
    PARK_FAILED,
    HOMING,
};

enum class HomeState : uint8_t {
    IDLE,
    HOMING,
};

enum class PecState : uint8_t {
    NONE         = 0,
    READY_PLAY   = 1,
    PLAYING      = 2,
    READY_RECORD = 3,
    RECORDING    = 4,
};

enum class GotoState : uint8_t {
    NONE = 0,
    GOTO = 1,
    DONE = 2,
};

enum class GuideState : uint8_t {
    NONE   = 0,
    ACTIVE = 1,
    PULSE  = 2,
    SPIRAL = 3,
};

// Phase 8 — direction of an active jog or pulse-guide move on one axis.
// NONE means that axis currently has no jog/pulse motion in progress.
enum class GuideDirection : uint8_t {
    NONE  = 0,
    PLUS  = 1,   // West (Axis1/RA) or North (Axis2/Dec) — increasing coordinate
    MINUS = 2,   // East (Axis1/RA) or South (Axis2/Dec) — decreasing coordinate
};

enum class RateComp : uint8_t {
    NONE            = 0,
    REFRACTION      = 1,
    REFRACTION_DUAL = 2,
    MODEL           = 3,
    MODEL_DUAL      = 4,
};

enum PreferredPierSide : uint8_t {
    BEST = 0,
    EAST = 1,
    WEST = 2,
};

// ---------------------------------------------------------------------------
// Phase 4 sub-system state structs
// ---------------------------------------------------------------------------

// Per-focuser state (up to 6 focusers on Axis4..9).
// All position units are steps internally; micron conversions use stepsPerMicron.
struct FocuserState {
    long   positionSteps  = 0;       // current position in steps
    long   targetSteps    = 0;       // target for absolute/relative goto
    long   backlashSteps  = 0;       // current backlash setting in steps
    bool   isMoving       = false;   // true while SimClock is stepping toward target
    bool   isDC           = false;   // true if DC (non-absolute) focuser mode

    int    gotoRate       = 3;       // 1-5; selects speed for :FS#/:Fr# moves
    int    moveRate       = 2;       // 1-4; selects speed for :F+#/:F-# moves

    // Temperature compensation (TCF)
    float  temperature    = 20.0f;   // last read temperature in °C
    bool   tcfEnabled     = false;
    float  tcfCoef        = 0.0f;    // steps per degree C
    long   tcfDeadband    = 5;       // steps deadband before TCF correction fires
    float  tcfT0          = 20.0f;   // reference temperature in °C

    // DC-mode power level (0-100%)
    int    dcPower        = 100;

    // Home / limits in steps
    long   homePositionSteps = 0;
    long   limitMinSteps     = 0;
    long   limitMaxSteps     = 0;    // populated from config: limitMax * stepsPerMicron * 1000

    // Steps-per-micron for this focuser (from config stepsPerMicron[i])
    float  stepsPerMicron = 0.5f;

    bool   homing         = false;
};

// Rotator state (Axis3).
struct RotatorState {
    double angle          = 0.0;    // current angle in degrees
    double targetAngle    = 0.0;    // target for :rS#/:rr# moves
    bool   isMoving       = false;
    bool   isParked       = false;
    bool   derotEnabled   = false;
    bool   derotReverse   = false;
    bool   homing         = false;
    int    gotoRate       = 3;      // 1-9 slew rate preset
    long   backlash       = 0;      // steps
    double limitMin       = 0.0;    // degrees
    double limitMax       = 360.0;  // degrees
    double stepsPerDegree = 64.0;   // from config stepsPerDegree[2]
};

// Per-feature state (8 slots, 0-indexed, matching FEATURE[1-8]_PURPOSE).
struct FeatureState {
    int   purpose     = -1;         // OFF (-1) or purpose code 1..7
    long  value       = 0;          // SWITCH/ANALOG_OUTPUT current value

    // DEW_HEATER fields
    bool  dewEnabled  = false;
    float dewZero     = 0.0f;       // offset (°C)
    float dewSpan     = 5.0f;       // span (°C)
    float dewDeltaT   = 0.0f;       // current delta-T reading

    // INTERVALOMETER fields
    bool  intvEnabled  = false;
    int   intvCount    = 0;         // frames remaining (0 = unlimited)
    float intvCurrent  = 0.0f;      // frames taken this run
    float intvExposure = 1.0f;      // seconds
    float intvDelay    = 5.0f;      // seconds between frames
};

// Weather sensor state (BME280 or similar).
struct WeatherState {
    float temperature = 15.0f;      // °C — non-zero so probeController succeeds
    float pressure    = 1013.0f;    // mb
    float humidity    = 60.0f;      // %
    float dewPoint    = 7.0f;       // °C
    float mcuTemp     = 25.0f;      // °C (always present in sim)
};

// ---------------------------------------------------------------------------
// SimState
// ---------------------------------------------------------------------------

struct SimState {
    mutable std::mutex mutex;

    // Mount position
    double ra       = 6.0;
    double dec      = 45.0;
    double ha       = 0.0;
    double az       = 180.0;
    double alt      = 45.0;
    PierSide pierSide = PIER_SIDE_EAST;

    // Goto target
    double  targetRA     = 0.0;
    double  targetDec    = 0.0;
    bool    targetRASet  = false;
    bool    targetDecSet = false;

    // State machines
    MountState  mountState  = MountState::STANDBY;
    ParkState   parkState   = PS_UNPARKED;
    HomeState   homeState   = HomeState::IDLE;
    PecState    pecState    = PecState::NONE;
    GotoState   gotoState   = GotoState::NONE;
    GuideState  guideState  = GuideState::NONE;
    GuideState  pulseGuide  = GuideState::NONE;

    // Tracking
    bool      isTracking       = false;
    RateComp  rateComp         = RateComp::NONE;
    float     trackingRateHz   = 60.136f;
    bool      syncToEncoders   = false;
    bool      startupTrusted   = false;

    // Goto / meridian flip
    bool autoFlipEnabled      = false;
    bool pauseAtHomeEnabled   = false;
    bool homePaused           = false;
    PreferredPierSide preferredPierSide = BEST;

    // Home
    bool  isAtHome        = true;
    bool  autoHomeAtBoot  = false;
    long  homeOffsetAxis1 = 0;
    long  homeOffsetAxis2 = 0;

    // Park position
    double parkRA           = 0.0;
    double parkDec          = 90.0;
    bool   parkPositionSet  = false;

    // Guide / slew rates
    int    guideRateSelect   = 2;
    int    pulseRateSelect   = 2;
    double slewRateDegPerSec = 1.0;

    // Custom guide rates set via :RA[n.n]# / :RE[n.n]# (deg/s, unsigned).
    // Only meaningful when guideRateSelect == GUIDE_RATE_CUSTOM (10).
    double customRateAxis1DegPerSec = 0.0;
    double customRateAxis2DegPerSec = 0.0;

    // Phase 8 — Jog (continuous :Mn/:Ms/:Me/:Mw#) and pulse guide
    // (:Mg/:MG#) motion. Independent of mountState — mirrors firmware,
    // where guiding can run concurrently with TRACKING.
    GuideDirection jogDirectionAxis1   = GuideDirection::NONE;  // Axis1 = RA
    GuideDirection jogDirectionAxis2   = GuideDirection::NONE;  // Axis2 = Dec
    double         jogRateDegPerSecAxis1 = 0.0;  // unsigned magnitude
    double         jogRateDegPerSecAxis2 = 0.0;  // unsigned magnitude

    GuideDirection pulseDirectionAxis1 = GuideDirection::NONE;
    GuideDirection pulseDirectionAxis2 = GuideDirection::NONE;
    double         pulseRateDegPerSecAxis1   = 0.0;  // unsigned magnitude
    double         pulseRateDegPerSecAxis2   = 0.0;  // unsigned magnitude
    long           pulseTicksRemainingAxis1  = 0;    // counted down at 10 Hz
    long           pulseTicksRemainingAxis2  = 0;

    // PEC
    bool   pecRecorded    = false;
    int8_t pecBuffer[720] = {};
    long   pecWormSteps   = 0;
    bool   wormIndexSense = false;
    long   pecBufferIndex = 0;

    // Alignment
    int  alignExpected  = 0;
    int  alignDoneCount = 0;
    bool alignDone      = false;

    // Limits
    double horizonMin         = -10.0;
    double horizonMax         =  90.0;
    double meridianLimitEDeg  =   0.0;
    double meridianLimitWDeg  =   0.0;
    double axis1LimitMin      = -180.0;
    double axis1LimitMax      =  180.0;
    double axis2LimitMin      =  -90.0;
    double axis2LimitMax      =   90.0;

    // Site / time
    struct Site {
        double latitude   = 51.5;
        double longitude  = 0.0;
        double timezone   = 0.0;
        double elevation  = 100.0;
        char   name[16]   = "None";
    };
    Site  sites[4];
    int   currentSite = 0;
    double utcOffset  = 0.0;

    bool   dateReady  = false;
    bool   timeReady  = false;
    double utcHours   = 12.0;
    struct { int y = 2024, m = 1, d = 1; } utcDate;

    // Sound
    bool soundEnabled = false;

    // Error
    uint8_t errorCode = 0;

    // PPS
    bool ppsSynced = false;

    // Phase 4 sub-systems -------------------------------------------------------

    // Focusers: focuser[0] = Axis4, ..., focuser[5] = Axis9.
    // Only focuser[0..numFocusers-1] are valid per config.
    FocuserState focuser[6];

    // Index of the currently active focuser (0-based).
    // Set by :FA[n]# (driver sends 1-based; we store 0-based internally).
    int activeFocuser = 0;

    // Rotator (Axis3). Valid only when cfg.hasRotator.
    RotatorState rotator;

    // Aux features: feature[0] = FEATURE1, ..., feature[7] = FEATURE8.
    FeatureState feature[8];

    // Weather sensor. Valid only when cfg.hasWeather.
    WeatherState weather;

    // ---------------------------------------------------------------------------
    // Factory — initialise THIS instance from config defaults.
    // Does NOT return by value (mutex is non-movable).
    // Usage:
    //   SimState state;
    //   state.init(cfg);
    // ---------------------------------------------------------------------------
    void init(const SimConfig& cfg) {
        soundEnabled        = cfg.soundEnabled;
        slewRateDegPerSec   = cfg.slewRateBaseDesired;
        horizonMin          = cfg.horizonLimitMin;
        horizonMax          = cfg.horizonLimitMax;
        meridianLimitEDeg   = cfg.meridianLimitEDeg;
        meridianLimitWDeg   = cfg.meridianLimitWDeg;
        axis1LimitMin       = cfg.limitMin[0];
        axis1LimitMax       = cfg.limitMax[0];
        axis2LimitMin       = cfg.limitMin[1];
        axis2LimitMax       = cfg.limitMax[1];
        for (int i = 0; i < 4; ++i) {
            sites[i].latitude  = cfg.latitude;
            sites[i].longitude = cfg.longitude;
            sites[i].timezone  = cfg.timezone;
            sites[i].elevation = cfg.elevation;
        }
        utcOffset = cfg.timezone;

        // Phase 4: initialise focuser state from config
        for (int i = 0; i < cfg.numFocusers && i < 6; ++i) {
            focuser[i].stepsPerMicron  = static_cast<float>(cfg.stepsPerMicron[i]);
            // limitMax in config is in mm; convert to steps:
            //   steps = limitMax_mm * 1000_um_per_mm * stepsPerMicron
            focuser[i].limitMaxSteps   = static_cast<long>(
                cfg.limitMax[3 + i] * 1000.0 * cfg.stepsPerMicron[i]);
            focuser[i].limitMinSteps   = static_cast<long>(
                cfg.limitMin[3 + i] * 1000.0 * cfg.stepsPerMicron[i]);
        }
        activeFocuser = 0;

        // Phase 4: initialise rotator from config
        if (cfg.hasRotator) {
            rotator.stepsPerDegree = cfg.stepsPerDegree[2];  // Axis3 = index 2
            rotator.limitMin       = cfg.limitMin[2];
            rotator.limitMax       = cfg.limitMax[2];
        }

        // Phase 4: initialise aux features from config
        for (int i = 0; i < 8; ++i) {
            feature[i].purpose = cfg.featurePurpose[i];
        }

        // Initialise UTC from system clock so SimClock ticks real time
        initUtcFromSystemClock();
    }

    // Populate utcHours and utcDate from the host system clock (UTC)
    void initUtcFromSystemClock() {
        std::time_t now = std::time(nullptr);
        std::tm* t = std::gmtime(&now);
        if (t) {
            utcHours    = t->tm_hour + t->tm_min / 60.0 + t->tm_sec / 3600.0;
            utcDate.y   = t->tm_year + 1900;
            utcDate.m   = t->tm_mon  + 1;
            utcDate.d   = t->tm_mday;
        }
    }
};
