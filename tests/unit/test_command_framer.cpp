// test_command_framer.cpp — CommandFramer unit tests.
//
// Framer tests exercise protocol mechanics only — no config feature dependency.
// All tests run unconditionally regardless of ONSTEPX_SIM_CONFIG (DEC-001).
//
// Bytes are fed through a StubTransport that captures writes into a string.
// The framer's tick() sets m_writeFn on each call to route writes through
// StubTransport.writeBytes(). Do NOT also call setWriteCallback() — that
// would install a second write path and double-capture every write.

#include <gtest/gtest.h>
#include "SimTestBase.h"

#include "protocol/CommandFramer.h"
#include "handlers/HandlerBase.h"

#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Test handler — records the last dispatched command and emits a preset reply
// ---------------------------------------------------------------------------

class EchoHandler : public HandlerBase {
public:
    std::string lastCmd;
    std::string lastParam;

    std::string  replyText;
    bool         wantNumeric       = false;
    bool         wantSuppressFrame = false;
    bool         wantHandle        = true;

    void reset() {
        lastCmd.clear();
        lastParam.clear();
        replyText.clear();
        wantNumeric       = false;
        wantSuppressFrame = false;
        wantHandle        = true;
    }

    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error) override
    {
        (void)error;
        if (!wantHandle) return false;
        lastCmd   = cmd;
        lastParam = param;
        if (!replyText.empty()) {
            std::strncpy(reply, replyText.c_str(), 254);
            reply[254] = '\0';
        }
        *suppressFrame = wantSuppressFrame;
        *numericReply  = wantNumeric;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class CommandFramerTest : public SimTestBase {
protected:
    CommandFramer framer;
    EchoHandler   handler;
    SimState      state;
    std::string   captured;   // bytes captured via StubTransport.writeBytes()

    void SetUp() override {
        SimTestBase::SetUp();
        state.init(cfg);
        framer.setConfig(&cfg);
        framer.setState(&state);
        framer.addHandler(&handler);
        // NOTE: do NOT call framer.setWriteCallback() here.
        // tick() installs its own m_writeFn that routes through StubTransport.
        // A second callback would cause double-capture of every write.
    }

    // Minimal transport: feeds one byte on read, captures all writes
    struct StubTransport {
        const char*  data;
        int          len;
        int          pos;
        std::string* out;

        int readBytes(char* buf, int maxLen, int /*timeoutMs*/) {
            int n = std::min(len - pos, maxLen);
            if (n <= 0) return 0;
            std::memcpy(buf, data + pos, static_cast<size_t>(n));
            pos += n;
            return n;
        }
        bool writeBytes(const char* d, int l) {
            out->append(d, static_cast<size_t>(l));
            return true;
        }
    };

    void feedByte(unsigned char c) {
        char buf[1] = { static_cast<char>(c) };
        StubTransport t{ buf, 1, 0, &captured };
        framer.tick(t, 0);
    }

    void feedString(const std::string& s) {
        for (unsigned char c : s) feedByte(c);
    }

    void clearCapture() { captured.clear(); }
};

// ---------------------------------------------------------------------------
// Frame parsing tests
// ---------------------------------------------------------------------------

TEST_F(CommandFramerTest, SimpleGVPFrame) {
    handler.replyText = "On-Step";
    feedString(":GVP#");
    EXPECT_EQ(handler.lastCmd,   "GV");
    EXPECT_EQ(handler.lastParam, "P");
}

TEST_F(CommandFramerTest, HashReplyAppended) {
    handler.replyText = "On-Step";
    feedString(":GVP#");
    EXPECT_EQ(captured, "On-Step#");
}

TEST_F(CommandFramerTest, NumericReplyNoHash) {
    handler.replyText   = "1";
    handler.wantNumeric = true;
    feedString(":hP#");
    EXPECT_EQ(captured, "1");
    EXPECT_EQ(captured.find('#'), std::string::npos);
}

TEST_F(CommandFramerTest, SuppressFrameNoHash) {
    handler.replyText         = "ABCD";
    handler.wantSuppressFrame = true;
    feedString(":Gu#");
    EXPECT_EQ(captured, "ABCD");
    EXPECT_EQ(captured.find('#'), std::string::npos);
}

TEST_F(CommandFramerTest, UnknownCommandReturns2Hash) {
    handler.wantHandle = false;
    feedString(":ZZ#");
    EXPECT_EQ(captured, "2#");
}

TEST_F(CommandFramerTest, PecDollarFramePrefix) {
    handler.replyText = "I";
    feedString("$QZ?#");
    EXPECT_EQ(handler.lastCmd,   "$Q");
    EXPECT_EQ(handler.lastParam, "Z?");
}

TEST_F(CommandFramerTest, LongParameterParsed) {
    handler.replyText = "0";
    feedString(":MS+10:30:00.0,+45:00:00#");
    EXPECT_EQ(handler.lastCmd,   "MS");
    EXPECT_EQ(handler.lastParam, "+10:30:00.0,+45:00:00");
}

TEST_F(CommandFramerTest, SplitFrameAcrossMultipleReads) {
    handler.replyText = "On-Step";
    feedString(":GV");
    EXPECT_TRUE(handler.lastCmd.empty()) << "Partial frame should not dispatch";
    feedString("P");
    EXPECT_TRUE(handler.lastCmd.empty()) << "Still no '#' — should not dispatch";
    feedString("#");
    EXPECT_EQ(handler.lastCmd, "GV") << "Complete frame should dispatch after '#'";
}

TEST_F(CommandFramerTest, AckByteRepliesP) {
    feedByte(0x06);
    EXPECT_EQ(captured, "P");
}

TEST_F(CommandFramerTest, AckByteNoHash) {
    feedByte(0x06);
    EXPECT_EQ(captured.find('#'), std::string::npos);
}

TEST_F(CommandFramerTest, GarbageBeforeFrameIgnored) {
    handler.replyText = "X";
    feedString("garbage:GVP#");
    EXPECT_EQ(handler.lastCmd, "GV");
    EXPECT_EQ(captured, "X#");
}

TEST_F(CommandFramerTest, EmptyParameterParsed) {
    handler.replyText = "GN0";
    feedString(":GU#");
    EXPECT_EQ(handler.lastCmd,   "GU");
    EXPECT_EQ(handler.lastParam, "");
}

TEST_F(CommandFramerTest, TwoConsecutiveFrames) {
    handler.replyText = "A";
    feedString(":GVP#");
    std::string first = captured;
    clearCapture();

    handler.replyText = "B";
    handler.lastCmd.clear();
    feedString(":GVN#");

    EXPECT_EQ(first,    "A#");
    EXPECT_EQ(captured, "B#");
    EXPECT_EQ(handler.lastCmd,  "GV");
    EXPECT_EQ(handler.lastParam, "N");
}

TEST_F(CommandFramerTest, SXCommandParsed) {
    handler.replyText   = "1";
    handler.wantNumeric = true;
    feedString(":SX97,1#");
    EXPECT_EQ(handler.lastCmd,   "SX");
    EXPECT_EQ(handler.lastParam, "97,1");
}

TEST_F(CommandFramerTest, BlindCommandSendsHashOnly) {
    // Handler returns true, reply="" numericReply=false suppressFrame=false.
    // Dispatcher always appends '#' — produces just "#".
    // The driver issues blind commands without reading a reply, so this '#'
    // is harmless: it sits in the PTY buffer and is discarded or consumed
    // by the next read. The key contract is no exception and no extra data.
    handler.replyText         = "";
    handler.wantNumeric       = false;
    handler.wantSuppressFrame = false;
    feedString(":Q#");
    EXPECT_EQ(captured, "#");
}
