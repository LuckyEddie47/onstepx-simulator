// test_fault_injector.cpp — Unit tests for FaultInjector.
//
// Tests connect to the control socket directly using a loopback Unix socket
// client, send control commands, and verify the fault state via:
//   - StatusHandler behaviour (for FORCE_STATE)
//   - Direct inspection of SimState (for FORCE_STATE)
//   - CommandFramer loopback (for TIMEOUT, GARBLE, NO_REPLY, ERROR)
//   - AxisHandler loopback (for AXIS_STATUS)
//
// The FaultInjector is started on a unique per-test socket path to avoid
// conflicts between parallel ctest runs.
//
// All tests apply regardless of config — fault injection is config-independent.

#include "SimTestBase.h"
#include "fault/FaultInjector.h"
#include "protocol/CommandFramer.h"
#include "handlers/AxisHandler.h"
#include "handlers/FirmwareHandler.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

// POSIX socket client helpers
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Helper: connect to a Unix domain socket, send a line, read response.
// ---------------------------------------------------------------------------
static std::string sendCtlCommand(const std::string& sockPath,
                                  const std::string& cmd) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return "ERR: socket()\n";

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                  sizeof(addr)) < 0) {
        ::close(fd);
        return "ERR: connect()\n";
    }

    std::string line = cmd + "\n";
    ::send(fd, line.c_str(), static_cast<int>(line.size()), MSG_NOSIGNAL);

    // Read response up to '\n'
    std::string resp;
    char ch;
    struct timeval tv {2, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));
    while (true) {
        ssize_t n = ::recv(fd, &ch, 1, 0);
        if (n <= 0) break;
        resp += ch;
        if (ch == '\n') break;
    }
    ::close(fd);
    return resp;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class FaultInjectorTest : public SimTestBase {
protected:
    SimState       simState;
    FaultInjector  fi;
    std::string    sockPath;

    // CommandFramer loopback for testing reply-level faults
    CommandFramer  framer;
    std::string    lastReply;

    FirmwareHandler firmwareHandler;
    AxisHandler     axisHandler;

    void SetUp() override {
        SimTestBase::SetUp();
        simState.init(cfg);

        // Unique socket path per test process + test name
        sockPath = "/tmp/onstepx-test-ctl-" + std::to_string(::getpid()) + ".sock";

        fi.setState(&simState);
        ASSERT_TRUE(fi.start(sockPath));

        // Give the server thread time to reach accept()
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        firmwareHandler.setConfig(&cfg);
        firmwareHandler.setState(&simState);
        axisHandler.setConfig(&cfg);
        axisHandler.setState(&simState);
        axisHandler.setFaultInjector(&fi);

        framer.setConfig(&cfg);
        framer.setState(&simState);
        framer.setFaultInjector(&fi);
        // AxisHandler must be before FirmwareHandler so :GXU* reaches it.
        // FirmwareHandler handles :GVP# and similar for fault-level tests.
        framer.addHandler(&axisHandler);
        framer.addHandler(&firmwareHandler);

        lastReply.clear();
        // Set write callback once — injectByte() does not replace it.
        framer.setWriteCallback([this](const char* data, int len) {
            lastReply.append(data, static_cast<size_t>(len));
        });
    }

    void TearDown() override {
        fi.stop();
        ::unlink(sockPath.c_str());
    }

    // Feed a raw string (including ':' and '#') byte by byte to the framer.
    // Uses injectByte() which calls processbyteInternal() directly without
    // replacing m_writeFn, so the setWriteCallback() set in SetUp() stays
    // in effect for the lifetime of the test.
    void simulateFrame(const std::string& raw) {
        lastReply.clear();
        for (unsigned char c : raw) {
            framer.injectByte(c);
        }
    }
};

// ---------------------------------------------------------------------------
// Control socket: basic connectivity
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, SocketAcceptsConnection) {
    std::string resp = sendCtlCommand(sockPath, "FAULT CLEAR");
    EXPECT_EQ(resp, "OK\n");
}

