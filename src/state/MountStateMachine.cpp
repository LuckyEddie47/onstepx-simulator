// MountStateMachine.cpp — Mount state transition logic

#include "MountStateMachine.h"

#include <cmath>

static constexpr double PI = 3.14159265358979323846;
static constexpr double DEG = PI / 180.0;

// ---------------------------------------------------------------------------
// Tracking
// ---------------------------------------------------------------------------

CommandError MountStateMachine::startTracking() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    if (m_state->parkState == PS_PARKED)      return CE_PARKED;
    if (m_state->parkState == PS_PARKING)     return CE_PARKED;
    m_state->isTracking  = true;
    m_state->mountState  = MountState::TRACKING;
    return CE_NONE;
}

CommandError MountStateMachine::stopTracking() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->isTracking = false;
    if (m_state->mountState == MountState::TRACKING)
        m_state->mountState = MountState::STANDBY;
    return CE_NONE;
}

void MountStateMachine::setTrackingRate(float hz) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->trackingRateHz = hz;
}

// ---------------------------------------------------------------------------
// Goto
// ---------------------------------------------------------------------------

CommandError MountStateMachine::validateGoto() const {
    // Called without lock (caller holds it, or called from beginGoto which holds it)
    //
    // Phase 11 — full rework, replacing Phase 10's deliberately-preserved
    // pre-existing logic. Mirrors firmware's real chain exactly, verified
    // directly against Goto.cpp this phase:
    //
    //   Goto::request() — unconditional trust check (BEFORE everything else)
    //   Goto::setTarget() — calls validate(); if CE_SLEW_ERR_IN_STANDBY and
    //     (encoders present OR mount is at home), auto-enables axes and
    //     re-validates (this simulator has no absolute-encoder concept, so
    //     only the isAtHome branch is reachable here — see AxisHandler);
    //     then, for goto only (not sync), an above-overhead check.
    //   Goto::validate() — axis-enabled -> parked -> already-slewing ->
    //     guiding-or-in-motion -> limits(unmodeled until Phase 17) ->
    //     hardware-fault(unmodeled, no motor-fault concept in this
    //     simulator yet).
    //
    // The below-horizon check is NOT part of firmware's validate()/
    // setTarget() chain at all in the form this simulator previously had it
    // (a flat altitude comparison) — real horizon/overhead enforcement is
    // limits.isAboveOverhead() (goto only) and the broader
    // limits.validateTarget() (both), neither of which exist yet in this
    // simulator (Phase 17). The horizon/overhead checks below are KEPT as
    // an interim approximation (better than no limit checking at all)
    // but moved to the position firmware's setTarget() actually puts the
    // overhead check (after axis/park/slew/motion preconditions, govered
    // by isGoto), pending Phase 17's proper limits.isGotoError()/
    // validateTarget() implementation replacing this entirely.
    //
    // REMOVED: the invented "target not set" precondition
    // (targetRASet/targetDecSet gating). Firmware has no such concept —
    // gotoTarget is zero-initialized at boot and :MS#/:CM# always operate
    // on whatever it currently holds. targetRASet/targetDecSet remain in
    // SimState (set by :Sr#/:Sd#/Library handlers) as inert bookkeeping;
    // nothing reads them for validation purposes any more.

    if (!m_state->startupTrusted)
        return CE_SLEW_ERR_UNSPECIFIED;

    return validateGotoOrSync(/*isGoto=*/true);
}

