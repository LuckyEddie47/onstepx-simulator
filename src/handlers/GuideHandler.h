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
    // rateSelect: 0..9 or the stored axis rate
    // durationMs: 0 = continuous
    CommandError startGuide(char direction, int rateSelect, int durationMs, bool isPulse);

    // Clear guide state on one or both axes.
    void stopAxis1();
    void stopAxis2();
    void stopAll();

    // Map named rate letter to rate index matching firmware
    static int namedRateToIndex(char c);

    // Convert rate index to approximate sidereal multiplier (for GX90 reply)
    static float rateIndexToSidereal(int r);
};
