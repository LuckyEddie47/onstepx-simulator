// TelescopeHandler.cpp — Miscellaneous telescope command handler.
//
// Protocol source: Telescope.command.cpp

#include "handlers/TelescopeHandler.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <cstdlib>

bool TelescopeHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    // Phase 12: numericReply starts true; all paths in this handler either
    // suppress the frame (no-reply blind commands) or produce '#'-terminated
    // text, so clear numericReply unconditionally. The one exception is
    // :ESPFLASH# which sets CE_CMD_UNKNOWN — that path also clears it here,
    // which means firmware would write nothing (Case C); acceptable since
    // :ESPFLASH# is a no-op stub with no expected reply.
    *numericReply = false;

    // :B+# / :B-# — reticle brightness; no reply
    if (cmd[0] == 'B') {
        if ((cmd[1] == '+' || cmd[1] == '-') && param[0] == '\0') {
            *suppressFrame = true;
            reply[0] = '\0';
            return true;
        }
        return false;
    }

    if (cmd[0] != 'E') return false;

    // :EC[s]# — echo string s to stderr; no reply
    if (cmd[1] == 'C') {
        std::fprintf(stderr, "[sim] echo: %s\n", param);
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // :ERESET# — simulate firmware reset
    // param contains "ESET" (the framer gives us cmd="ER", param="ESET")
    // Wait — the framer splits ":ERESET#" as cmd[0]='E', cmd[1]='R', param="ESET"
    if (cmd[1] == 'R' && std::strcmp(param, "ESET") == 0) {
        // Announce reset to stdout so test harnesses can detect it
        std::printf("SIMULATOR_RESET\n");
        std::fflush(stdout);
        // Short delay then exit cleanly
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::exit(0);
        return true;  // unreachable but satisfies compiler
    }

    // :ENVRESET# — NV clear on next boot message
    // Framer: cmd="EN", param="VRESET"
    if (cmd[1] == 'N' && std::strcmp(param, "VRESET") == 0) {
        std::strncpy(reply,
            "NV memory will be cleared on the next boot.",
            255);
        reply[255] = '\0';
        return true;
    }

    // :ESPFLASH# — not simulated
    // Framer: cmd="ES", param="PFLASH"
    if (cmd[1] == 'S' && std::strcmp(param, "PFLASH") == 0) {
        *error = CE_CMD_UNKNOWN;
        return true;
    }

    return false;
}
