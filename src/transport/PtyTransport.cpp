// PtyTransport.cpp — Linux PTY transport implementation

#include "PtyTransport.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

// POSIX PTY
#include <stdlib.h>  // posix_openpt, grantpt, unlockpt, ptsname

PtyTransport::~PtyTransport() {
    close();
}

bool PtyTransport::open() {
    // Open PTY master
    m_masterFd = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (m_masterFd < 0) {
        std::perror("posix_openpt");
        return false;
    }

    if (::grantpt(m_masterFd) < 0) {
        std::perror("grantpt");
        ::close(m_masterFd);
        m_masterFd = -1;
        return false;
    }

    if (::unlockpt(m_masterFd) < 0) {
        std::perror("unlockpt");
        ::close(m_masterFd);
        m_masterFd = -1;
        return false;
    }

    // Get slave path
    char* name = ::ptsname(m_masterFd);
    if (!name) {
        std::perror("ptsname");
        ::close(m_masterFd);
        m_masterFd = -1;
        return false;
    }
    std::strncpy(m_slavePath, name, sizeof(m_slavePath) - 1);
    m_slavePath[sizeof(m_slavePath) - 1] = '\0';

    // Configure master as raw (no echo, no line discipline processing)
    struct termios tios;
    if (::tcgetattr(m_masterFd, &tios) == 0) {
        ::cfmakeraw(&tios);
        // 9600 baud — matches common OnStep default; driver sets its own rate
        ::cfsetispeed(&tios, B9600);
        ::cfsetospeed(&tios, B9600);
        ::tcsetattr(m_masterFd, TCSANOW, &tios);
    }

    // Announce slave path on stdout — test harnesses parse this line
    std::printf("SIMULATOR_PTY=%s\n", m_slavePath);
    std::fflush(stdout);

    return true;
}

int PtyTransport::readBytes(char* buf, int maxLen, int timeoutMs) {
    if (m_masterFd < 0 || maxLen <= 0) return -1;

    struct pollfd pfd;
    pfd.fd     = m_masterFd;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, timeoutMs);
    if (ret < 0) {
        if (errno == EINTR) return 0;  // interrupted — treat as timeout
        return -1;
    }
    if (ret == 0) return 0;  // timeout

    if (pfd.revents & (POLLHUP | POLLERR)) {
        // Slave side closed — not fatal; driver may reconnect
        return 0;
    }

    ssize_t n = ::read(m_masterFd, buf, static_cast<size_t>(maxLen));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        return -1;
    }
    return static_cast<int>(n);
}

bool PtyTransport::writeBytes(const char* data, int len) {
    if (m_masterFd < 0 || len <= 0) return false;

    int written = 0;
    while (written < len) {
        ssize_t n = ::write(m_masterFd, data + written,
                            static_cast<size_t>(len - written));
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<int>(n);
    }
    return true;
}

void PtyTransport::close() {
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
}
