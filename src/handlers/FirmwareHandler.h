#pragma once
// FirmwareHandler.h — Handles GV* firmware identity commands.
//
// Commands handled:
//   :GVP#  -> "On-Step"     (product name — driver does strcmp)
//   :GVN#  -> "10.24c"      (version — driver checks major >= 10)
//   :GVD#  -> firmware date
//   :GVT#  -> firmware time
//   :GVC#  -> config name (from HOST_NAME)
//   :GVH#  -> hardware string
//   :GVM#  -> general message "On-Step 10.24c"
//
// All replies are '#'-terminated. numericReply=false for all.

#include "HandlerBase.h"

class FirmwareHandler : public HandlerBase {
public:
    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error
    ) override;
};
