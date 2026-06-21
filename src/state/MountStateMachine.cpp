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
    // Phase 10: CommandError values below now exactly match firmware's
    // numbering (see CommandFramer.h). The :MS# single-character reply is
    // built by MountHandler::gotoErrorChar(), which as of Phase 10 uses
    // firmware's actual arithmetic formula
    // (reply = (e - CE_SLEW_ERR_BELOW_HORIZON) + '1') instead of a
    // hand-built switch — see that function for the full mapping.
    //
    // NOTE: the precondition logic below (check order, the "target not
    // set" condition via CE_SLEW_ERR_SLEW, and the already-in-motion case
    // returning CE_SLEW_ERR_HARDWARE_FAULT instead of CE_MOUNT_IN_MOTION)
    // is INTENTIONALLY left unchanged in this phase — Phase 10 is a pure
    // rename/renumber with zero behavior change. The precondition chain
    // itself (order, the invented "target not set" check, the wrong
    // already-in-motion code, and the missing startupAuthority/trust gate)
    // is reworked in Phase 11 per the audit findings (1.5, 2.2, 4.9).

    if (m_state->parkState == PS_PARKED || m_state->parkState == PS_PARKING)
        return CE_SLEW_ERR_IN_PARK;

    if (!m_state->dateReady || !m_state->timeReady)
        return CE_SLEW_ERR_IN_STANDBY;

    if (!m_state->targetRASet || !m_state->targetDecSet)
        return CE_SLEW_ERR_SLEW;            // "no target set" — Phase 10: renamed
                                             // from CE_SLEW_IN_SLEW only; this
                                             // condition itself has no firmware
                                             // equivalent and is reworked in
                                             // Phase 11, not removed here.

    double alt = targetAltitudeDeg();
    if (alt < m_state->horizonMin)
        return CE_SLEW_ERR_BELOW_HORIZON;
    if (alt > m_state->horizonMax)
        return CE_SLEW_ERR_ABOVE_OVERHEAD;

    MountState ms = m_state->mountState;
    if (ms == MountState::SLEWING_GOTO || ms == MountState::PARKING ||
        ms == MountState::HOMING)
        return CE_SLEW_ERR_HARDWARE_FAULT; // Phase 10: value renumbered only;
                                            // this is still the wrong code for
                                            // "already in motion" (should be
                                            // CE_MOUNT_IN_MOTION) — fixed in
                                            // Phase 11, not here.

    return CE_NONE;
}

CommandError MountStateMachine::beginGoto() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    CommandError e = validateGoto();
    if (e != CE_NONE) return e;

    m_state->mountState  = MountState::SLEWING_GOTO;
    m_state->gotoState   = GotoState::GOTO;
    m_state->isTracking  = false;
    // Phase 8: a goto supersedes any in-progress jog/pulse guide motion —
    // clear it so SimClock's goto interpolation is the sole writer of
    // ra/dec while slewing (mirrors firmware: Goto always takes over from
    // Guide).
    clearJogAndPulseMotion();
    // SimClock detects the state transition and calls beginGoto() on next tick
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
    if (m_state->parkState == PS_PARKED)  return CE_PARKED;
    if (m_state->parkState == PS_PARKING) return CE_PARKED;

    m_state->parkState  = PS_PARKING;
    m_state->mountState = MountState::PARKING;
    m_state->isTracking = false;
    // Phase 8: park supersedes any in-progress jog/pulse guide motion —
    // see beginGoto() for the equivalent rationale.
    clearJogAndPulseMotion();
    // SimClock detects PARKING state and primes park timer
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
    return CE_NONE;
}

// ---------------------------------------------------------------------------
// Home
// ---------------------------------------------------------------------------

CommandError MountStateMachine::beginHome() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    if (!m_cfg->hasHomeSense) {
        // No home sense — attempt anyway (firmware does, sim honours it)
        // but set error state so tests can detect it
    }
    if (m_state->parkState == PS_PARKED) return CE_PARKED;

    m_state->mountState = MountState::HOMING;
    m_state->homeState  = HomeState::HOMING;
    m_state->isTracking = false;
    // Phase 8: home supersedes any in-progress jog/pulse guide motion —
    // see beginGoto() for the equivalent rationale.
    clearJogAndPulseMotion();
    // SimClock detects HOMING state and primes home timer
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
    if (!m_state->targetRASet || !m_state->targetDecSet)
        return CE_SLEW_ERR_OUTSIDE_LIMITS;

    m_state->ra  = m_state->targetRA;
    m_state->dec = m_state->targetDec;
    m_state->alignDone      = true;
    m_state->alignDoneCount++;
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