CommandError MountStateMachine::validateGotoOrSync(bool isGoto) const {
    // Shared core of Goto::validate(), called by both goto and sync paths
    // (mirrors firmware: Goto::setTarget() is called by both
    // Goto::request() and Goto::requestSync(), and always calls validate()
    // first regardless of isGoto — isGoto only affects what happens after
    // validate() succeeds).

    // --- axis-enabled, with firmware's standby auto-recovery special case ---
    if (!m_state->axesEnabled) {
        // Goto::setTarget(): if standby-rejected and (encoders present OR
        // mount is at home), auto-enable and re-check. This simulator has
        // no absolute-encoder concept (see AxisHandler / SimConfig — no
        // config models AXIS*_DRIVER_MODEL as an absolute-encoder type),
        // so only the isAtHome branch is reachable here.
        if (m_state->isAtHome) {
            m_state->axesEnabled = true; // const method, non-const pointer member —
                                          // mutating *m_state is valid C++, same
                                          // pattern as every other const accessor
                                          // in this class that reads via m_state.
        } else {
            return CE_SLEW_ERR_IN_STANDBY;
        }
    }

    if (m_state->parkState == PS_PARKED)
        return CE_SLEW_ERR_IN_PARK;

    MountState ms = m_state->mountState;
    if (ms == MountState::SLEWING_GOTO)
        return CE_SLEW_ERR_SLEW; // "already in goto" — firmware's real meaning

    if (m_state->guideState != GuideState::NONE || m_state->pulseGuide != GuideState::NONE ||
        ms == MountState::PARKING || ms == MountState::HOMING)
        return CE_MOUNT_IN_MOTION;

    // Interim horizon/overhead approximation — see header comment above.
    // Firmware's real check (limits.isAboveOverhead()) only applies to
    // goto, not sync (Goto::setTarget()'s `if (... && isGoto && ...)`);
    // the below-horizon side has no direct firmware equivalent at this
    // stage at all (limits.validateTarget() covers it differently and is
    // deferred to Phase 17) but is kept here for both, as the best
    // available approximation until then.
    double alt = targetAltitudeDeg();
    if (alt < m_state->horizonMin)
        return CE_SLEW_ERR_BELOW_HORIZON;
    if (isGoto && alt > m_state->horizonMax)
        return CE_SLEW_ERR_ABOVE_OVERHEAD;

    return CE_NONE;
}

CommandError MountStateMachine::beginGoto() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    CommandError e = validateGoto();
    if (e != CE_NONE) return e;

    // Phase 18: select pier side for this goto.
    // Approximate LST from stored ha (ha = lst - ra → lst = ha + ra).
    double lst      = m_state->ha + m_state->ra;
    double targetHA = lst - m_state->targetRA;
    while (targetHA >  12.0) targetHA -= 24.0;
    while (targetHA < -12.0) targetHA += 24.0;

    PierSide ps = selectPierSide(targetHA);
    if (ps == PIER_SIDE_NONE)
        return CE_SLEW_ERR_OUTSIDE_LIMITS;

    m_state->targetPierSide = ps;

    m_state->mountState  = MountState::SLEWING_GOTO;
    m_state->gotoState   = GotoState::GOTO;
    m_state->isTracking  = false;
    m_state->isAtHome    = false;
    clearJogAndPulseMotion();
    return CE_NONE;
}

CommandError MountStateMachine::abortGoto() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    if (m_state->mountState == MountState::SLEWING_GOTO) {
        m_state->mountState = MountState::TRACKING;
        m_state->gotoState  = GotoState::NONE;
        m_state->isTracking = true;
    }
    // Also stop any active guide (jog/pulse motion fields included — Phase 8)
    clearJogAndPulseMotion();
    return CE_NONE;
}

// ---------------------------------------------------------------------------
// Park
// ---------------------------------------------------------------------------

