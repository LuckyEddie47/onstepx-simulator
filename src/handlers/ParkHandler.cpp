// ParkHandler.cpp — Park/unpark command handler.
//
// Protocol source: Park.command.cpp
//
// Reply convention for hP and hR:
//   CE_1 is set on success (CommandFramer sends "1").
//   Any other CommandError is set on failure (CommandFramer sends "0" + logs).
//
// The firmware sets *commandError = CE_1 on success for hP/hR, which makes
// the framer emit "1". We mirror this: on success we return CE_1, which the
// framer maps to the "1" reply. On failure we set the actual error code and
// return "0".

#include "handlers/ParkHandler.h"

bool ParkHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)reply;
    (void)suppressFrame;
    // Phase 12: numericReply starts true (dispatcher init); all hP/hQ/hR paths
    // return a numeric result via the error code — no explicit set needed here.
    (void)numericReply;

    if (cmd[0] != 'h') return false;

    // :hP# — Move mount to park position
    if (cmd[1] == 'P' && param[0] == '\0') {
        CommandError e = m_msm->beginPark();
        if (e == CE_NONE) {
            *error = CE_1;
        } else {
            *error = e;
        }
        return true;
    }

    // :hQ# — Set current position as park position
    if (cmd[1] == 'Q' && param[0] == '\0') {
        *error = m_msm->setParkPosition();
        return true;
    }

    // :hR# — Unpark
    if (cmd[1] == 'R' && param[0] == '\0') {
        CommandError e = m_msm->beginUnpark();
        if (e == CE_NONE) {
            *error = CE_1;
        } else {
            *error = e;
        }
        return true;
    }

    return false;
}
