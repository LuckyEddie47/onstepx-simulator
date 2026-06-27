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

TEST_F(CommandFramerTest, UnknownCommandReturnsZeroNoHash) {
    // Phase 12: firmware sends "0" (no '#') for a completely unrecognised
    // top-level command — numericReply starts true, CE_CMD_UNKNOWN is returned
    // from ProcessCmds::command(), poll() writes "0" with suppressFrame=true.
    handler.wantHandle = false;
    feedString(":ZZ#");
    EXPECT_EQ(captured, "0");
    EXPECT_EQ(captured.find('#'), std::string::npos);
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

// ---------------------------------------------------------------------------
// Phase 12 — Unknown-command reply matches firmware (audit 1.3)
// ---------------------------------------------------------------------------
//
// Three distinct cases are exercised here, each corresponding to a specific
// path through firmware's poll() in ProcessCmds.cpp:
//
//   Case A  — completely unrecognised top-level command: no handler claims it,
//             numericReply stays true → wire output "0" (no '#').
//             (Tested by UnknownCommandReturnsZeroNoHash above.)
//
//   Case B  — handler claims the prefix (returns true) but encounters an
//             unknown sub-command, with numericReply still true (the default):
//             CE_CMD_UNKNOWN is set → same numeric-reply path → wire "0".
//
//   Case C  — handler claims the prefix, sets *numericReply=false before
//             discovering the unknown sub-parameter, then sets CE_CMD_UNKNOWN:
//             poll() skips the numericReply block; reply="" → strlen=0 →
//             nothing written at all.
//
// Cases B and C use a small stub handler that can simulate either scenario.

class UnknownSubCmdHandler : public HandlerBase {
public:
    // If clearNumericFirst=true the handler sets *numericReply=false before
    // setting CE_CMD_UNKNOWN (simulating Case C).
    // If false it leaves numericReply at its default true (Case B).
    bool clearNumericFirst = false;

    bool handle(
        const char*   cmd,
        const char*   /*param*/,
        char*         /*reply*/,
        bool*         /*suppressFrame*/,
        bool*         numericReply,
        CommandError* error) override
    {
        // Claim only the :ZZ prefix so normal tests still work.
        if (cmd[0] != 'Z' || cmd[1] != 'Z') return false;
        if (clearNumericFirst) *numericReply = false;
        *error = CE_CMD_UNKNOWN;
        return true;   // handler claims the command but marks it unknown
    }
};

class CommandFramerPhase12Test : public SimTestBase {
protected:
    CommandFramer        framer;
    UnknownSubCmdHandler handler;
    SimState             state;
    std::string          captured;

    void SetUp() override {
        SimTestBase::SetUp();
        state.init(cfg);
        framer.setConfig(&cfg);
        framer.setState(&state);
        framer.addHandler(&handler);
    }

    struct StubTransport {
        const char*  data;
        int          len;
        int          pos;
        std::string* out;
        int readBytes(char* buf, int maxLen, int) {
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

    void feedString(const std::string& s) {
        for (unsigned char c : s) {
            char buf[1] = { static_cast<char>(c) };
            StubTransport t{ buf, 1, 0, &captured };
            framer.tick(t, 0);
        }
    }
};

// Case B: handler returns true + CE_CMD_UNKNOWN with numericReply still true
// (the GotoHandler :A[bad-subcmd]# pattern, etc.)
// Firmware → poll() writes "0" (suppressFrame=true), no '#'.
TEST_F(CommandFramerPhase12Test, HandlerSetsUnknownNumericReplyTrueProducesZero) {
    handler.clearNumericFirst = false;
    feedString(":ZZ#");
    EXPECT_EQ(captured, "0")
        << "CE_CMD_UNKNOWN with numericReply=true should produce \"0\" (no '#')";
    EXPECT_EQ(captured.find('#'), std::string::npos);
}

// Case C: handler sets *numericReply=false THEN sets CE_CMD_UNKNOWN.
// Firmware → poll() skips numericReply block; reply="" → strlen=0 → nothing written.
TEST_F(CommandFramerPhase12Test, HandlerSetsUnknownAfterNumericFalseProducesNothing) {
    handler.clearNumericFirst = true;
    feedString(":ZZ#");
    EXPECT_EQ(captured, "")
        << "CE_CMD_UNKNOWN with numericReply=false should produce no output";
}

// ---------------------------------------------------------------------------
// Phase 10 — CommandError numbering matches firmware exactly
// ---------------------------------------------------------------------------
//
// Firmware's CommandError is implicitly ordinal: its integer value is its
// index into commandErrorStr[26] (src/libApp/commands/ProcessCmds.cpp), and
// that integer is wire-visible both directly (a future :GE# command) and
// arithmetically (:MS#'s reply formula,
// (e - CE_SLEW_ERR_BELOW_HORIZON) + '1'). This test pins every value
// against the firmware-documented array index so any future drift away
// from firmware's real numbering is caught immediately, rather than
// silently reintroducing the kind of mismatch Phase 10 fixed (CE_0 and
// CE_CMD_UNKNOWN were swapped relative to firmware in the pre-Phase-10
// enum, among other divergences).

TEST(CommandErrorNumbering, MatchesFirmwareArrayIndexExactly) {
    EXPECT_EQ(static_cast<int>(CE_NONE), 0);
    EXPECT_EQ(static_cast<int>(CE_1), 1);
    EXPECT_EQ(static_cast<int>(CE_0), 2);
    EXPECT_EQ(static_cast<int>(CE_CMD_UNKNOWN), 3);
    EXPECT_EQ(static_cast<int>(CE_REPLY_UNKNOWN), 4);
    EXPECT_EQ(static_cast<int>(CE_PARAM_RANGE), 5);
    EXPECT_EQ(static_cast<int>(CE_PARAM_FORM), 6);
    EXPECT_EQ(static_cast<int>(CE_ALIGN_FAIL), 7);
    EXPECT_EQ(static_cast<int>(CE_ALIGN_NOT_ACTIVE), 8);
    EXPECT_EQ(static_cast<int>(CE_NOT_PARKED_OR_AT_HOME), 9);
    EXPECT_EQ(static_cast<int>(CE_PARKED), 10);
    EXPECT_EQ(static_cast<int>(CE_PARK_FAILED), 11);
    EXPECT_EQ(static_cast<int>(CE_NOT_PARKED), 12);
    EXPECT_EQ(static_cast<int>(CE_NO_PARK_POSITION_SET), 13);
    EXPECT_EQ(static_cast<int>(CE_SLEW_FAIL), 14);
    EXPECT_EQ(static_cast<int>(CE_LIBRARY_FULL), 15);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_BELOW_HORIZON), 16);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_ABOVE_OVERHEAD), 17);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_IN_STANDBY), 18);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_IN_PARK), 19);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_SLEW), 20);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_OUTSIDE_LIMITS), 21);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_HARDWARE_FAULT), 22);
    EXPECT_EQ(static_cast<int>(CE_MOUNT_IN_MOTION), 23);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_UNSPECIFIED), 24);
    EXPECT_EQ(static_cast<int>(CE_UNK), 25);
}