CommandError MountStateMachine::beginPark() {
    std::lock_guard<std::mutex> lk(m_state->mutex);

    // Phase 14: match firmware's Park::request() validation sequence
    // (Park.cpp lines 88–112).
    //
    // Pre-Phase-14 the simulator only checked PS_PARKED and PS_PARKING.
    // All checks below are ordered exactly as in firmware.

    // Firmware line 88: if (!settings.saved) return CE_NO_PARK_POSITION_SET
    if (!m_state->parkPositionSet)
        return CE_NO_PARK_POSITION_SET;

    // Firmware line 89: if (state == PS_PARKED)  return CE_NONE  (already parked — success)
    if (m_state->parkState == PS_PARKED)  return CE_NONE;

    // Firmware line 90: if (state == PS_PARKING)     return CE_PARK_FAILED
    if (m_state->parkState == PS_PARKING) return CE_PARK_FAILED;

    // Firmware line 91: if (state == PS_PARK_FAILED) return CE_PARK_FAILED
    if (m_state->parkState == PS_PARK_FAILED) return CE_PARK_FAILED;

    // Firmware line 93: if (!startupAuthority.trusted()) return CE_SLEW_ERR_UNSPECIFIED
    if (!m_state->startupTrusted)
        return CE_SLEW_ERR_UNSPECIFIED;

    // Firmware line 96: if (!mount.isEnabled()) return CE_SLEW_ERR_IN_STANDBY
    if (!m_state->axesEnabled)
        return CE_SLEW_ERR_IN_STANDBY;

    // Firmware line 97: if (goTo.state != GS_NONE) return CE_SLEW_IN_MOTION
    if (m_state->gotoState != GotoState::NONE)
        return CE_MOUNT_IN_MOTION;

    // Firmware line 98: if (guide.state != GU_NONE) return CE_SLEW_IN_MOTION
    if (m_state->guideState != GuideState::NONE)
        return CE_MOUNT_IN_MOTION;

    // (motorFault check omitted — simulator has no motor-fault model)

    // Proceed: stop tracking, enable axes, begin parking motion
    m_state->axesEnabled = true;
    m_state->isAtHome    = false;
    m_state->parkState   = PS_PARKING;
    m_state->mountState  = MountState::PARKING;
    m_state->isTracking  = false;
    clearJogAndPulseMotion();
    return CE_NONE;
}

CommandError MountStateMachine::setParkPosition() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->parkRA          = m_state->ra;
    m_state->parkDec         = m_state->dec;
    m_state->parkPositionSet = true;
    return CE_NONE;
}

CommandError MountStateMachine::beginUnpark() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    if (m_state->parkState != PS_PARKED) return CE_NONE;  // not an error, just noop

    m_state->parkState  = PS_UNPARKED;
    m_state->mountState = MountState::TRACKING;
    m_state->isTracking = true;
    // Phase 11: firmware's Park.cpp enables axes on unpark (confirmed in
    // the original audit via Park.cpp:291 limits.enabled(true) path, and
    // the logical requirement that a parked mount needs motors re-enabled
    // before it can move). The mount was at its park position — not the
    // "home" position in general — so leave isAtHome as-is (it was already
    // cleared on the park entry; only :hF# or a completed home sequence
    // correctly sets isAtHome=true).
    m_state->axesEnabled = true;
    // Phase 17: firmware Park.cpp:291 calls limits.enabled(true) on unpark.
    // Mirror that here. startupTrusted must already be set for unpark to
    // have been possible (Park::request() checks it), so the condition
    // is always satisfied at this point.
    if (m_state->startupTrusted && m_state->dateReady && m_state->timeReady)
        m_state->limitsEnabled = true;
    return CE_NONE;
}

// ---------------------------------------------------------------------------
// Home
// ---------------------------------------------------------------------------

CommandError MountStateMachine::beginHome() {
    std::lock_guard<std::mutex> lk(m_state->mutex);

    // Phase 14: match firmware's Home::request() validation sequence
    // (Home.cpp lines 50–67).
    //
    // Pre-Phase-14 the simulator only checked PS_PARKED.
    // All checks below are ordered exactly as in firmware.

    // Firmware line 57: if (!startupAuthority.trusted() && !hasSense)
    //                       return CE_SLEW_ERR_UNSPECIFIED
    // hasSense = cfg->hasHomeSense; trusted = state->startupTrusted
    if (!m_state->startupTrusted && !m_cfg->hasHomeSense)
        return CE_SLEW_ERR_UNSPECIFIED;

    // Firmware line 61: if (!site.dateIsReady || !site.timeIsReady)
    //                       return CE_SLEW_ERR_IN_STANDBY
    if (!m_state->dateReady || !m_state->timeReady)
        return CE_SLEW_ERR_IN_STANDBY;

    // Firmware line 62: if (goTo.state != GS_NONE) return CE_SLEW_IN_MOTION
    if (m_state->gotoState != GotoState::NONE)
        return CE_MOUNT_IN_MOTION;

    // Firmware line 63: if (guide.state != GU_NONE) return CE_SLEW_IN_MOTION
    // (firmware stops GU_HOME_GUIDE first, but that guide state isn't
    //  modelled in the simulator — any non-NONE guide state blocks homing)
    if (m_state->guideState != GuideState::NONE)
        return CE_MOUNT_IN_MOTION;

    // Proceed: enable axes, begin homing motion
    m_state->axesEnabled = true;
    m_state->isAtHome    = false;
    m_state->mountState  = MountState::HOMING;
    m_state->homeState   = HomeState::HOMING;
    m_state->isTracking  = false;
    clearJogAndPulseMotion();
    return CE_NONE;
}

