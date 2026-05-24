#pragma once
// HomeHandler.h — Handles :h?#, :hA#, :hC#, :hC1,n#, :hC2,n#, :hF# home commands.
// Source reference: Home.command.cpp
//
// :h?#      — get home status: hasSense,axis1Offset,axis2Offset  (3 fields, not 4)
// :hAn#     — set auto home at boot (0/1), no reply
// :hC#      — begin homing, no reply, *commandError = request()
// :hC1,n#   — set axis1 home offset/reverse, no reply
// :hC2,n#   — set axis2 home offset/reverse, no reply
// :hF#      — reset to home position, no reply

#include "HandlerBase.h"
#include "state/MountStateMachine.h"

class HomeHandler : public HandlerBase {
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
};
