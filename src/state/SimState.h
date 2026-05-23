#pragma once
// SimState.h — Central mutable simulator state.
//
// Phase 1: fields required for FirmwareHandler and Status stub responses.
// Fields are added incrementally in later phases.
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
