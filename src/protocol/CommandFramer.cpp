// CommandFramer.cpp — LX200/OnStepX protocol framer implementation

#include "CommandFramer.h"
#include "fault/FaultInjector.h"
#include "handlers/HandlerBase.h"

#include <cstdio>
#include <cstring>

void CommandFramer::addHandler(HandlerBase* h) {
    m_handlers.push_back(h);
}

// ---------------------------------------------------------------------------
// Byte-by-byte frame parser
// ---------------------------------------------------------------------------

void CommandFramer::processbyteInternal(uint8_t byte) {
    if (byte == 0x06) {
        writeRaw("P", 1);
        return;
    }

    switch (m_frameState) {
    case FrameState::IDLE:
        if (byte == ':' || byte == '$') {
            m_frameBuf[0]  = static_cast<char>(byte);
            m_frameBufLen  = 1;
            m_frameState   = FrameState::IN_FRAME;
        }
        break;

    case FrameState::IN_FRAME:
        if (byte == '#') {
            m_frameBuf[m_frameBufLen] = '\0';
            dispatchFrame();
            m_frameState  = FrameState::IDLE;
            m_frameBufLen = 0;
        } else {
            if (m_frameBufLen < static_cast<int>(sizeof(m_frameBuf)) - 2) {
                m_frameBuf[m_frameBufLen++] = static_cast<char>(byte);
            }
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Frame dispatch
// ---------------------------------------------------------------------------

void CommandFramer::dispatchFrame() {
    if (m_frameBufLen < 2) return;

    char cmd[3];
    if (m_frameBuf[0] == '$') {
        cmd[0] = '$';
        cmd[1] = (m_frameBufLen > 1) ? m_frameBuf[1] : '\0';
    } else {
        cmd[0] = (m_frameBufLen > 1) ? m_frameBuf[1] : '\0';
        cmd[1] = (m_frameBufLen > 2) ? m_frameBuf[2] : '\0';
    }
    cmd[2] = '\0';

    const char* param;
    if (m_frameBuf[0] == '$') {
        param = (m_frameBufLen > 2) ? &m_frameBuf[2] : "";
    } else {
        param = (m_frameBufLen > 3) ? &m_frameBuf[3] : "";
    }

    // -----------------------------------------------------------------------
    // Phase 6 — Pre-dispatch fault injection
    // applyPreDispatch() may:
    //   - Apply a FORCE_STATE mutation to SimState
    //   - Match an ERROR pattern and return a CommandError override
    // -----------------------------------------------------------------------
    CommandError errorOverride = CE_NONE;
    bool errorInjected = false;
    if (m_faultInjector) {
        if (!m_faultInjector->applyPreDispatch(m_frameBuf, &errorOverride)) {
            errorInjected = true;
        }
    }

    // Prepare reply buffer
    char         reply[256]     = {};
    bool         suppressFrame  = false;
    bool         numericReply   = false;
    CommandError error          = errorInjected ? errorOverride : CE_NONE;

    // Dispatch to handlers (skip if error already injected)
    bool handled = errorInjected;  // treat injected error as "handled"
    if (!errorInjected) {
        for (HandlerBase* h : m_handlers) {
            if (h->handle(cmd, param, reply, &suppressFrame, &numericReply, &error)) {
                handled = true;
                break;
            }
        }
    }

    if (!handled || error == CE_CMD_UNKNOWN) {
        writeRaw("2", 1);
        writeRaw("#", 1);
        return;
    }

    // -----------------------------------------------------------------------
    // Build the reply payload into a scratch buffer so that
    // applyPostDispatch() can inspect and optionally modify it.
    // -----------------------------------------------------------------------
    char   outBuf[258] = {};  // max reply + '#' + NUL
    int    outLen      = 0;

    if (numericReply) {
        char c = (error == CE_NONE || error == CE_1) ? '1' : '0';
        // CE_1 means firmware-level "success" numeric; CE_0/others mean failure.
        // Numeric replies: CE_NONE=success('1'), CE_0=ambiguous but treat as '0'
        // when error is explicitly CE_0 and not CE_NONE (they share value 0, so
        // we check reply[0] override first).
        if (reply[0] != '\0') c = reply[0];
        outBuf[0] = c;
        outLen    = 1;
    } else if (suppressFrame) {
        int len = static_cast<int>(std::strlen(reply));
        std::memcpy(outBuf, reply, static_cast<size_t>(len));
        outLen = len;
    } else {
        int len = static_cast<int>(std::strlen(reply));
        std::memcpy(outBuf, reply, static_cast<size_t>(len));
        outBuf[len]   = '#';
        outLen        = len + 1;
    }

    // -----------------------------------------------------------------------
    // Phase 6 — Post-dispatch fault injection
    // applyPostDispatch() may:
    //   - Sleep (TIMEOUT fault)
    //   - Truncate the reply (GARBLE fault) — sets suppressFrame=true
    //   - Return false to suppress the reply entirely (NO_REPLY fault)
    // -----------------------------------------------------------------------
    if (m_faultInjector) {
        if (!m_faultInjector->applyPostDispatch(outBuf, &outLen,
                                                 &suppressFrame, &numericReply)) {
            return;  // NO_REPLY: send nothing
        }
    }

    // Write final bytes
    if (outLen > 0) {
        writeRaw(outBuf, outLen);
    }
}

// ---------------------------------------------------------------------------
// Reply senders
// ---------------------------------------------------------------------------

void CommandFramer::sendHashReply(const char* text) {
    if (text) writeRaw(text, static_cast<int>(std::strlen(text)));
    writeRaw("#", 1);
}

void CommandFramer::sendSingleChar(char c) {
    writeRaw(&c, 1);
}

void CommandFramer::sendNothing() {}

void CommandFramer::sendRawBytes(const uint8_t* data, int len) {
    writeRaw(reinterpret_cast<const char*>(data), len);
}

void CommandFramer::writeRaw(const char* data, int len) {
    if (m_writeFn && len > 0) {
        m_writeFn(data, len);
    }
}
