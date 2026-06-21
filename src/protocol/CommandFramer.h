#pragma once
// CommandFramer.h — LX200/OnStepX protocol framer.
//
// Reads raw bytes from PtyTransport, assembles :cmd# frames, dispatches
// to registered handlers, and writes replies back through the transport.
//
// Frame format:
//   :CCPPP...#    C = 2-char command, P = parameter (may be empty)
//   $CCPPP...#    PEC commands start with '$'
//   0x06          LX200 ACK byte — replies 'P' immediately, no '#'
//
// Reply modes (set by handler via out-params):
//   numericReply=true   -> single char '0'/'1', NO '#' appended
//   suppressFrame=true  -> reply text written as-is, NO '#' appended
//   otherwise           -> reply text + '#' appended
//
// Dispatch:
//   Handlers are tried in registered order. First handler returning true
//   owns the command. Unknown command -> sends "2#" (CE_CMD_UNKNOWN).
//
// Phase 6 — Fault injection:
//   If a FaultInjector is registered via setFaultInjector(), dispatchFrame()
//   calls applyPreDispatch() before handler dispatch and applyPostDispatch()
//   before writing the final reply. Both are no-ops when no FaultInjector
//   is registered.

#include "config/SimConfig.h"
#include "state/SimState.h"

#include <cstdint>
#include <functional>
#include <vector>

// Forward declarations
class HandlerBase;
class FaultInjector;

// Command error codes — exact mirror of firmware's CommandError enum and
// its integer values (src/libApp/commands/ProcessCmds.cpp's commandErrorStr[26]
// array — the array index *is* the enum's wire-visible integer value, used
// directly by :GE# and by :MS#'s arithmetic reply-character formula:
// reply = (e - CE_SLEW_ERR_BELOW_HORIZON) + '1'). Verified directly against
// firmware source (Phase 10) rather than assumed; the pre-Phase-10 version
// of this enum had different values for nearly every code from index 2
// onward (CE_0/CE_CMD_UNKNOWN were swapped relative to firmware, among
// other divergences) and several real firmware codes (CE_REPLY_UNKNOWN,
// CE_NOT_PARKED_OR_AT_HOME, CE_PARK_FAILED, CE_NOT_PARKED,
// CE_NO_PARK_POSITION_SET, CE_SLEW_FAIL, CE_LIBRARY_FULL, CE_MOUNT_IN_MOTION)
// were missing entirely. Four simulator-invented codes with no firmware
// equivalent (CE_SLEW_ERR_GOTO_SAME, CE_SLEW_ERR_ALT_MIN, CE_SLEW_ERR_ALT_MAX)
// have been removed — they were either entirely unused (GOTO_SAME) or only
// referenced by dead switch cases (ALT_MIN/ALT_MAX, see MountHandler.cpp's
// gotoErrorChar(), rebuilt in this phase to use firmware's real arithmetic
// formula instead of a hand-built table). CE_SLEW_IN_SLEW is renamed to
// CE_SLEW_ERR_SLEW (firmware's actual name, "already in goto") — its
// current use for a "no target set" condition that has no firmware
// equivalent is intentionally left as-is for now; fixing that precondition
// itself is Phase 11's scope, not this phase's. This phase changes ONLY
// names/values, not behavior.
enum CommandError : uint8_t {
    CE_NONE                     = 0,
    CE_1                        = 1,   // "no error true" — numeric success
    CE_0                        = 2,   // "no error false/fail" — numeric failure
    CE_CMD_UNKNOWN              = 3,
    CE_REPLY_UNKNOWN            = 4,
    CE_PARAM_RANGE              = 5,
    CE_PARAM_FORM               = 6,
    CE_ALIGN_FAIL               = 7,
    CE_ALIGN_NOT_ACTIVE         = 8,
    CE_NOT_PARKED_OR_AT_HOME    = 9,
    CE_PARKED                   = 10,
    CE_PARK_FAILED              = 11,
    CE_NOT_PARKED               = 12,
    CE_NO_PARK_POSITION_SET     = 13,
    CE_SLEW_FAIL                = 14,
    CE_LIBRARY_FULL             = 15,
    CE_SLEW_ERR_BELOW_HORIZON   = 16,
    CE_SLEW_ERR_ABOVE_OVERHEAD  = 17,
    CE_SLEW_ERR_IN_STANDBY      = 18,
    CE_SLEW_ERR_IN_PARK         = 19,
    CE_SLEW_ERR_SLEW            = 20,  // "already in goto"
    CE_SLEW_ERR_OUTSIDE_LIMITS  = 21,
    CE_SLEW_ERR_HARDWARE_FAULT  = 22,
    CE_MOUNT_IN_MOTION          = 23,
    CE_SLEW_ERR_UNSPECIFIED     = 24,
    CE_UNK                      = 25,
};

class CommandFramer {
public:
    CommandFramer() = default;

    // Inject dependencies
    void setConfig(const SimConfig* cfg)  { m_cfg = cfg; }
    void setState(SimState* state)        { m_state = state; }

    // Register a handler (ownership not transferred — caller manages lifetime)
    void addHandler(HandlerBase* h);

    // Register fault injector (optional; nullptr = no fault injection)
    void setFaultInjector(FaultInjector* fi) { m_faultInjector = fi; }

    // Process bytes from transport. Call in a tight loop from main thread.
    // Returns false only on unrecoverable transport error.
    template<typename Transport>
    bool tick(Transport& transport, int timeoutMs = 50) {
        char buf[256];
        int n = transport.readBytes(buf, sizeof(buf), timeoutMs);
        if (n < 0) return false;
        for (int i = 0; i < n; ++i) {
            processByte(static_cast<uint8_t>(buf[i]), transport);
        }
        return true;
    }

    // Expose reply senders for use in unit tests (loopback transport)
    void sendHashReply(const char* text);
    void sendSingleChar(char c);
    void sendNothing();
    void sendRawBytes(const uint8_t* data, int len);

    // Set write callback (used by unit tests instead of real transport)
    using WriteFn = std::function<void(const char*, int)>;
    void setWriteCallback(WriteFn fn) { m_writeFn = fn; }

    // Inject a single byte directly into the framer (unit tests only).
    // Uses the currently set write callback — does NOT replace m_writeFn
    // with a transport lambda, so setWriteCallback() remains in effect.
    void injectByte(uint8_t byte) { processbyteInternal(byte); }

private:
    enum class FrameState { IDLE, IN_FRAME };

    FrameState   m_frameState = FrameState::IDLE;
    char         m_frameBuf[256] = {};
    int          m_frameBufLen   = 0;

    const SimConfig*          m_cfg           = nullptr;
    SimState*                 m_state         = nullptr;
    FaultInjector*            m_faultInjector = nullptr;
    std::vector<HandlerBase*> m_handlers;
    WriteFn                   m_writeFn;

    template<typename Transport>
    void processByte(uint8_t byte, Transport& transport) {
        m_writeFn = [&transport](const char* data, int len) {
            transport.writeBytes(data, len);
        };
        processbyteInternal(byte);
    }

    void processbyteInternal(uint8_t byte);
    void dispatchFrame();
    void writeRaw(const char* data, int len);
};
