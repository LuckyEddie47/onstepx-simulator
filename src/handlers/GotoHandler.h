#pragma once
// GotoHandler.h — Handles goto, sync, alignment, and target commands.
//
// Commands handled (matching Goto.command.cpp):
//   A?#  A[n]#  A+#  AW#         Alignment
//   CS#  CM#                      Sync
//   D#                            Distance bars (slewing indicator)
//   Gr#  GrH#                     Get target RA
//   Gd#  GdH#                     Get target Dec
//   GX9[n]#                       Get goto settings (pier side, rate, flip)
//   MA#  MD#  MN#  MS#  MP#  MNe# MNw#   Goto variants
//   Sr#  Sd#                      Set target RA / Dec
//   SX9[n],[v]#                   Set goto settings (rate, flip, pier side, pause)

#include "HandlerBase.h"
#include "state/MountStateMachine.h"

class GotoHandler : public HandlerBase {
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

    // Format helpers — mirror firmware PM_HIGH / PM_HIGHEST behaviour
    // Returns hours as "HH:MM.T" (PM_HIGH) or "HH:MM:SS.SSSS" (PM_HIGHEST)
    static void formatHMS(char* buf, double hours, bool highest);

    // Returns degrees as "sDD*MM" (PM_HIGH) or "sDD*MM:SS.SSS" (PM_HIGHEST)
    static void formatDMS(char* buf, double deg, bool sign, bool highest);

    // Parse "HH:MM.T", "HH:MM:SS", or "HH:MM:SS.SSSS" → hours.
    // Returns false on parse failure.
    static bool parseHMS(const char* s, double& hours);

    // Parse "sDD*MM", "sDD*MM:SS", or "sDD*MM:SS.SSS" → degrees.
    // sign=true means leading +/- is expected.
    // Returns false on parse failure.
    static bool parseDMS(const char* s, double& deg, bool sign);

    // Map CommandError slew error → '1'..'9' digit string (for :MS# etc.)
    static char slewErrorChar(CommandError e);

    // Alignment helpers
    bool alignActive() const;
};