// The :MS#/:CM# reply-character arithmetic formula
// (e - CE_SLEW_ERR_BELOW_HORIZON) + '1', verified directly against
// Goto.command.cpp, only produces the correct '1'-'9' sequence if these
// nine codes are exactly contiguous in this order. This test guards that
// contiguity independently of the absolute-value test above, since a
// future edit could change all nine consistently (preserving relative
// offsets) while still breaking this property.
TEST(CommandErrorNumbering, SlewErrorRangeIsContiguousForMSFormula) {
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_ABOVE_OVERHEAD),
              static_cast<int>(CE_SLEW_ERR_BELOW_HORIZON) + 1);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_IN_STANDBY),
              static_cast<int>(CE_SLEW_ERR_BELOW_HORIZON) + 2);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_IN_PARK),
              static_cast<int>(CE_SLEW_ERR_BELOW_HORIZON) + 3);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_SLEW),
              static_cast<int>(CE_SLEW_ERR_BELOW_HORIZON) + 4);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_OUTSIDE_LIMITS),
              static_cast<int>(CE_SLEW_ERR_BELOW_HORIZON) + 5);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_HARDWARE_FAULT),
              static_cast<int>(CE_SLEW_ERR_BELOW_HORIZON) + 6);
    EXPECT_EQ(static_cast<int>(CE_MOUNT_IN_MOTION),
              static_cast<int>(CE_SLEW_ERR_BELOW_HORIZON) + 7);
    EXPECT_EQ(static_cast<int>(CE_SLEW_ERR_UNSPECIFIED),
              static_cast<int>(CE_SLEW_ERR_BELOW_HORIZON) + 8);
}
