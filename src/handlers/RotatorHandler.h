#pragma once
// RotatorHandler.h — Handles rotator and derotator commands.
//
// Commands handled (matching Rotator.command.cpp):
//
//   :GX98#     Rotator/derotator present: "D" if hasDerotator, "R" if hasRotator
//
//   :rA#       Get angle as signed decimal degrees (e.g. "+123.45")
//   :rG#       Get angle in "sDDD*MM" format (e.g. "+123*45")
//   :rS[deg]#  Absolute goto (decimal degrees) -> '0'/'1'
//   :rr[deg]#  Relative goto (signed decimal degrees) -> nothing
//   :rQ#       Stop -> nothing
//   :r1#       Set goto rate 1 (slowest) -> nothing
//   :r2# .. :r9# Set goto rate 2-9 -> nothing
//   :rF#       Reset position to 0 degrees -> nothing
//   :rP#       Goto parallactic angle -> nothing
//   :r+#       Enable derotator (ALTAZM only; silently ignored on GEM/FORK)
//   :r-#       Disable derotator -> nothing
//   :rB[n]#    Set backlash (arcseconds) -> '0'/'1'
//   :rB#       Get backlash -> "n"
//   :rT#       Status: "I" (idle) or "B" (busy/moving)
//   :rb#       Get backlash (steps variant) -> "n"
//   :rR#       Derotator reverse toggle -> nothing
//   :rZ#       Sync to position 0 -> nothing
//
//   :hP# / :hR# — rotator park/unpark when !hasMount
//     :hP#    Park rotator (go to angle=0, set isParked)
//     :hR#    Unpark rotator (clear isParked)

#include "HandlerBase.h"

class RotatorHandler : public HandlerBase {
public:
    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error
    ) override;

private:
    // Compute parallactic angle (degrees) from current mount state.
    // Uses sim's RA, Dec, and site latitude.
    //
    // Formula: PA = atan2(sin(H), tan(lat)*cos(Dec) - sin(Dec)*cos(H))
    // where H = hour angle in radians, lat = latitude in radians.
    //
    // Named constants below make it easy to replace with real values later.
    double computeParallacticAngle() const;
};
