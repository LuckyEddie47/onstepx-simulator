// FaultInjector.cpp — Side-channel fault injection for simulator testing.

#include "fault/FaultInjector.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sstream>
#include <thread>
#include <chrono>

// POSIX / Linux socket headers
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

bool FaultInjector::start(const std::string& sockPath) {
    m_sockPath = sockPath;

    // Remove stale socket file if present
    ::unlink(sockPath.c_str());

    m_serverFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_serverFd < 0) {
        std::perror("FaultInjector: socket");
        return false;
    }

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sockPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(m_serverFd, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        std::perror("FaultInjector: bind");
        ::close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    if (::listen(m_serverFd, 4) < 0) {
        std::perror("FaultInjector: listen");
        ::close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    // Make accept() non-blocking so we can poll for shutdown
    int flags = ::fcntl(m_serverFd, F_GETFL, 0);
    ::fcntl(m_serverFd, F_SETFL, flags | O_NONBLOCK);

    m_running.store(true);
    m_thread = std::thread([this]() { serverThread(); });
    return true;
}

void FaultInjector::stop() {
    m_running.store(false);
    if (m_serverFd >= 0) {
        ::shutdown(m_serverFd, SHUT_RDWR);
        ::close(m_serverFd);
        m_serverFd = -1;
    }
    if (m_thread.joinable()) m_thread.join();
    if (!m_sockPath.empty()) ::unlink(m_sockPath.c_str());
}

// ---------------------------------------------------------------------------
// Background server thread
// ---------------------------------------------------------------------------

void FaultInjector::serverThread() {
    while (m_running.load()) {
        // Poll with a short timeout so we check m_running regularly
        struct pollfd pfd { m_serverFd, POLLIN, 0 };
        int ret = ::poll(&pfd, 1, 100);
        if (ret <= 0) continue;

        int clientFd = ::accept(m_serverFd, nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            break;  // server socket closed
        }
        handleClient(clientFd);
        ::close(clientFd);
    }
}

void FaultInjector::handleClient(int clientFd) {
    // Set a read timeout on the client socket
    struct timeval tv { 5, 0 };
    ::setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&tv), sizeof(tv));

    std::string lineBuf;
    char ch;

    while (m_running.load()) {
        ssize_t n = ::recv(clientFd, &ch, 1, 0);
        if (n <= 0) break;

        if (ch == '\n') {
            // Strip trailing '\r' for Windows clients
            if (!lineBuf.empty() && lineBuf.back() == '\r') lineBuf.pop_back();
            if (!lineBuf.empty()) {
                std::string response = dispatchControlCommand(lineBuf);
                ::send(clientFd, response.c_str(),
                       static_cast<int>(response.size()), MSG_NOSIGNAL);
            }
            lineBuf.clear();
        } else {
            if (lineBuf.size() < 512) lineBuf += ch;
        }
    }
}

// ---------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------

std::string FaultInjector::dispatchControlCommand(const std::string& line) {
    // Tokenise on first space
    auto firstSpace = line.find(' ');
    std::string verb = (firstSpace == std::string::npos)
                       ? line : line.substr(0, firstSpace);
    std::string rest = (firstSpace == std::string::npos)
                       ? "" : line.substr(firstSpace + 1);

    // Normalise verb to uppercase for comparison
    for (char& c : verb) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (verb == "STATUS")  return cmdStatus();
    if (verb == "FAULT") {
        auto sub = rest.find(' ');
        std::string subVerb = (sub == std::string::npos) ? rest : rest.substr(0, sub);
        std::string subRest = (sub == std::string::npos) ? ""   : rest.substr(sub + 1);
        for (char& c : subVerb) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (subVerb == "TIMEOUT")     return cmdFaultTimeout(subRest);
        if (subVerb == "GARBLE")      return cmdFaultGarble(subRest);
        if (subVerb == "NO_REPLY")    return cmdFaultNoReply();
        if (subVerb == "ERROR")       return cmdFaultError(subRest);
        if (subVerb == "AXIS_STATUS") return cmdFaultAxisStatus(subRest);
        if (subVerb == "FORCE_STATE") return cmdFaultForceState(subRest);
        if (subVerb == "CLEAR")       return cmdFaultClear();
        return "ERR: unknown fault sub-command: " + subVerb + "\n";
    }
    return "ERR: unknown command: " + verb + "\n";
}

// ---------------------------------------------------------------------------
// Individual command handlers
// ---------------------------------------------------------------------------

std::string FaultInjector::cmdFaultTimeout(const std::string& rest) {
    int ms = std::atoi(rest.c_str());
    if (ms < 0 || ms > 60000)
        return "ERR: timeout ms must be 0..60000\n";
    std::lock_guard<std::mutex> lk(m_mutex);
    m_fault.timeoutActive = true;
    m_fault.timeoutMs     = ms;
    return "OK\n";
}

