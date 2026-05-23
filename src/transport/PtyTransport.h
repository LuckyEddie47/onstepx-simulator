#pragma once
// PtyTransport.h — Linux PTY (pseudo-terminal) transport layer.
//
// Creates a master/slave PTY pair. The simulator holds the master fd.
// The INDI driver connects to the slave path (e.g. /dev/pts/7).
//
// Startup output (written to stdout by open()):
//   SIMULATOR_PTY=/dev/pts/N
//
// Thread safety: readBytes/writeBytes may be called from different threads
// but not concurrently with each other on the same fd. The caller (CommandFramer)
// is responsible for serialising access if needed.

#include <cstdint>

class PtyTransport {
public:
    PtyTransport() = default;
    ~PtyTransport();

    // Create PTY pair, print slave path to stdout. Returns false on failure.
    bool open();

    // Slave device path, e.g. "/dev/pts/7". Valid after open() returns true.
    const char* slavePath() const { return m_slavePath; }

    // Read up to maxLen bytes from the master fd. Blocks for up to timeout_ms.
    // Returns number of bytes read, 0 on timeout, -1 on error/EOF.
    int readBytes(char* buf, int maxLen, int timeoutMs);

    // Write exactly len bytes to the master fd.
    // Returns true on success, false on error.
    bool writeBytes(const char* data, int len);

    // Close the master fd.
    void close();

    bool isOpen() const { return m_masterFd >= 0; }

private:
    int  m_masterFd = -1;
    char m_slavePath[64] = {};
};
