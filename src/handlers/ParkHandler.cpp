// ParkHandler.cpp — Park command handler
// Source reference: Park.command.cpp

#include "ParkHandler.h"

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
    if (!m_cfg->hasMount) return false;
    if (cmd[0] != 'h') return false;

    *numericReply = true;

    // :hP# — move to park position
    // Firmware: CE_NONE -> *commandError = CE_1 (success='1')
    //           error   -> *commandError = e    (failure='0' via dispatcher)
    if (cmd[1] == 'P' && param[0] == 0) {
        CommandError e = m_sm->beginPark();
        if (e == CE_NONE) {
            *error = CE_1;   // success -> numeric reply '1'
        } else {
            *error = e;      // failure -> numeric reply '0'
        }
        return true;
    }

    // :hQ# — set park position
    if (cmd[1] == 'Q' && param[0] == 0) {
        CommandError e = m_sm->setParkPosition();
        *error = (e == CE_NONE) ? CE_1 : e;
        return true;
    }

    // :hR# — unpark
    if (cmd[1] == 'R' && param[0] == 0) {
        CommandError e = m_sm->beginUnpark();
        if (e == CE_NONE) {
            *error = CE_1;
        } else {
            *error = e;
        }
        return true;
    }

    return false;
}
