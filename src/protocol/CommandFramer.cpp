// CommandFramer.cpp — LX200/OnStepX protocol framer implementation

#include "CommandFramer.h"
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
    // LX200 ACK (0x06) — reply 'P' immediately, no frame needed
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
        // Any other byte in IDLE state is discarded
        break;

    case FrameState::IN_FRAME:
        if (byte == '#') {
            // End of frame — dispatch
            m_frameBuf[m_frameBufLen] = '\0';
            dispatchFrame();
            m_frameState  = FrameState::IDLE;
            m_frameBufLen = 0;
        } else {
            if (m_frameBufLen < static_cast<int>(sizeof(m_frameBuf)) - 2) {
                m_frameBuf[m_frameBufLen++] = static_cast<char>(byte);
            }
            // Overflow: silently drop (frame too long — not valid protocol)
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Frame dispatch
// ---------------------------------------------------------------------------

void CommandFramer::dispatchFrame() {
    // m_frameBuf contains e.g. ":GVP" or ":Q" or "$QZ?" (no '#', NUL-terminated)
    //
    // Minimum valid frame: ':' + at least 1 char = length 2.
    // Command key is always 2 chars: cmd[0] and cmd[1].
    // cmd[1] defaults to '\0' when only one command char is present (e.g. ":Q#").
    // This matches firmware behaviour where single-char commands like :Q# are valid.

    if (m_frameBufLen < 2) {
        return;
    }

    // Extract 2-char command key and parameter.
    // ':' frames: buf[0]=':' buf[1]=cmd[0] buf[2]=cmd[1] buf[3..]=param
    // '$' frames: buf[0]='$' buf[1]=cmd[0] buf[2]=cmd[1] buf[3..]=param
    //   e.g. "$QZ?" -> cmd="$Q", param="Z?"
    // Single-char command e.g. ":Q" -> cmd[0]='Q', cmd[1]='\0', param=""

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

    // Prepare reply buffer
    char         reply[256]     = {};
    bool         suppressFrame  = false;
    bool         numericReply   = false;
    CommandError error          = CE_NONE;

    // Try handlers in order
    bool handled = false;
    for (HandlerBase* h : m_handlers) {
        if (h->handle(cmd, param, reply, &suppressFrame, &numericReply, &error)) {
            handled = true;
            break;
        }
    }

    // Build and send reply
    if (!handled || error == CE_CMD_UNKNOWN) {
        // Unknown command -> "2#"
        writeRaw("2", 1);
        writeRaw("#", 1);
        return;
    }

    if (numericReply) {
        // Single char '0' or '1', no '#'
        char c = (error == CE_NONE) ? '1' : '0';
        // Some handlers write their own char into reply[0] to override
        if (reply[0] != '\0') c = reply[0];
        writeRaw(&c, 1);
    } else if (suppressFrame) {
        // No '#' appended — write reply verbatim
        int len = static_cast<int>(std::strlen(reply));
        if (len > 0) writeRaw(reply, len);
    } else {
        // Standard '#'-terminated reply
        int len = static_cast<int>(std::strlen(reply));
        if (len > 0) writeRaw(reply, len);
        writeRaw("#", 1);
    }
}

// ---------------------------------------------------------------------------
// Reply senders (also callable directly from unit tests)
// ---------------------------------------------------------------------------

void CommandFramer::sendHashReply(const char* text) {
    if (text) writeRaw(text, static_cast<int>(std::strlen(text)));
    writeRaw("#", 1);
}

void CommandFramer::sendSingleChar(char c) {
    writeRaw(&c, 1);
}

void CommandFramer::sendNothing() {
    // Intentionally empty
}

void CommandFramer::sendRawBytes(const uint8_t* data, int len) {
    writeRaw(reinterpret_cast<const char*>(data), len);
}

void CommandFramer::writeRaw(const char* data, int len) {
    if (m_writeFn && len > 0) {
        m_writeFn(data, len);
    }
}
