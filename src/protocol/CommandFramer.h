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

#include "config/SimConfig.h"
#include "state/SimState.h"

#include <cstdint>
#include <functional>
#include <vector>

// Forward declaration
class HandlerBase;

// Command error codes — mirror firmware CommandError enum
enum CommandError : uint8_t {
    CE_NONE           = 0,
    CE_0              = 0,  // alias — "success" numeric reply
    CE_1              = 1,  // alias — "error" numeric reply
    CE_CMD_UNKNOWN    = 2,
    CE_PARAM_RANGE    = 3,
    CE_PARAM_FORM     = 4,
    CE_ALIGN_FAIL     = 5,
    CE_SLEW_ERR_BELOW_HORIZON  = 6,
    CE_SLEW_ERR_ABOVE_OVERHEAD = 7,
    CE_SLEW_ERR_IN_STANDBY     = 8,
    CE_SLEW_ERR_IN_PARK        = 9,
    CE_SLEW_ERR_GOTO_SAME      = 10,
    CE_SLEW_ERR_OUTSIDE_LIMITS = 11,
    CE_SLEW_ERR_HARDWARE_FAULT = 12,
    CE_SLEW_ERR_ALT_MIN        = 13,
    CE_SLEW_ERR_ALT_MAX        = 14,
};

class CommandFramer {
public:
    CommandFramer() = default;

    // Inject dependencies
    void setConfig(const SimConfig* cfg)  { m_cfg = cfg; }
    void setState(SimState* state)        { m_state = state; }

    // Register a handler (ownership not transferred — caller manages lifetime)
    void addHandler(HandlerBase* h);

    // Process bytes from transport. Call in a tight loop from main thread.
    // transport: object with readBytes(char*, int, int) and writeBytes(const char*, int)
    // Returns false only on unrecoverable transport error.
    template<typename Transport>
    bool tick(Transport& transport, int timeoutMs = 50) {
        char buf[256];
        int n = transport.readBytes(buf, sizeof(buf), timeoutMs);
        if (n < 0) return false;   // transport error
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

private:
    // Byte-by-byte frame parser state
    enum class FrameState { IDLE, IN_FRAME };

    FrameState   m_frameState = FrameState::IDLE;
    char         m_frameBuf[256] = {};
    int          m_frameBufLen   = 0;

    const SimConfig*         m_cfg   = nullptr;
    SimState*                m_state = nullptr;
    std::vector<HandlerBase*> m_handlers;
    WriteFn                  m_writeFn;

    template<typename Transport>
    void processByte(uint8_t byte, Transport& transport) {
        // Set up write function to go through transport
        m_writeFn = [&transport](const char* data, int len) {
            transport.writeBytes(data, len);
        };
        processbyteInternal(byte);
    }

    void processbyteInternal(uint8_t byte);
    void dispatchFrame();
    void writeRaw(const char* data, int len);
};
