#pragma once
// ParkHandler.h — Handles park/unpark commands.
//
// Commands handled (matching Park.command.cpp):
//   :hP#   Move mount to park position
//   :hQ#   Set current position as park position
//   :hR#   Restore parked mount to operation (unpark)

#include "HandlerBase.h"
#include "state/MountStateMachine.h"

class ParkHandler : public HandlerBase {
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
};
