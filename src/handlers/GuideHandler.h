#pragma once
// GuideHandler.h — Handles guiding and guide-rate commands.
//
// Commands handled (matching Guide.command.cpp):
//
//   :GX90#         Get pulse guide rate (n.nn#)
//
//   :Mg[d][n]#     Pulse guide d=n/s/e/w, n=milliseconds (no return)
//   :MG[d][n]#     Pulse guide with return 0/1
//   :Mw# :Me# :Mn# :Ms#   Continuous guide N/S/E/W
//   :Mp#           Spiral guide
//
//   :Q#            Halt all — abort goto + stop guiding
//   :Qe# :Qw#      Halt E/W axis
//   :Qn# :Qs#      Halt N/S axis
//
//   :RG# :RC# :RM# :RF# :RS#   Set guide rate by name
//   :Rn#           Set guide rate by number (0..9)
//   :RA[n.n]#      Set axis1 custom rate deg/s
//   :RE[n.n]#      Set axis2 custom rate deg/s
//
// Guide start/stop writes guideState and pulseGuide directly into SimState
// under mutex (see design note in development plan).
// :Q# calls msm->abortGoto() for the goto part, then clears guide state.
//
// Phase 8: startGuide() additionally computes a signed deg/s rate for the
// commanded axis and writes it into SimState's jog*/pulse* fields, which
// SimClock::tick() reads each tick to actually move ra/dec. Direction sign
// convention (verified against firmware Guide.command.cpp):
//   :Mw# (West) -> Axis1 (RA) increasing   :Me# (East) -> Axis1 decreasing
//   :Mn# (North)-> Axis2 (Dec) increasing  :Ms# (South)-> Axis2 decreasing
// stopAxis1()/stopAxis2() now act independently — previously both cleared
// guideState/pulseGuide unconditionally regardless of which axis a call
// targeted; this is fixed as part of Phase 8 so :Qe#/:Qw# only halt Axis1
// and :Qn#/:Qs# only halt Axis2, matching firmware's stopAxis1()/stopAxis2().

#include "HandlerBase.h"
#include "state/MountStateMachine.h"

class GuideHandler : public HandlerBase {
public:
    void setStateMachine(MountStateMachine* msm) { m_msm = msm; }

    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error
    ) override;

private:
    MountStateMachine* m_msm = nullptr;

    // Validate guide preconditions. Returns CE_NONE if OK.
    // If a goto is in progress, aborts it first (mirrors firmware validate()).
    CommandError validateGuide();

    // Start a guide on axis 1 or 2.
    // direction: 'n','s','e','w'
    // rateSelect: 0..9 (table) or 10 (custom, uses stored custom*RateDegPerSec)
    // durationMs: pulse duration in ms (only meaningful when isPulse)
    CommandError startGuide(char direction, int rateSelect, int durationMs, bool isPulse);

    // Clear guide state on Axis1 (E/W) only. Mirrors firmware stopAxis1().
    void stopAxis1();
    // Clear guide state on Axis2 (N/S) only. Mirrors firmware stopAxis2().
    void stopAxis2();
    // Clear guide state on both axes (:Q#).
    void stopAll();

    // Map named rate letter to rate index matching firmware
    static int namedRateToIndex(char c);

    // Convert rate index to approximate sidereal multiplier (for GX90 reply)
    static float rateIndexToSidereal(int r);

    // Phase 8 — resolve a rate index (0..9, or 10=custom) to a signed deg/s
    // rate for the given axis (1 or 2), positive meaning the PLUS direction
    // (West for axis1, North for axis2). direction supplies the sign.
    // customDegPerSec is read from SimState under the caller's existing lock.
    static double resolveRateDegPerSec(int rateSelect, int axis,
                                        double customAxis1DegPerSec,
                                        double customAxis2DegPerSec);
};
