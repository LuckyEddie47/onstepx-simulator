#pragma once
// ParkHandler.h — Handles :hP#, :hQ#, :hR# park commands.
// Source reference: Park.command.cpp
//
// :hP# — begin park  -> returns CE_1 on success (numeric '1'), error code on fail
// :hQ# — set park position -> returns CE_1 on success
// :hR# — unpark       -> returns CE_1 on success

#include "HandlerBase.h"
#include "state/MountStateMachine.h"

class ParkHandler : public HandlerBase {
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
