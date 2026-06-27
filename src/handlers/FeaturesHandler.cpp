// FeaturesHandler.cpp — Auxiliary feature command handler.
//
// Protocol source: Features.command.cpp

#include "handlers/FeaturesHandler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

// ---------------------------------------------------------------------------
// Feature purpose constants (mirror Constants.h)
// ---------------------------------------------------------------------------
static constexpr int FP_OFF               = -1;
static constexpr int FP_SWITCH            =  1;
static constexpr int FP_ANALOG_OUTPUT     =  2;
static constexpr int FP_DEW_HEATER        =  3;
static constexpr int FP_INTERVALOMETER    =  4;
static constexpr int FP_MOMENTARY_SWITCH  =  5;
static constexpr int FP_HIDDEN_SWITCH     =  6;
static constexpr int FP_COVER_SWITCH      =  7;

// ---------------------------------------------------------------------------
// handle()
// ---------------------------------------------------------------------------

bool FeaturesHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)suppressFrame;

    // -----------------------------------------------------------------------
    // GX — get extended value
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G' && cmd[1] == 'X') {
        *numericReply = false;  // Phase 12: all GXY*/GXX* paths return '#'-terminated text

        // :GXY0# — active features mask (8 chars, always respond even with no features)
        if (param[0] == 'Y' && param[1] == '0' && param[2] == '\0') {
            for (int i = 0; i < 8; ++i) {
                int p = m_cfg->featurePurpose[i];
                reply[i] = (p != FP_OFF) ? '1' : '0';
            }
            reply[8] = '\0';
            return true;
        }

        // :GXY[n]# — feature info for slot n (1-8)
        if (param[0] == 'Y' && param[1] >= '1' && param[1] <= '8' && param[2] == '\0') {
            int idx = param[1] - '1';   // 0-based
            int purpose = m_cfg->featurePurpose[idx];
            if (!isVisible(purpose)) {
                *error = CE_0;
                return true;
            }
            int rp = reportedPurpose(purpose);
            std::snprintf(reply, 256, "%s,%d",
                          m_cfg->featureName[idx], rp);
            return true;
        }

        // :GXX[n]# — feature value/state for slot n (1-8)
        if (param[0] == 'X' && param[1] >= '1' && param[1] <= '8' && param[2] == '\0') {
            int idx = param[1] - '1';
            int purpose = m_cfg->featurePurpose[idx];
            if (!isVisible(purpose)) {
                *error = CE_0;
                return true;
            }
            formatValue(idx, reply);
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // SX — set extended value
    // -----------------------------------------------------------------------
    if (cmd[0] == 'S' && cmd[1] == 'X') {

        // :SXX[n],[sub][v]# — set feature n sub-command
        // param format: "X[n],[sub][v]"
        if (param[0] == 'X' && param[1] >= '1' && param[1] <= '8' && param[2] == ',') {
            int idx = param[1] - '1';
            int purpose = m_cfg->featurePurpose[idx];

            // Always return '1' even for absent slots (matches firmware permissiveness),
            // but only claim the command if the 'X' prefix matches.
            *numericReply = true;

            if (!isVisible(purpose)) {
                reply[0] = '0';
                return true;
            }

            char sub = param[3];    // sub-command char: V, Z, S, E, D, C
            const char* valStr = &param[4];

            std::lock_guard<std::mutex> lk(m_state->mutex);
            FeatureState& f = m_state->feature[idx];

            switch (sub) {

            case 'V':  // Set value / enable
                switch (purpose) {
                case FP_SWITCH:
                case FP_MOMENTARY_SWITCH:
                case FP_COVER_SWITCH:
                    f.value = (std::atoi(valStr) != 0) ? 1 : 0;
                    break;
                case FP_ANALOG_OUTPUT: {
                    long v = std::atol(valStr);
                    f.value = (v < 0) ? 0 : (v > 255) ? 255 : v;
                    break;
                }
                case FP_DEW_HEATER:
                    f.dewEnabled = (std::atoi(valStr) != 0);
                    break;
                case FP_INTERVALOMETER:
                    f.intvEnabled = (std::atoi(valStr) != 0);
                    break;
                default:
                    break;
                }
                reply[0] = '1';
                break;

            case 'Z':  // Dew-heater zero offset
                if (purpose == FP_DEW_HEATER) {
                    float v = static_cast<float>(std::atof(valStr));
                    f.dewZero = v;
                    reply[0] = '1';
                } else {
                    reply[0] = '0';
                }
                break;

            case 'S':  // Dew-heater span
                if (purpose == FP_DEW_HEATER) {
                    float v = static_cast<float>(std::atof(valStr));
                    f.dewSpan = v;
                    reply[0] = '1';
                } else {
                    reply[0] = '0';
                }
                break;

            case 'E':  // Intervalometer exposure (seconds)
                if (purpose == FP_INTERVALOMETER) {
                    float v = static_cast<float>(std::atof(valStr));
                    if (v < 0.0f || v > 3600.0f) {
                        reply[0] = '0';
                    } else {
                        f.intvExposure = v;
                        reply[0] = '1';
                    }
                } else {
                    reply[0] = '0';
                }
                break;

            case 'D':  // Intervalometer delay (seconds)
                if (purpose == FP_INTERVALOMETER) {
                    float v = static_cast<float>(std::atof(valStr));
                    if (v < 1.0f || v > 3600.0f) {
                        reply[0] = '0';
                    } else {
                        f.intvDelay = v;
                        reply[0] = '1';
                    }
                } else {
                    reply[0] = '0';
                }
                break;

            case 'C':  // Intervalometer count (0=unlimited)
                if (purpose == FP_INTERVALOMETER) {
                    int v = std::atoi(valStr);
                    if (v < 0 || v > 255) {
                        reply[0] = '0';
                    } else {
                        f.intvCount = v;
                        reply[0] = '1';
                    }
                } else {
                    reply[0] = '0';
                }
                break;

            default:
                reply[0] = '0';
                break;
            }
            return true;
        }

        return false;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool FeaturesHandler::hasAnyFeature() const {
    for (int i = 0; i < 8; ++i) {
        if (m_cfg->featurePurpose[i] != FP_OFF) return true;
    }
    return false;
}

bool FeaturesHandler::isVisible(int purpose) {
    return purpose != FP_OFF && purpose != FP_HIDDEN_SWITCH;
}

int FeaturesHandler::reportedPurpose(int purpose) {
    // MOMENTARY_SWITCH and COVER_SWITCH both report as SWITCH (1) to the driver.
    if (purpose == FP_MOMENTARY_SWITCH || purpose == FP_COVER_SWITCH) return FP_SWITCH;
    return purpose;
}

void FeaturesHandler::formatValue(int slot, char* reply) const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    const FeatureState& f = m_state->feature[slot];
    int purpose = m_cfg->featurePurpose[slot];

    switch (purpose) {
    case FP_SWITCH:
    case FP_MOMENTARY_SWITCH:
    case FP_COVER_SWITCH:
        std::snprintf(reply, 256, "%ld", f.value);
        break;

    case FP_ANALOG_OUTPUT:
        std::snprintf(reply, 256, "%ld", f.value);
        break;

    case FP_DEW_HEATER: {
        char deltaBuf[16];
        if (std::isnan(f.dewDeltaT)) {
            std::snprintf(deltaBuf, sizeof(deltaBuf), "NAN");
        } else {
            std::snprintf(deltaBuf, sizeof(deltaBuf), "%.1f",
                          static_cast<double>(f.dewDeltaT));
        }
        std::snprintf(reply, 256, "%d,%.1f,%.1f,%s",
                      f.dewEnabled ? 1 : 0,
                      static_cast<double>(f.dewZero),
                      static_cast<double>(f.dewSpan),
                      deltaBuf);
        break;
    }

    case FP_INTERVALOMETER: {
        char expBuf[16];
        formatExposure(f.intvExposure, expBuf, sizeof(expBuf));
        std::snprintf(reply, 256, "%.0f,%s,%.0f,%d",
                      static_cast<double>(f.intvCurrent),
                      expBuf,
                      static_cast<double>(f.intvDelay),
                      f.intvCount);
        break;
    }

    default:
        reply[0] = '\0';
        break;
    }
}

void FeaturesHandler::formatExposure(float secs, char* buf, int bufLen) {
    // Variable decimal places mirroring firmware:
    //   >= 60s  -> 0 dp
    //   >= 10s  -> 1 dp
    //   >= 1s   -> 2 dp
    //   <  1s   -> 3 dp
    if (secs >= 60.0f) {
        std::snprintf(buf, bufLen, "%.0f", static_cast<double>(secs));
    } else if (secs >= 10.0f) {
        std::snprintf(buf, bufLen, "%.1f", static_cast<double>(secs));
    } else if (secs >= 1.0f) {
        std::snprintf(buf, bufLen, "%.2f", static_cast<double>(secs));
    } else {
        std::snprintf(buf, bufLen, "%.3f", static_cast<double>(secs));
    }
}