CommandError MountStateMachine::resetHome() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    // :hF# — reset position to home, clear park state
    m_state->isAtHome   = true;
    m_state->homeState  = HomeState::IDLE;
    m_state->mountState = MountState::STANDBY;
    m_state->isTracking = false;
    // Reset park state (firmware calls park.reset())
    m_state->parkState  = PS_UNPARKED;
    m_state->parkPositionSet = false;
    return CE_NONE;
}

// ---------------------------------------------------------------------------
// Sync
// ---------------------------------------------------------------------------

CommandError MountStateMachine::syncToTarget() {
    std::lock_guard<std::mutex> lk(m_state->mutex);

    // Phase 11 — verified against Goto::requestSync(): same unconditional
    // trust check as goto, before everything else.
    if (!m_state->startupTrusted)
        return CE_SLEW_ERR_UNSPECIFIED;

    CommandError e = validateGotoOrSync(/*isGoto=*/false);
    if (e != CE_NONE) return e;

    m_state->ra  = m_state->targetRA;
    m_state->dec = m_state->targetDec;
    m_state->alignDone      = true;
    m_state->alignDoneCount++;
    // Phase 11: a successful sync means the mount is no longer physically
    // at its home position in general (unless targetRA/Dec happen to BE
    // the home position) — clear isAtHome so a subsequent goto correctly
    // re-checks axesEnabled rather than silently relying on a stale "at
    // home" flag from before this sync moved the reported position.
    m_state->isAtHome = false;
    return CE_NONE;
}

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

double MountStateMachine::targetAltitudeDeg() const {
    // Simple altitude from HA/Dec/latitude
    // alt = asin(sin(dec)*sin(lat) + cos(dec)*cos(lat)*cos(ha))
    double lat = m_state->sites[m_state->currentSite].latitude * DEG;
    double dec = m_state->targetDec * DEG;
    // Compute HA from LST - RA (approximate using stored utcHours)
    // This is a rough check; full LST calc is in SimClock
    double ha   = (m_state->ha != 0.0) ? m_state->ha * 15.0 * DEG : 0.0;
    double sinAlt = std::sin(dec) * std::sin(lat) +
                    std::cos(dec) * std::cos(lat) * std::cos(ha);
    sinAlt = std::max(-1.0, std::min(1.0, sinAlt));
    return std::asin(sinAlt) * 180.0 / PI;
}

// ---------------------------------------------------------------------------
// Phase 18 — Pier-side selection for goto
// ---------------------------------------------------------------------------
// Mirrors firmware's Goto::setTarget() pier-side selection logic (Goto.cpp).
//
// For GEM mounts the axis1 (HA) instrument coordinate determines reachability:
//   East side of pier: star is in western sky, HA > 0 (positive).
//     Instrument axis1 = HA in degrees (same sign convention as firmware).
//     East reachable when 0 ≤ targetHA_deg ≤ axis1LimitMax.
//   West side of pier: star is in eastern sky, HA < 0 (negative).
//     Instrument axis1 = 180 - HA in degrees (flipped through meridian).
//     West reachable when axis1LimitMin ≤ (-(targetHA_deg)+180°) conceptually,
//     simplified to: westReachable when targetHA_deg ≥ -(axis1LimitMax).
//     (Firmware uses a symmetric ±axis1LimitMax for most GEM configs.)
//
// For non-GEM equatorial (FORK) mounts: meridian flips are disabled, so
// PIER_SIDE_EAST is always used (PSS_EAST_ONLY is the effective mode).
//
// Returns PIER_SIDE_EAST, PIER_SIDE_WEST, or PIER_SIDE_NONE if unreachable.

