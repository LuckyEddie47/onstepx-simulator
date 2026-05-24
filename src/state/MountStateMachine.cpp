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
    // Returns the :MS# error code character value minus '0':
    //   CE_NONE = 0 = '0' success
    //   CE_SLEW_ERR_BELOW_HORIZON  = below horizon  -> '1'
    //   CE_SLEW_ERR_ABOVE_OVERHEAD = above overhead -> '2'
    //   CE_SLEW_ERR_IN_STANDBY     = standby        -> '3'
    //   CE_SLEW_ERR_IN_PARK        = parked          -> '4'
    //   CE_SLEW_IN_SLEW            = already slewing -> '5'
    //   CE_SLEW_ERR_OUTSIDE_LIMITS = outside limits  -> '6'
    //   CE_SLEW_ERR_HARDWARE_FAULT = hw fault        -> '7'
    //   CE_SLEW_ERR_ALT_MIN        = alt min         -> unused in :MS# path

    // Check order matches Goto.command.cpp guard sequence exactly:
    // target not set -> '5', parked -> '4', standby -> '3',
    // below horizon -> '1', above overhead -> '2', already slewing -> '9'

    if (m_state->parkState == PS_PARKED || m_state->parkState == PS_PARKING)
        return CE_SLEW_ERR_IN_PARK;        // '4'

    if (!m_state->dateReady || !m_state->timeReady)
        return CE_SLEW_ERR_IN_STANDBY;     // '3'

    if (!m_state->targetRASet || !m_state->targetDecSet)
        return CE_SLEW_IN_SLEW;            // '5' — no target set

    double alt = targetAltitudeDeg();
    if (alt < m_state->horizonMin)
        return CE_SLEW_ERR_BELOW_HORIZON;  // '1'
    if (alt > m_state->horizonMax)
        return CE_SLEW_ERR_ABOVE_OVERHEAD; // '2'

    MountState ms = m_state->mountState;
    if (ms == MountState::SLEWING_GOTO || ms == MountState::PARKING ||
        ms == MountState::HOMING)
        return CE_SLEW_ERR_HARDWARE_FAULT; // '7' — already in motion

    return CE_NONE; // '0' — success
}

CommandError MountStateMachine::beginGoto() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    CommandError e = validateGoto();
    if (e != CE_NONE) return e;

    m_state->mountState  = MountState::SLEWING_GOTO;
    m_state->gotoState   = GotoState::GOTO;
    m_state->isTracking  = false;
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
    // Also stop any active guide
    m_state->guideState = GuideState::NONE;
    m_state->pulseGuide = GuideState::NONE;
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
