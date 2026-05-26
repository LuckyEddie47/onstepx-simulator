// PecHandler.cpp — PEC command handler.
//
// Protocol source: Pec.command.cpp

#include "handlers/PecHandler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

bool PecHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    *suppressFrame = false;

    // -----------------------------------------------------------------------
    // GX — Get extended values
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G' && cmd[1] == 'X' && param[2] == '\0') {

        // :GX91# — PEC analog value (hasPec only, always 0)
        if (param[0] == '9' && param[1] == '1') {
            if (!m_cfg->hasPec) return false;
            std::snprintf(reply, 256, "%d", 0);
            *numericReply = false;
            return true;
        }

        // :GXE6# — Steps per sidereal second (always)
        if (param[0] == 'E' && param[1] == '6') {
            std::snprintf(reply, 256, "%0.6f", stepsPerSiderealSecond());
            *numericReply = false;
            return true;
        }

        // :GXE7# — Worm rotation steps (always)
        if (param[0] == 'E' && param[1] == '7') {
            long steps;
            { std::lock_guard<std::mutex> lk(m_state->mutex); steps = m_state->pecWormSteps; }
            std::snprintf(reply, 256, "%ld", steps);
            *numericReply = false;
            return true;
        }

        // :GXE8# — PEC buffer size in seconds (always)
        if (param[0] == 'E' && param[1] == '8') {
            std::snprintf(reply, 256, "%ld", bufferSize());
            *numericReply = false;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // SX — Set extended values
    // -----------------------------------------------------------------------
    if (cmd[0] == 'S' && cmd[1] == 'X' &&
        param[0] == 'E' && param[1] == '7' && param[2] == ',') {
        if (!m_cfg->hasPec) return false;
        long l = std::atol(&param[3]);
        if (l < 0 || l >= 129600000L) {
            *error = CE_PARAM_RANGE;
        } else {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            m_state->pecWormSteps = l;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // V — PEC readout
    // -----------------------------------------------------------------------
    if (cmd[0] == 'V') {

        // :VH# — PEC index sense position in sidereal seconds (hasPec only)
        if (cmd[1] == 'H' && param[0] == '\0') {
            if (!m_cfg->hasPec) return false;
            long s;
            long wormSecs = bufferSize();
            {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                s = m_state->pecBufferIndex;
            }
            while (s > wormSecs) s -= wormSecs;
            while (s < 0)        s += wormSecs;
            std::snprintf(reply, 256, "%05ld", s);
            *numericReply = false;
            return true;
        }

        // :VR[n]# or :VR# — Read PEC table entry (hasPec only)
        if (cmd[1] == 'R') {
            if (!m_cfg->hasPec) return false;
            *numericReply = false;
            long wormSecs = bufferSize();
            std::lock_guard<std::mutex> lk(m_state->mutex);
            if (param[0] == '\0') {
                long i = m_state->pecBufferIndex - 1;
                if (i < 0) i += wormSecs;
                if (i >= wormSecs) i -= wormSecs;
                int8_t j = (i >= 0 && i < 720) ? m_state->pecBuffer[i] : 0;
                std::snprintf(reply, 256, "%+04d,%03ld", static_cast<int>(j), i);
            } else {
                long i = std::atol(param);
                if (i < 0 || i >= wormSecs) {
                    *error = CE_PARAM_RANGE;
                } else {
                    int8_t j = (i < 720) ? m_state->pecBuffer[i] : 0;
                    std::snprintf(reply, 256, "%+04d", static_cast<int>(j));
                }
            }
            return true;
        }

        // :Vr[n]# — Read 10-byte hex frame (hasPec only)
        if (cmd[1] == 'r') {
            if (!m_cfg->hasPec) return false;
            *numericReply = false;
            long wormSecs = bufferSize();
            long i = std::atol(param);
            if (i < 0 || i >= wormSecs) {
                *error = CE_PARAM_RANGE;
                return true;
            }
            std::lock_guard<std::mutex> lk(m_state->mutex);
            char s[3] = "  ";
            for (int j = 0; j < 10; ++j) {
                long idx = i + j;
                uint8_t b = (idx < wormSecs && idx < 720)
                            ? static_cast<uint8_t>(m_state->pecBuffer[idx] + 128)
                            : 128;
                std::snprintf(s, sizeof(s), "%02X", b);
                std::strcat(reply, s);
            }
            return true;
        }

        // :VS# — Steps per sidereal second (always)
        if (cmd[1] == 'S' && param[0] == '\0') {
            std::snprintf(reply, 256, "%0.6f", stepsPerSiderealSecond());
            *numericReply = false;
            return true;
        }

        // :VW# — Worm rotation steps (always)
        if (cmd[1] == 'W' && param[0] == '\0') {
            long steps = 0;
            if (m_cfg->hasPec) {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                steps = m_state->pecWormSteps;
            }
            std::snprintf(reply, 256, "%06ld", steps);
            *numericReply = false;
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // W — PEC write (hasPec only)
    // -----------------------------------------------------------------------
    if (cmd[0] == 'W') {
        if (!m_cfg->hasPec) return false;

        long wormSecs = bufferSize();
        std::lock_guard<std::mutex> lk(m_state->mutex);

        // :WR+# — Shift table forward one second
        if (cmd[1] == 'R' && param[0] == '+' && param[1] == '\0') {
            if (wormSecs > 1 && wormSecs <= 720) {
                int8_t last = m_state->pecBuffer[wormSecs - 1];
                std::memmove(&m_state->pecBuffer[1], &m_state->pecBuffer[0], wormSecs - 1);
                m_state->pecBuffer[0] = last;
            }
            return true;
        }

        // :WR-# — Shift table back one second
        if (cmd[1] == 'R' && param[0] == '-' && param[1] == '\0') {
            if (wormSecs > 1 && wormSecs <= 720) {
                int8_t first = m_state->pecBuffer[0];
                std::memmove(&m_state->pecBuffer[0], &m_state->pecBuffer[1], wormSecs - 1);
                m_state->pecBuffer[wormSecs - 1] = first;
            }
            return true;
        }

        // :WR[n,sn]# — Write entry
        if (cmd[1] == 'R') {
            *numericReply = false;
            const char* comma = std::strchr(param, ',');
            if (!comma) { *error = CE_PARAM_FORM; return true; }
            long i = std::atol(param);
            long j = std::atol(comma + 1);
            if (i < 0 || i >= wormSecs) { *error = CE_PARAM_RANGE; return true; }
            if (j < -128 || j > 127)    { *error = CE_PARAM_RANGE; return true; }
            if (i < 720) {
                m_state->pecBuffer[i] = static_cast<int8_t>(j);
                m_state->pecRecorded  = true;
            }
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // $QZ — PEC control
    // -----------------------------------------------------------------------
    if (cmd[0] == '$' && cmd[1] == 'Q' &&
        param[0] == 'Z' && param[2] == '\0') {
        *numericReply = false;

        // :$QZ?# — Get PEC state (always, regardless of hasPec)
        if (param[1] == '?') {
            const char* stateStr = "IpPrR";
            uint8_t st = 0;
            bool    wormIdx = false;
            if (m_cfg->hasPec) {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                st      = static_cast<uint8_t>(m_state->pecState);
                wormIdx = m_state->wormIndexSense;
            }
            reply[0] = stateStr[st < 5 ? st : 0];
            reply[1] = wormIdx ? '.' : '\0';
            reply[2] = '\0';
            return true;
        }

        // All remaining $QZ variants require hasPec
        if (!m_cfg->hasPec) return false;

        std::lock_guard<std::mutex> lk(m_state->mutex);

        // :$QZ+# — Enable playback
        if (param[1] == '+') {
            if (m_state->pecState == PecState::NONE && m_state->pecRecorded)
                m_state->pecState = PecState::READY_PLAY;
            else
                *error = CE_0;
            return true;
        }

        // :$QZ-# — Disable PEC
        if (param[1] == '-') {
            m_state->pecState = PecState::NONE;
            return true;
        }

        // :$QZ/# — Ready record
        if (param[1] == '/') {
            if (m_state->pecState == PecState::NONE && m_state->isTracking)
                m_state->pecState = PecState::READY_RECORD;
            else
                *error = CE_0;
            return true;
        }

        // :$QZZ# — Clear buffer
        if (param[1] == 'Z') {
            std::memset(m_state->pecBuffer, 0, sizeof(m_state->pecBuffer));
            m_state->pecState    = PecState::NONE;
            m_state->pecRecorded = false;
            return true;
        }

        // :$QZ!# — Write to NV (sim: mark as recorded)
        if (param[1] == '!') {
            m_state->pecRecorded = true;
            return true;
        }

        return false;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

long PecHandler::bufferSize() const {
    double spss = stepsPerSiderealSecond();
    if (spss <= 0.0) return 720;
    long steps;
    { std::lock_guard<std::mutex> lk(m_state->mutex); steps = m_state->pecWormSteps; }
    if (steps <= 0) return 720;
    long sz = static_cast<long>(steps / spss + 0.5);
    return (sz > 720) ? 720 : (sz < 1 ? 1 : sz);
}

double PecHandler::stepsPerSiderealSecond() const {
    // Mirrors firmware: (axis1.getStepsPerMeasure() / RAD_DEG_RATIO) / 240
    // stepsPerMeasure = stepsPerDegree[0]; RAD_DEG_RATIO = 180/π
    double spd = m_cfg->stepsPerDegree[0];
    return spd / (180.0 / 3.14159265358979323846) / 240.0;
}