std::string FaultInjector::cmdFaultGarble(const std::string& rest) {
    float prob = static_cast<float>(std::atof(rest.c_str()));
    if (prob < 0.0f || prob > 1.0f)
        return "ERR: probability must be 0.0..1.0\n";
    std::lock_guard<std::mutex> lk(m_mutex);
    if (prob == 0.0f) {
        m_fault.garbleActive      = false;
        m_fault.garbleProbability = 0.0f;
    } else {
        m_fault.garbleActive      = true;
        m_fault.garbleProbability = prob;
    }
    return "OK\n";
}

std::string FaultInjector::cmdFaultNoReply() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_fault.noReplyActive = true;
    return "OK\n";
}

std::string FaultInjector::cmdFaultError(const std::string& rest) {
    // rest = "<pattern> <code>"
    auto sp = rest.find(' ');
    if (sp == std::string::npos)
        return "ERR: usage: FAULT ERROR <pattern> <code>\n";
    std::string pattern = rest.substr(0, sp);
    int code = std::atoi(rest.substr(sp + 1).c_str());
    if (code < 0 || code > 255)
        return "ERR: error code must be 0..255\n";
    std::lock_guard<std::mutex> lk(m_mutex);
    m_fault.errorActive  = true;
    m_fault.errorPattern = pattern;
    m_fault.errorCode    = static_cast<CommandError>(code);
    return "OK\n";
}

std::string FaultInjector::cmdFaultAxisStatus(const std::string& rest) {
    // rest = "<n> <flags>"  e.g. "1 OT,GF"
    auto sp = rest.find(' ');
    if (sp == std::string::npos)
        return "ERR: usage: FAULT AXIS_STATUS <n> <flags>\n";
    int n = std::atoi(rest.substr(0, sp).c_str());
    if (n < 1 || n >= FAULT_MAX_AXES)
        return "ERR: axis number must be 1.." + std::to_string(FAULT_MAX_AXES - 1) + "\n";
    std::string flags = rest.substr(sp + 1);
    std::lock_guard<std::mutex> lk(m_mutex);
    m_fault.axisStatus[n] = flags;
    return "OK\n";
}

std::string FaultInjector::cmdFaultForceState(const std::string& rest) {
    MountState ms;
    if      (rest == "PARKED")      ms = MountState::PARKED;
    else if (rest == "PARK_FAILED") ms = MountState::PARK_FAILED;
    else if (rest == "HOMING")      ms = MountState::HOMING;
    else if (rest == "SLEWING")     ms = MountState::SLEWING_GOTO;
    else if (rest == "TRACKING")    ms = MountState::TRACKING;
    else if (rest == "STANDBY")     ms = MountState::STANDBY;
    else
        return "ERR: unknown state: " + rest +
               " (valid: PARKED PARK_FAILED HOMING SLEWING TRACKING STANDBY)\n";

    std::lock_guard<std::mutex> lk(m_mutex);
    m_fault.forceStateActive = true;
    m_fault.forceState       = ms;
    return "OK\n";
}

std::string FaultInjector::cmdFaultClear() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_fault = FaultState{};
    return "OK\n";
}

std::string FaultInjector::cmdStatus() {
    if (!m_state) return "ERR: state not set\n";
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return buildStatusJson(*m_state) + "\n";
}

// ---------------------------------------------------------------------------
// applyPreDispatch — called by CommandFramer before dispatch
// ---------------------------------------------------------------------------

bool FaultInjector::applyPreDispatch(const char* rawFrame, CommandError* errorOut) {
    std::lock_guard<std::mutex> lk(m_mutex);

    // Apply FORCE_STATE (one-shot)
    if (m_fault.forceStateActive && m_state) {
        std::lock_guard<std::mutex> slk(m_state->mutex);
        m_state->mountState      = m_fault.forceState;
        m_fault.forceStateActive = false;
    }

    // Check ERROR pattern (one-shot)
    if (m_fault.errorActive) {
        // Match against raw frame sans leading ':' or '$'
        const char* matchTarget = rawFrame;
        if (*matchTarget == ':' || *matchTarget == '$') ++matchTarget;

        if (globMatch(m_fault.errorPattern.c_str(), matchTarget)) {
            *errorOut            = m_fault.errorCode;
            m_fault.errorActive  = false;
            m_fault.errorPattern.clear();
            return false;  // framer should send error reply
        }
    }

    return true;  // proceed normally
}

// ---------------------------------------------------------------------------
// applyPostDispatch — called by CommandFramer after building reply
// ---------------------------------------------------------------------------