TEST_F(FaultInjectorTest, UnknownCommand_ReturnsErr) {
    std::string resp = sendCtlCommand(sockPath, "BOGUS");
    EXPECT_EQ(resp.substr(0, 3), "ERR");
}

// ---------------------------------------------------------------------------
// FAULT CLEAR
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, FaultClear_ResetsAllFaults) {
    sendCtlCommand(sockPath, "FAULT TIMEOUT 5000");
    sendCtlCommand(sockPath, "FAULT GARBLE 1.0");
    sendCtlCommand(sockPath, "FAULT NO_REPLY");
    sendCtlCommand(sockPath, "FAULT AXIS_STATUS 1 OT,GF");

    std::string resp = sendCtlCommand(sockPath, "FAULT CLEAR");
    EXPECT_EQ(resp, "OK\n");

    // After clear, a normal frame should get a normal reply
    simulateFrame(":GVP#");
    EXPECT_FALSE(lastReply.empty()) << "Expected reply after FAULT CLEAR";
}

// ---------------------------------------------------------------------------
// STATUS command
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, Status_ReturnsJson) {
    std::string resp = sendCtlCommand(sockPath, "STATUS");
    EXPECT_NE(resp.find("mountState"), std::string::npos)
        << "STATUS should return JSON with mountState: " << resp;
    EXPECT_NE(resp.find("isTracking"), std::string::npos);
    EXPECT_NE(resp.find("ra"), std::string::npos);
}

TEST_F(FaultInjectorTest, Status_ReflectsStateChange) {
    {
        std::lock_guard<std::mutex> lk(simState.mutex);
        simState.ra = 12.345;
    }
    std::string resp = sendCtlCommand(sockPath, "STATUS");
    EXPECT_NE(resp.find("12.345"), std::string::npos)
        << "STATUS should reflect current RA: " << resp;
}

// ---------------------------------------------------------------------------
// FAULT TIMEOUT
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, FaultTimeout_DelaysReply) {
    sendCtlCommand(sockPath, "FAULT TIMEOUT 150");

    auto t0 = std::chrono::steady_clock::now();
    simulateFrame(":GVP#");
    auto t1 = std::chrono::steady_clock::now();

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_GE(elapsedMs, 140L) << "Reply should be delayed by ~150ms";
    EXPECT_FALSE(lastReply.empty()) << "Reply should still arrive after timeout";
}

TEST_F(FaultInjectorTest, FaultTimeout_IsOneShot) {
    sendCtlCommand(sockPath, "FAULT TIMEOUT 100");

    simulateFrame(":GVP#");  // first: delayed
    lastReply.clear();

    auto t0 = std::chrono::steady_clock::now();
    simulateFrame(":GVP#");  // second: not delayed
    auto t1 = std::chrono::steady_clock::now();

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_LT(elapsedMs, 80L) << "Second reply should not be delayed";
}

// ---------------------------------------------------------------------------
// FAULT NO_REPLY
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, FaultNoReply_SuppressesReply) {
    sendCtlCommand(sockPath, "FAULT NO_REPLY");
    simulateFrame(":GVP#");
    EXPECT_TRUE(lastReply.empty()) << "Reply should be suppressed";
}

TEST_F(FaultInjectorTest, FaultNoReply_IsOneShot) {
    sendCtlCommand(sockPath, "FAULT NO_REPLY");
    simulateFrame(":GVP#");  // suppressed
    simulateFrame(":GVP#");  // should reply normally
    EXPECT_FALSE(lastReply.empty()) << "Second reply should not be suppressed";
}

