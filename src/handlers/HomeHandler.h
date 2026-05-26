#pragma once
// HomeHandler.h — Handles home commands.
//
// Commands handled (matching Home.command.cpp):
//   :h?#         Get home status (hasSense, axis1 offset, axis2 offset)
//   :hA[0|1]#    Set auto home at boot
//   :hC#         Move mount to home position
//   :hC1,n#      Set axis1 home sense offset (arcseconds) or reverse (R)
//   :hC2,n#      Set axis2 home sense offset (arcseconds) or reverse (R)
//   :hF#         Reset mount at home position (cold start)

#include "HandlerBase.h"
#include "state/MountStateMachine.h"

class HomeHandler : public HandlerBase {
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
