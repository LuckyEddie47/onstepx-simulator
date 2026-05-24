#pragma once
// MountHandler.h — Handles mount coordinate, tracking, sync, and slew-rate commands.
//
// Source references: Mount.command.cpp, Goto.command.cpp
//
// Get commands:
//   :GR#/:GRH#   RA  HH:MM:SS / HH:MM:SS.SSSS
//   :GD#/:GDH#   Dec sDD*MM:SS / high precision
//   :GA#         Alt sDD*MM
//   :GZ#         Az  DDD*MM
//   :Gr#/:GrH#   Target RA
//   :Gd#/:GdH#   Target Dec
//   :GX92#       Slew rate deg/s
//   :GX94#       Pier side numeric
//   :GX95#       Auto meridian flip state
//   :GX96#       Preferred pier side char
//
// Set commands:
//   :Sr[HH:MM:SS]#   Set target RA
//   :Sd[sDD*MM:SS]#  Set target Dec
//   :CM#/:CS#        Sync to target
//
// Tracking:
//   :T+# :Te#   Enable sidereal tracking
//   :T-#        Stop tracking
//   :Ts#        Solar rate (60.000 Hz)
//   :To#        Sidereal rate (60.136 Hz)
//   :TL#        Lunar rate (57.900 Hz)
//   :TM#/:TK#   King rate (60.136 Hz)
//   :Tn#        Toggle refraction correction
//   :T1#/:T2#   Single/dual axis refraction
//
// Slew rate:
//   :RS#        Set max slew rate
//   :R[0-9]#    Set slew rate multiplier
//
// Alignment:
//   :A[1-9]#    Start n-star alignment
//   :A+#        Accept alignment star
//   :AW#        Write alignment to NV
//   :A?#        Get alignment status
//
// Goto:
//   :MS#        Goto target (returns char '0'-'9', suppressFrame)
//   :MA#        Goto alt/az target
//   :D#         Slew distance indicator
//
// SX9n get/set (goto misc):
//   :GX92#/:GX93#/:GX94#/:GX95#/:GX96#/:GX97#/:GX99#
//   :SX92,n# :SX93,n# :SX95,n# :SX96,n# :SX98,n# :SX99,n#

#include "HandlerBase.h"
#include "state/MountStateMachine.h"

class MountHandler : public HandlerBase {
public:
    void setStateMachine(MountStateMachine* sm) { m_sm = sm; }

    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error
    ) override;

private:
    MountStateMachine* m_sm = nullptr;

    // Format helpers (DEC-006: default RA = HH:MM:SS)
    void formatRA(char* buf, double hours, bool highPrec) const;
    void formatDec(char* buf, double deg, bool highPrec) const;
    void formatAlt(char* buf, double deg) const;
    void formatAz(char* buf, double deg) const;

    // Parse helpers
    bool parseRA(const char* param, double* hours) const;
    bool parseDec(const char* param, double* deg) const;

    // :MS# error code -> single char (suppressFrame, no #)
    // Maps CommandError to '0'..'9' per Goto.command.cpp
    char gotoErrorChar(CommandError e) const;
};