// ---------------------------------------------------------------------------
// FAULT GARBLE
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, FaultGarble_100pct_AlwaysTruncates) {
    sendCtlCommand(sockPath, "FAULT GARBLE 1.0");

    // With probability=1.0, every reply is truncated — the '#' will be missing
    // or the reply will be shorter than normal.
    simulateFrame(":GVP#");
    // A normal :GVP# reply would be something like "OnStepX#" (several chars + '#').
    // After garble, it may be empty or lack the trailing '#'.
    // Just verify the framer didn't crash and the reply is likely shorter.
    // (We can't assert exact length as truncation point is random.)
    // Verify garble is active by checking a non-trivial frame multiple times.
    bool sawTruncation = false;
    for (int i = 0; i < 10; ++i) {
        simulateFrame(":GVP#");
        if (lastReply.empty() || lastReply.back() != '#') {
            sawTruncation = true;
            break;
        }
    }
    EXPECT_TRUE(sawTruncation) << "GARBLE 1.0 should truncate at least one reply in 10 tries";
}

TEST_F(FaultInjectorTest, FaultGarble_0pct_NeverTruncates) {
    sendCtlCommand(sockPath, "FAULT GARBLE 0.0");
    // 0.0 should disable garble
    for (int i = 0; i < 5; ++i) {
        simulateFrame(":GVP#");
        // Reply should be properly framed
        EXPECT_FALSE(lastReply.empty()) << "GARBLE 0.0 should not suppress replies";
    }
}

TEST_F(FaultInjectorTest, FaultGarble_InvalidProbability_ReturnsErr) {
    std::string resp = sendCtlCommand(sockPath, "FAULT GARBLE 2.5");
    EXPECT_EQ(resp.substr(0, 3), "ERR");
}

// ---------------------------------------------------------------------------
// FAULT ERROR
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, FaultError_MatchingPattern_InjectsError) {
    // Inject CE_CMD_UNKNOWN for any GV* command.
    // Phase 10: CE_CMD_UNKNOWN's numeric value is 3 (renumbered to match firmware).
    // Phase 12: the wire reply for CE_CMD_UNKNOWN is now "0" (no '#'), matching
    // firmware's poll() logic (numericReply=true path → "0" with suppressFrame=true).
    // Pre-Phase-12 the simulator incorrectly sent "2#".
    sendCtlCommand(sockPath, "FAULT ERROR GV* 3");

    simulateFrame(":GVP#");
    // CE_CMD_UNKNOWN → framer sends "0" (no '#')
    EXPECT_EQ(lastReply, "0") << "Error-injected reply should be '0' (no '#')";
}

TEST_F(FaultInjectorTest, FaultError_NonMatchingPattern_NoEffect) {
    // Inject error only for "XX*" pattern — :GVP# should not match
    sendCtlCommand(sockPath, "FAULT ERROR XX* 3");

    simulateFrame(":GVP#");
    EXPECT_NE(lastReply, "0") << "Non-matching pattern should not inject error";
    EXPECT_FALSE(lastReply.empty());
}

TEST_F(FaultInjectorTest, FaultError_IsOneShot) {
    sendCtlCommand(sockPath, "FAULT ERROR GV* 3");

    simulateFrame(":GVP#");  // first: error injected
    EXPECT_EQ(lastReply, "0");

    simulateFrame(":GVP#");  // second: normal reply (FirmwareHandler returns "On-Step#")
    EXPECT_NE(lastReply, "0");
}

TEST_F(FaultInjectorTest, FaultError_WildcardMatchesSingleChar) {
    // '?' matches exactly one character
    sendCtlCommand(sockPath, "FAULT ERROR GV? 3");
    simulateFrame(":GVP#");  // "GVP" — 'G','V','P' — pattern "GV?" matches
    EXPECT_EQ(lastReply, "0");
}

// ---------------------------------------------------------------------------
// FAULT AXIS_STATUS
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, FaultAxisStatus_InjectsStatusString) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount axes in this config";

    sendCtlCommand(sockPath, "FAULT AXIS_STATUS 1 OT,GF");

    simulateFrame(":GXU1#");
    EXPECT_EQ(lastReply, "OT,GF#")
        << "Injected axis status should appear in :GXUn# reply";
}

