#pragma once
// MountStateMachine.h — Mount state transition logic.
//
// Centralises all state machine transitions so handlers don't reach
// into SimState directly. Called by MountHandler, GotoHandler,
// ParkHandler, HomeHandler, GuideHandler.
//
// All methods acquire the SimState mutex internally and return a
// CommandError. CE_NONE = success.

#include "SimState.h"
#include "SimClock.h"
#include "config/SimConfig.h"
#include "protocol/CommandFramer.h"  // for CommandError

class MountStateMachine {
public:
    void setConfig(const SimConfig* cfg) { m_cfg = cfg; }
    void setState(SimState* state)       { m_state = state; }
    void setClock(SimClock* clock)       { m_clock = clock; }

    // Tracking control
    CommandError startTracking();
    CommandError stopTracking();
    void setTrackingRate(float hz);

    // Goto
    CommandError beginGoto();   // validates then transitions to SLEWING_GOTO
    CommandError abortGoto();   // transitions back to TRACKING or STANDBY

    // Park
    CommandError beginPark();
    CommandError setParkPosition();
    CommandError beginUnpark();

    // Home
    CommandError beginHome();
    CommandError resetHome();   // :hF# — reset position to home, no slew

    // Sync
    CommandError syncToTarget();

    // Validate goto preconditions — returns error code matching :MS# spec
    CommandError validateGoto() const;

private:
    const SimConfig* m_cfg   = nullptr;
    SimState*        m_state = nullptr;
    SimClock*        m_clock = nullptr;

    // Altitude check using simple latitude/dec formula
    double targetAltitudeDeg() const;
    // Phase 18: compute pier side for goto target based on HA and preferredPierSide.
    // Returns PIER_SIDE_EAST, PIER_SIDE_WEST, or PIER_SIDE_NONE (unreachable).
    PierSide selectPierSide(double targetHA) const;

    // Phase 11 — shared core of firmware's Goto::validate(), called by
    // both validateGoto() (isGoto=true) and syncToTarget()'s validation
    // (isGoto=false). See validateGoto()'s implementation comment for the
    // full chain this replicates. Caller must hold m_state->mutex (or be
    // validateGoto()/syncToTarget(), which already do).
    CommandError validateGotoOrSync(bool isGoto) const;

    // Phase 8 — clear jog/pulse guide motion fields on both axes.
    // Called whenever goto/park/home takes over ra/dec, so SimClock never
    // has two motion sources writing the same tick. Caller must hold
    // m_state->mutex.
    void clearJogAndPulseMotion();
};
