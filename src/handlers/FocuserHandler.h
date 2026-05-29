#pragma once
// FocuserHandler.h — Handles all focuser commands.
//
// Commands handled (matching Focuser.command.cpp):
//
// Focuser selection:
//   :FA#        Get active focuser number (1-based); suppressFrame=true (no '#')
//   :FA[n]#     Select focuser n (1-6); single char '1'/'0', no '#'
//
// All :F[cmd]# sub-commands operate on the active focuser unless prefixed
// by a digit 1-6 (e.g. :F2G# operates on focuser 2 regardless of active).
//
// Status:
//   :Fa#        Primary focuser present? -> '1' (always when numFocusers > 0)
//   :FT#        Status: "S3" (stopped) or "M3" (moving)
//   :Fp#        Mode: CE_0=DC, CE_1=absolute
//
// Position (microns / steps):
//   :FG#        Get position microns (signed int, no fraction)
//   :Fg#        Get position steps
//   :FI#        Get min position microns
//   :Fi#        Get min position steps
//   :FM#        Get max position microns
//   :Fm#        Get max position steps
//
// Motion:
//   :F+#        Move in (continuous; sets isMoving, target = limitMax)
//   :F-#        Move out (continuous; sets isMoving, target = limitMin)
//   :FQ#        Stop -> nothing
//   :F[1-4]#    Set move rate (1=slowest, 4=fastest) -> nothing
//   :F[5-9]#    Set goto rate (5=slowest, 9=fastest) -> nothing
//   :FW#        Get working slew rate µm/s
//   :FR[sn]#    Relative goto (signed microns) -> nothing
//   :Fr[sn]#    Relative goto (signed steps) -> nothing
//   :FS[n]#     Absolute goto (microns) -> '0'/'1'
//   :Fs[n]#     Absolute goto (steps) -> '0'/'1'
//   :FZ#        Set position as zero -> nothing
//   :FH#        Set home position -> nothing
//   :Fh#        Goto home -> nothing
//
// Backlash:
//   :FB#        Get backlash microns
//   :Fb#        Get backlash steps
//   :FB[n]#     Set backlash microns -> '0'/'1'
//   :Fb[n]#     Set backlash steps   -> '0'/'1'
//
// Temperature / TCF:
//   :Fe#        Get temperature differential (°C)
//   :Ft#        Get temperature (°C)
//   :Fu#        Get microns per step
//   :FC#        Get TCF coefficient
//   :FC[v]#     Set TCF coefficient -> '0'/'1'
//   :Fc#        Get TCF enabled -> CE_0/CE_1
//   :Fc[n]#     Set TCF enabled -> '0'/'1'
//   :FD#        Get TCF deadband
//   :Fd#        Get TCF deadband (steps variant)
//   :FD[n]#     Set TCF deadband -> '0'/'1'
//   :Fd[n]#     Set TCF deadband (steps) -> '0'/'1'
//
// DC power:
//   :FP#        Get DC power (DC focuser only)
//   :FP[n]#     Set DC power -> '0'/'1'

#include "HandlerBase.h"

class FocuserHandler : public HandlerBase {
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
    // Resolve which focuser slot to operate on.
    // If cmd[1] is '1'..'6', that focuser is used (0-based = digit-1).
    // Otherwise the active focuser is used.
    // Returns -1 if the resolved slot is out of range for this config.
    int resolveSlot(char digitOrCmd) const;

    // Helpers operating on a resolved focuser slot
    void cmdStatus   (int slot, char* reply) const;
    void cmdGetPosMicrons(int slot, char* reply) const;
    void cmdGetPosSteps  (int slot, char* reply) const;
    void cmdGetMinMicrons(int slot, char* reply) const;
    void cmdGetMinSteps  (int slot, char* reply) const;
    void cmdGetMaxMicrons(int slot, char* reply) const;
    void cmdGetMaxSteps  (int slot, char* reply) const;
    void cmdMoveIn   (int slot);
    void cmdMoveOut  (int slot);
    void cmdStop     (int slot);
    void cmdSetMoveRate(int slot, int rate);
    void cmdSetGotoRate(int slot, int rate);
    void cmdGetWorkingRate(int slot, char* reply) const;
    void cmdRelGotoMicrons(int slot, long deltaUm);
    void cmdRelGotoSteps  (int slot, long deltaSteps);
    bool cmdAbsGotoMicrons(int slot, long targetUm);
    bool cmdAbsGotoSteps  (int slot, long targetSteps);
    void cmdSetZero  (int slot);
    void cmdSetHome  (int slot);
    void cmdGotoHome (int slot);
    bool cmdSetBacklashMicrons(int slot, long um);
    bool cmdSetBacklashSteps  (int slot, long steps);
    bool cmdSetTcfCoef  (int slot, float coef);
    bool cmdSetTcfEnable(int slot, int enable);
    bool cmdSetTcfDeadband(int slot, long deadband);
    bool cmdSetTcfDeadbandSteps(int slot, long steps);
    bool cmdSetDcPower  (int slot, int pct);

    // Micron <-> step conversions for a slot
    long micronsToSteps(int slot, double um) const;
    double stepsToMicrons(int slot, long steps) const;
};
