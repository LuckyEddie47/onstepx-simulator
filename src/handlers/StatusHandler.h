#pragma once
// StatusHandler.h — Handles GU/Gu/GW/Gm/SX97 status commands.
//
// Phase 1 implementation provides correct responses to all commands
// required by probeController() and probeMount(). Full bit-packing
// detail is implemented here to allow Checkpoint 1 to be met completely.
//
// Commands handled:
//   :GU#     -> ASCII status string (variable length, '#'-terminated)
//   :Gu#     -> 9 raw bytes, all >= 0x80, NO '#'
//   :GW#     -> 3-char mount/tracking status + '#'
//   :Gm#     -> pier side ('E'/'W'/'N') + '#'
//   :SX97,n# -> buzzer control, single-char reply '0' or '1'

#include "HandlerBase.h"

class StatusHandler : public HandlerBase {
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
    // Build the :GU# ASCII status string into reply[]
    void buildGuString(char* reply) const;

    // Build the :Gu# 9-byte binary status into reply[] (raw bytes, no NUL)
    // Returns the byte count (always 9)
    int buildGuBinary(uint8_t* buf) const;
};
