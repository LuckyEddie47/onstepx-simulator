#pragma once
// FaultInjector.h — Side-channel fault injection for simulator testing.
//
// Runs a Unix domain socket server (SOCK_STREAM) on a background thread.
// Test harnesses connect and send plain-text commands to inject faults.
//
// Control protocol (newline-delimited, responses end with '\n'):
//
//   FAULT TIMEOUT <ms>             Hold all replies for <ms> ms (one-shot)
//   FAULT GARBLE <probability>     Float 0.0-1.0: truncate reply at this rate (persistent)
//   FAULT NO_REPLY                 Next command: send nothing at all (one-shot)
//   FAULT ERROR <pattern> <code>   Next frame matching glob <pattern>: return CE error (one-shot)
//   FAULT AXIS_STATUS <n> <flags>  Axis n status override e.g. "OT,GF" (persistent)
//   FAULT FORCE_STATE <state>      Force mount state: PARKED|PARK_FAILED|HOMING|SLEWING (one-shot)
//   FAULT CLEAR                    Remove all active faults
//   STATUS                         Return JSON snapshot of current SimState
//
//   Response: "OK\n" or "ERR: <reason>\n"
//
// Thread safety:
//   All fault state is protected by m_mutex.
//   CommandFramer calls applyPreDispatch() and applyPostDispatch() from the
//   main thread; the control socket runs on a separate thread.

#include "state/SimState.h"
#include "protocol/CommandFramer.h"  // for CommandError

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

// Maximum number of axes for per-axis status injection (Axis1..9)
static constexpr int FAULT_MAX_AXES = 10;

// Fault state snapshot — all fields guarded by FaultInjector::m_mutex
struct FaultState {
    // FAULT TIMEOUT <ms> — one-shot: sleep before sending reply
    bool    timeoutActive = false;
    int     timeoutMs     = 0;

    // FAULT GARBLE <probability> — persistent: truncate reply randomly
    bool    garbleActive      = false;
    float   garbleProbability = 0.0f;

    // FAULT NO_REPLY — one-shot: suppress entire reply
    bool    noReplyActive = false;

    // FAULT ERROR <pattern> <code> — one-shot: return CE error for matching frame
    bool    errorActive  = false;
    std::string errorPattern;   // glob pattern matched against raw frame (e.g. "GX*")
    CommandError errorCode = CE_NONE;

    // FAULT AXIS_STATUS <n> <flags> — persistent per-axis override for :GXUn#
    // axisStatus[n] is non-empty when axis n has an injected status string.
    std::string axisStatus[FAULT_MAX_AXES];

    // FAULT FORCE_STATE — one-shot: applied once on next dispatchFrame call
    bool    forceStateActive = false;
    MountState forceState    = MountState::STANDBY;
};

class FaultInjector {
public:
    FaultInjector() = default;
    ~FaultInjector() { stop(); }

    // Inject state dependency (needed for STATUS and FORCE_STATE)
    void setState(SimState* state) { m_state = state; }

    // Start the control socket server on a background thread.
    // sockPath: Unix domain socket path (e.g. "/tmp/onstepx-sim-ctl.sock")
    // Returns false if the socket cannot be created.
    bool start(const std::string& sockPath);

    // Stop the control socket server and join the thread.
    void stop();

    bool isRunning() const { return m_running.load(); }

    // Called by CommandFramer::dispatchFrame() before sending reply.
    //
    // Pre-dispatch: apply FORCE_STATE (mutates SimState) and check ERROR
    // pattern against raw frame. If ERROR matches, sets *errorOut and returns
    // false to signal the framer should send an error reply instead.
    //
    // Returns true  -> proceed normally (errorOut not set)
    // Returns false -> frame matched an error injection; *errorOut is the CE code
    bool applyPreDispatch(const char* rawFrame, CommandError* errorOut);

    // Called by CommandFramer::dispatchFrame() after building the reply string
    // but before writing it. May modify replyBuf, suppress the reply entirely,
    // or sleep (timeout fault).
    //
    // Returns true  -> send reply normally (replyBuf may have been modified)
    // Returns false -> suppress reply entirely (NO_REPLY fault)
    bool applyPostDispatch(char* replyBuf, int* replyLen, bool* suppressFrame, bool* numericReply);

    // Called by AxisHandler to check whether axis n has an injected status string.
    // Returns empty string when no fault is active for that axis.
    std::string axisStatusOverride(int axisNumber) const;

private:
    SimState*         m_state   = nullptr;
    std::atomic<bool> m_running {false};
    std::thread       m_thread;
    std::string       m_sockPath;
    int               m_serverFd = -1;

    mutable std::mutex m_mutex;
    FaultState         m_fault;

    // Background thread: accept connections and dispatch commands.
    void serverThread();

    // Handle one connected client: read lines, dispatch, send responses.
    void handleClient(int clientFd);

    // Parse and apply a single control command line.
    // Returns "OK\n" or "ERR: <reason>\n".
    std::string dispatchControlCommand(const std::string& line);

    // --- individual command handlers ---
    std::string cmdFaultTimeout(const std::string& rest);
    std::string cmdFaultGarble(const std::string& rest);
    std::string cmdFaultNoReply();
    std::string cmdFaultError(const std::string& rest);
    std::string cmdFaultAxisStatus(const std::string& rest);
    std::string cmdFaultForceState(const std::string& rest);
    std::string cmdFaultClear();
    std::string cmdStatus();

    // Build JSON snapshot of SimState (called with m_state->mutex held)
    static std::string buildStatusJson(const SimState& s);

    // Simple glob match: '*' matches any sequence of chars, '?' matches one char.
    static bool globMatch(const char* pattern, const char* str);

    // Apply garble fault: truncate replyBuf at a random position before '#'.
    // replyLen is updated. Does nothing if garble probability check fails.
    void applyGarble(char* replyBuf, int* replyLen);
};
