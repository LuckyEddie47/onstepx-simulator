#pragma once
// TelescopeHandler.h — Handles miscellaneous telescope control commands.
//
// Protocol source: Telescope.command.cpp
//
// Commands handled:
//   :B+#        Reticle brightness increase; no reply (suppressFrame=true)
//   :B-#        Reticle brightness decrease; no reply (suppressFrame=true)
//   :EC[s]#     Echo string s to stderr log; no reply (suppressFrame=true)
//   :ERESET#    Simulate firmware reset: print "SIMULATOR_RESET" to stdout,
//               sleep 1s, exit(0). Allows test harness to detect reset.
//   :ENVRESET#  Reply "NV memory will be cleared on the next boot."#
//   :ESPFLASH#  Reply CE_CMD_UNKNOWN (not simulated)
//
// Note: :GX9F# (MCU temperature) is handled by WeatherHandler, not here.
// Note: :SX9A/B/C# (weather set) is handled by WeatherHandler.

#include "HandlerBase.h"

class TelescopeHandler : public HandlerBase {
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
