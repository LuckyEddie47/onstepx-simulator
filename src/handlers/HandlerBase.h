#pragma once
// HandlerBase.h — Abstract base for all command handlers.
//
// Each concrete handler implements handle() and returns true if it consumed
// the command, false to pass to the next handler in the chain.
//
// Reply convention:
//   reply[]       — NUL-terminated response string (pre-zeroed by dispatcher)
//   suppressFrame — set true to send reply without trailing '#'
//   numericReply  — Phase 12: the dispatcher initialises this TRUE (matching
//                   firmware's poll() in ProcessCmds.cpp). Handlers that
//                   produce '#'-terminated text replies MUST set
//                   *numericReply = false. Handlers that produce numeric
//                   ('0'/'1') replies may leave it true or set it explicitly.
//                   Handler may write an override char into reply[0] when
//                   numericReply=true to send that char instead of '0'/'1'.
//   error         — set to CE_CMD_UNKNOWN when the handler claims the prefix
//                   but the sub-command/parameter is unrecognised. The wire
//                   reply matches firmware (Phase 12):
//                     numericReply=true  → "0" (no '#')
//                     numericReply=false → nothing written

#include "config/SimConfig.h"
#include "state/SimState.h"
#include "protocol/CommandFramer.h"  // for CommandError enum

class HandlerBase {
public:
    virtual ~HandlerBase() = default;

    // Process a command. Return true if this handler owns the command.
    virtual bool handle(
        const char*   cmd,           // 2-char command key, NUL-terminated
        const char*   param,         // parameter string (may be ""), NUL-terminated
        char*         reply,         // reply buffer [256], pre-zeroed
        bool*         suppressFrame, // true -> no '#' appended
        bool*         numericReply,  // true -> single-char reply, no '#'
        CommandError* error          // CE_NONE on entry
    ) = 0;

    void setConfig(const SimConfig* cfg) { m_cfg = cfg; }
    void setState(SimState* state)       { m_state = state; }

protected:
    const SimConfig* m_cfg   = nullptr;
    SimState*        m_state = nullptr;

    // Convenience: set reply to a string
    static void setReply(char* reply, const char* text) {
        if (text) {
            int i = 0;
            while (text[i] && i < 255) { reply[i] = text[i]; ++i; }
            reply[i] = '\0';
        }
    }

    // Convenience: numeric (single-char) success reply
    static void replyNumericOk(char* reply, bool* numericReply) {
        reply[0] = '1';
        *numericReply = true;
    }

    // Convenience: numeric (single-char) failure reply
    static void replyNumericFail(char* reply, bool* numericReply) {
        reply[0] = '0';
        *numericReply = true;
    }
};