TEST_F(FaultInjectorTest, FaultAxisStatus_ClearedByFaultClear) {
    if (!cfg.hasMount) GTEST_SKIP() << "No mount axes in this config";

    sendCtlCommand(sockPath, "FAULT AXIS_STATUS 1 OT,GF");
    sendCtlCommand(sockPath, "FAULT CLEAR");

    simulateFrame(":GXU1#");
    EXPECT_EQ(lastReply, "#")
        << "After FAULT CLEAR, :GXUn# should return empty (DEC-018)";
}

TEST_F(FaultInjectorTest, FaultAxisStatus_InvalidAxis_ReturnsErr) {
    std::string resp = sendCtlCommand(sockPath, "FAULT AXIS_STATUS 0 OT");
    EXPECT_EQ(resp.substr(0, 3), "ERR");
}

// ---------------------------------------------------------------------------
// FAULT FORCE_STATE
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, FaultForceState_Parked_UpdatesSimState) {
    sendCtlCommand(sockPath, "FAULT FORCE_STATE PARKED");

    // Force state is applied on the next frame dispatch
    simulateFrame(":GVP#");

    std::lock_guard<std::mutex> lk(simState.mutex);
    EXPECT_EQ(simState.mountState, MountState::PARKED)
        << "FORCE_STATE PARKED should set mountState to PARKED";
}

TEST_F(FaultInjectorTest, FaultForceState_Tracking_UpdatesSimState) {
    // Start from a non-tracking state
    { std::lock_guard<std::mutex> lk(simState.mutex); simState.mountState = MountState::STANDBY; }

    sendCtlCommand(sockPath, "FAULT FORCE_STATE TRACKING");
    simulateFrame(":GVP#");

    std::lock_guard<std::mutex> lk(simState.mutex);
    EXPECT_EQ(simState.mountState, MountState::TRACKING);
}

TEST_F(FaultInjectorTest, FaultForceState_IsOneShot) {
    sendCtlCommand(sockPath, "FAULT FORCE_STATE PARKED");
    simulateFrame(":GVP#");  // applies FORCE_STATE

    // Manually change state
    { std::lock_guard<std::mutex> lk(simState.mutex); simState.mountState = MountState::STANDBY; }

    simulateFrame(":GVP#");  // second frame: should NOT re-apply

    std::lock_guard<std::mutex> lk(simState.mutex);
    EXPECT_EQ(simState.mountState, MountState::STANDBY)
        << "FORCE_STATE should be one-shot";
}

TEST_F(FaultInjectorTest, FaultForceState_InvalidState_ReturnsErr) {
    std::string resp = sendCtlCommand(sockPath, "FAULT FORCE_STATE FLYING");
    EXPECT_EQ(resp.substr(0, 3), "ERR");
}

// ---------------------------------------------------------------------------
// FAULT TIMEOUT invalid value
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, FaultTimeout_InvalidMs_ReturnsErr) {
    std::string resp = sendCtlCommand(sockPath, "FAULT TIMEOUT -1");
    EXPECT_EQ(resp.substr(0, 3), "ERR");
}

// ---------------------------------------------------------------------------
// Multiple faults: GARBLE + TIMEOUT don't interfere with CLEAR
// ---------------------------------------------------------------------------

TEST_F(FaultInjectorTest, MultipleFaults_ClearResetsAll) {
    sendCtlCommand(sockPath, "FAULT TIMEOUT 5000");
    sendCtlCommand(sockPath, "FAULT GARBLE 1.0");
    sendCtlCommand(sockPath, "FAULT AXIS_STATUS 2 ST");
    sendCtlCommand(sockPath, "FAULT CLEAR");

    auto t0 = std::chrono::steady_clock::now();
    simulateFrame(":GVP#");
    auto t1 = std::chrono::steady_clock::now();

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    EXPECT_LT(elapsedMs, 200L) << "After FAULT CLEAR, no timeout should apply";
    EXPECT_FALSE(lastReply.empty()) << "After FAULT CLEAR, reply should not be suppressed";
}
