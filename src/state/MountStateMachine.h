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
};