bool FaultInjector::applyPostDispatch(char* replyBuf, int* replyLen,
                                      bool* suppressFrame, bool* numericReply) {
    std::lock_guard<std::mutex> lk(m_mutex);

    // NO_REPLY (one-shot) — suppress entirely
    if (m_fault.noReplyActive) {
        m_fault.noReplyActive = false;
        return false;
    }

    // TIMEOUT (one-shot) — sleep before sending
    if (m_fault.timeoutActive) {
        int ms               = m_fault.timeoutMs;
        m_fault.timeoutActive = false;
        m_fault.timeoutMs    = 0;
        // Release mutex while sleeping
        m_mutex.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        m_mutex.lock();
    }

    // GARBLE (persistent) — randomly truncate the reply
    if (m_fault.garbleActive && *replyLen > 0) {
        applyGarble(replyBuf, replyLen);
        // After garbling, suppress normal frame termination —
        // the mangled bytes are written as-is by the framer
        *suppressFrame  = true;
        *numericReply   = false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// axisStatusOverride
// ---------------------------------------------------------------------------

std::string FaultInjector::axisStatusOverride(int axisNumber) const {
    if (axisNumber < 1 || axisNumber >= FAULT_MAX_AXES) return "";
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_fault.axisStatus[axisNumber];
}

// ---------------------------------------------------------------------------
// Garble: truncate before '#' at a random position
// ---------------------------------------------------------------------------

void FaultInjector::applyGarble(char* replyBuf, int* replyLen) {
    // Simple LCG random using clock as seed (good enough for fault simulation)
    static std::atomic<uint32_t> seed { static_cast<uint32_t>(std::time(nullptr)) };
    uint32_t s = seed.load();
    s = s * 1664525u + 1013904223u;
    seed.store(s);
    float r = static_cast<float>(s & 0xFFFF) / 65535.0f;

    if (r > m_fault.garbleProbability) return;  // no garble this time

    // Truncate at a random point in [0, replyLen-1]
    // Ensure we always produce at least 0 bytes (full suppression counts as garble)
    uint32_t s2 = s * 1664525u + 1013904223u;
    seed.store(s2);
    int truncAt = static_cast<int>(s2 % static_cast<uint32_t>(*replyLen + 1));
    replyBuf[truncAt] = '\0';
    *replyLen = truncAt;
}

// ---------------------------------------------------------------------------
// Glob match — '*' matches any sequence, '?' matches exactly one char
// ---------------------------------------------------------------------------

bool FaultInjector::globMatch(const char* pattern, const char* str) {
    // Classic iterative glob: track last '*' position and retry point
    const char* star  = nullptr;
    const char* match = str;

    while (*str) {
        if (*pattern == '?' || *pattern == *str) {
            ++pattern; ++str;
        } else if (*pattern == '*') {
            star    = pattern++;
            match   = str;
        } else if (star) {
            pattern = star + 1;
            str     = ++match;
        } else {
            return false;
        }
    }
    while (*pattern == '*') ++pattern;
    return *pattern == '\0';
}

// ---------------------------------------------------------------------------
// Status JSON builder
// ---------------------------------------------------------------------------

std::string FaultInjector::buildStatusJson(const SimState& s) {
    // Simple hand-rolled JSON — no external dependencies.
    // Caller holds s.mutex.
    std::ostringstream j;
    j << "{"
      << "\"mountState\":"  << static_cast<int>(s.mountState)  << ","
      << "\"parkState\":"   << static_cast<int>(s.parkState)   << ","
      << "\"isTracking\":"  << (s.isTracking ? "true" : "false") << ","
      << "\"ra\":"          << s.ra    << ","
      << "\"dec\":"         << s.dec   << ","
      << "\"ha\":"          << s.ha    << ","
      << "\"pierSide\":"    << static_cast<int>(s.pierSide) << ","
      << "\"targetRA\":"    << s.targetRA  << ","
      << "\"targetDec\":"   << s.targetDec << ","
      << "\"isAtHome\":"    << (s.isAtHome ? "true" : "false") << ","
      << "\"errorCode\":"   << static_cast<int>(s.errorCode) << ","
      << "\"site\":{"
          << "\"lat\":"  << s.sites[s.currentSite].latitude  << ","
          << "\"lon\":"  << s.sites[s.currentSite].longitude << ","
          << "\"tz\":"   << s.sites[s.currentSite].timezone
      << "},"
      << "\"utcHours\":" << s.utcHours << ","
      << "\"dateReady\":" << (s.dateReady ? "true" : "false") << ","
      << "\"timeReady\":" << (s.timeReady ? "true" : "false");

    // Focuser positions (non-zero slots only, up to 6)
    j << ",\"focusers\":[";
    for (int i = 0; i < 6; ++i) {
        if (i > 0) j << ",";
        j << "{"
          << "\"pos\":"     << s.focuser[i].positionSteps << ","
          << "\"target\":"  << s.focuser[i].targetSteps   << ","
          << "\"moving\":"  << (s.focuser[i].isMoving ? "true" : "false")
          << "}";
    }
    j << "]";

    j << ",\"rotator\":{"
      << "\"angle\":"   << s.rotator.angle       << ","
      << "\"target\":"  << s.rotator.targetAngle << ","
      << "\"moving\":"  << (s.rotator.isMoving ? "true" : "false")
      << "}";

    j << "}";
    return j.str();
}