PierSide MountStateMachine::selectPierSide(double targetHA) const {
    // Non-GEM equatorials: no meridian flips → always east side
    if (!m_cfg->meridianFlipsEnabled()) {
        return PIER_SIDE_EAST;
    }

    // Compute east/west reachability from target HA and axis1 limits.
    // axis1LimitMin/Max are in degrees; targetHA is in hours.
    // Firmware convention (Transform.cpp / Goto.cpp setTarget):
    //   East side of pier: star in western sky, HA > 0. Instrument axis1 = targetHADeg.
    //   West side of pier: star in eastern sky, HA < 0. Instrument axis1 = -targetHADeg
    //     (axis is flipped 180° through the meridian for west-side pointing, but in
    //     practice the symmetric ±axis1LimitMax covers both sides equally for most GEMs).
    double targetHADeg = targetHA * 15.0;

    bool eastReachable = (targetHADeg >= m_state->axis1LimitMin &&
                          targetHADeg <= m_state->axis1LimitMax);
    bool westReachable = (-targetHADeg >= m_state->axis1LimitMin &&
                          -targetHADeg <= m_state->axis1LimitMax);

    PreferredPierSide pss = m_state->preferredPierSide;
    // Resolve SAME_ONLY to the actual current pier side
    if (pss == SAME_ONLY) {
        pss = (m_state->pierSide == PIER_SIDE_WEST) ? WEST_ONLY : EAST_ONLY;
    }

    switch (static_cast<int>(pss)) {
    case EAST_ONLY:
        return eastReachable ? PIER_SIDE_EAST : PIER_SIDE_NONE;

    case WEST_ONLY:
        return westReachable ? PIER_SIDE_WEST : PIER_SIDE_NONE;

    case EAST:
        // Prefer east; fall back to west if east unreachable
        if (eastReachable) return PIER_SIDE_EAST;
        if (westReachable) return PIER_SIDE_WEST;
        return PIER_SIDE_NONE;

    case WEST:
        // Prefer west; fall back to east if west unreachable
        if (westReachable) return PIER_SIDE_WEST;
        if (eastReachable) return PIER_SIDE_EAST;
        return PIER_SIDE_NONE;

    case BEST:
    default:
        // From home: use HA sign (firmware's isHome branch in setTarget()).
        if (m_state->isAtHome) {
            if (targetHA < 0.0 && westReachable) return PIER_SIDE_WEST;
            if (eastReachable) return PIER_SIDE_EAST;
            if (westReachable) return PIER_SIDE_WEST;
            return PIER_SIDE_NONE;
        }
        // Not at home: prefer current pier side (PSS_BEST firmware logic).
        if (m_state->pierSide == PIER_SIDE_WEST) {
            if (westReachable) return PIER_SIDE_WEST;
            if (eastReachable) return PIER_SIDE_EAST;
        } else {
            if (eastReachable) return PIER_SIDE_EAST;
            if (westReachable) return PIER_SIDE_WEST;
        }
        return PIER_SIDE_NONE;
    }
}

// Phase 8 — clear jog/pulse guide motion fields on both axes.
// Caller must hold m_state->mutex.
void MountStateMachine::clearJogAndPulseMotion() {
    m_state->jogDirectionAxis1        = GuideDirection::NONE;
    m_state->jogRateDegPerSecAxis1    = 0.0;
    m_state->jogDirectionAxis2        = GuideDirection::NONE;
    m_state->jogRateDegPerSecAxis2    = 0.0;
    m_state->pulseDirectionAxis1      = GuideDirection::NONE;
    m_state->pulseRateDegPerSecAxis1  = 0.0;
    m_state->pulseTicksRemainingAxis1 = 0;
    m_state->pulseDirectionAxis2      = GuideDirection::NONE;
    m_state->pulseRateDegPerSecAxis2  = 0.0;
    m_state->pulseTicksRemainingAxis2 = 0;
    m_state->guideState = GuideState::NONE;
    m_state->pulseGuide = GuideState::NONE;
}
