// StatusHandler.cpp — Status command handler
// Implements :GU#, :Gu#, :GW#, :Gm#, :SX97,n#
// Bit/char packing matches Status.command.cpp exactly.

#include "StatusHandler.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// handle() — dispatcher
// ---------------------------------------------------------------------------

bool StatusHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)error;

    // Phase 12: default to non-numeric reply; SX97 numeric path sets *numericReply=true.
    *numericReply = false;

    // -----------------------------------------------------------------------
    // :GU# — ASCII status string
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G' && cmd[1] == 'U' && param[0] == '\0') {
        buildGuString(reply);
        return true;
    }

    // -----------------------------------------------------------------------
    // :Gu# — 9-byte binary status (no '#', raw bytes)
    // All bytes have bit7 set (>= 0x80), so none are NUL. We NUL-terminate
    // at reply[9] and set suppressFrame=true. The dispatcher uses strlen()
    // which correctly returns 9 since no byte in [0..8] is 0x00.
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G' && cmd[1] == 'u' && param[0] == '\0') {
        uint8_t buf[9];
        buildGuBinary(buf);
        std::memcpy(reply, buf, 9);
        reply[9] = '\0';
        *suppressFrame = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // :GW# — brief mount/tracking/park status (3 chars + '#')
    // Format: [mount-type][tracking][park-or-home-or-align]
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G' && cmd[1] == 'W' && param[0] == '\0') {
        char mt = 'G';
        if (m_cfg->hasMount) {
            switch (m_cfg->mountType) {
            case MOUNT_GEM:
            case MOUNT_GEM_TA:
            case MOUNT_GEM_TAC:    mt = 'G'; break;
            case MOUNT_FORK:
            case MOUNT_FORK_TA:
            case MOUNT_FORK_TAC:   mt = 'P'; break;
            case MOUNT_ALTAZM:
            case MOUNT_ALTAZM_UNL: mt = 'A'; break;
            case MOUNT_ALTALT:     mt = 'L'; break;
            default:               mt = 'G'; break;
            }
        }

        char tracking = m_state->isTracking ? 'T' : 'N';

        char status;
        if (m_state->parkState == PS_PARKED) {
            status = 'P';
        } else if (m_state->isAtHome) {
            status = 'H';
        } else if (m_state->alignDone) {
            status = '1';
        } else {
            status = '0';
        }

        reply[0] = mt;
        reply[1] = tracking;
        reply[2] = status;
        reply[3] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :Gm# — pier side
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G' && cmd[1] == 'm' && param[0] == '\0') {
        switch (m_state->pierSide) {
        case PIER_SIDE_EAST: reply[0] = 'E'; break;
        case PIER_SIDE_WEST: reply[0] = 'W'; break;
        default:             reply[0] = 'N'; break;
        }
        reply[1] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :SX97,n# — buzzer / sound control
    // n=0 off, n=1 on, n=2 beep, n=3 alert, n=4 click
    // Single-char reply '1' success, '0' invalid.
    // -----------------------------------------------------------------------
    if (cmd[0] == 'S' && cmd[1] == 'X' &&
        param[0] == '9' && param[1] == '7' && param[2] == ',') {

        *numericReply = true;

        char nChar = param[3];
        if (nChar < '0' || nChar > '4') {
            reply[0] = '0';
            return true;
        }

        int n = nChar - '0';
        switch (n) {
        case 0: m_state->soundEnabled = false; break;
        case 1: m_state->soundEnabled = true;  break;
        case 2: /* beep  — sim no-op */ break;
        case 3: /* alert — sim no-op */ break;
        case 4: /* click — sim no-op */ break;
        }
        reply[0] = '1';
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// buildGuString — :GU# ASCII status string
// Characters appended in the exact order defined by Status.command.cpp.
// ---------------------------------------------------------------------------

void StatusHandler::buildGuString(char* reply) const {
    int pos = 0;

    auto append = [&](char c) {
        if (pos < 254) reply[pos++] = c;
    };

    // Tracking
    if (!m_state->isTracking) append('n');

    // Goto state
    if (m_state->gotoState == GotoState::NONE) append('N');

    // Park state
    switch (m_state->parkState) {
    case PS_UNPARKED:    append('p'); break;
    case PS_PARKING:     append('I'); break;
    case PS_PARKED:      append('P'); break;
    case PS_PARK_FAILED: append('F'); break;
    case PS_UNPARKING:   append('p'); break;
    }

    // Sync to encoders
    if (m_state->syncToEncoders) append('e');

    // Home / homing
    if (m_state->isAtHome)                       append('H');
    if (m_state->homeState == HomeState::HOMING)  append('h');
    if (m_state->autoHomeAtBoot)                  append('B');

    // PPS sync (only if config has PPS)
    if (m_cfg->hasPPS && m_state->ppsSynced) append('S');

    // Guide states
    if (m_state->pulseGuide != GuideState::NONE) append('G');
    if (m_state->guideState != GuideState::NONE) append('g');

    // Rate compensation
    switch (m_state->rateComp) {
    case RateComp::REFRACTION:      append('r'); append('s'); break;
    case RateComp::REFRACTION_DUAL: append('r'); break;
    case RateComp::MODEL:           append('t'); append('s'); break;
    case RateComp::MODEL_DUAL:      append('t'); break;
    default: break;
    }

    // Tracking rate
    float hz = m_state->trackingRateHz;
    if      (hz > 57.8f && hz < 58.0f) append('(');  // lunar  57.900
    else if (hz > 59.9f && hz < 60.1f) append('O');  // solar  60.000
    else if (hz > 60.0f && hz < 60.2f) append('k');  // king   60.136

    // Pause / home flags
    if (m_state->homePaused)          append('w');
    if (m_state->pauseAtHomeEnabled)  append('u');

    // Sound
    if (m_state->soundEnabled) append('z');

    // Auto meridian flip
    if (m_state->autoFlipEnabled) append('a');

    // PEC (only if config has PEC and equatorial mount)
    if (m_cfg->hasPec) {
        if (m_state->pecRecorded) append('R');
        if (m_cfg->isEquatorial()) {
            switch (m_state->pecState) {
            case PecState::NONE:         append('/'); break;
            case PecState::READY_PLAY:   append('~'); break;
            case PecState::PLAYING:      append('^'); break;
            case PecState::READY_RECORD: append(';'); break;
            case PecState::RECORDING:    append('~'); break;
            }
        }
    }

    // Mount type
    if (m_cfg->hasMount) {
        switch (m_cfg->mountType) {
        case MOUNT_GEM:
        case MOUNT_GEM_TA:
        case MOUNT_GEM_TAC:    append('E'); break;
        case MOUNT_FORK:
        case MOUNT_FORK_TA:
        case MOUNT_FORK_TAC:   append('K'); break;
        case MOUNT_ALTAZM:
        case MOUNT_ALTAZM_UNL: append('A'); break;
        case MOUNT_ALTALT:     append('L'); break;
        default:               append('E'); break;
        }
    }

    // Pier side
    switch (m_state->pierSide) {
    case PIER_SIDE_NONE: append('o'); break;
    case PIER_SIDE_EAST: append('T'); break;
    case PIER_SIDE_WEST: append('W'); break;
    }

    // Pulse guide rate (always present)
    append(static_cast<char>('0' + (m_state->pulseRateSelect & 0x0F)));

    // Guide rate (always present)
    append(static_cast<char>('0' + (m_state->guideRateSelect & 0x0F)));

    // Error code (always last)
    append(static_cast<char>('0' + (m_state->errorCode & 0x0F)));

    reply[pos] = '\0';
}

// ---------------------------------------------------------------------------
// buildGuBinary — :Gu# 9-byte binary status
// All bytes have bit7 set (>= 0x80). Bit assignments from Status.command.cpp.
// ---------------------------------------------------------------------------

int StatusHandler::buildGuBinary(uint8_t* buf) const {
    // byte[0]: tracking/goto/pps/pulse-guide/rate-comp
    {
        uint8_t b = 0x80;
        if (!m_state->isTracking)                    b |= 0x01;
        if (m_state->gotoState == GotoState::NONE)   b |= 0x02;
        if (m_cfg->hasPPS && m_state->ppsSynced)     b |= 0x04;
        if (m_state->pulseGuide != GuideState::NONE) b |= 0x08;
        switch (m_state->rateComp) {
        case RateComp::REFRACTION:      b |= 0x30; break;
        case RateComp::REFRACTION_DUAL: b |= 0x10; break;
        case RateComp::MODEL:           b |= 0x70; break;
        case RateComp::MODEL_DUAL:      b |= 0x50; break;
        default: break;
        }
        buf[0] = b;
    }

    // byte[1]: tracking rate / encoders / guide / startup
    // King rate = bits 0 AND 1 both set (source: Status.command.cpp)
    // Lunar = bit0 only, Solar = bit1 only, King = bits 0+1, Sidereal = 00
    {
        uint8_t b = 0x80;
        float hz    = m_state->trackingRateHz;
        bool  lunar = (hz > 57.8f && hz < 58.0f);
        bool  solar = (hz > 59.9f && hz < 60.1f);
        bool  king  = (hz > 60.1f && hz < 60.2f);
        if (lunar) b |= 0x01;
        if (solar) b |= 0x02;
        if (king)  b |= 0x03;  // both bits — DEC-005
        if (m_state->syncToEncoders)                 b |= 0x04;
        if (m_state->guideState != GuideState::NONE) b |= 0x08;
        if (m_state->startupTrusted)                 b |= 0x10;
        buf[1] = b;
    }

    // byte[2]: home / homing / pause / buzzer / autoflip
    {
        uint8_t b = 0x80;
        if (m_state->isAtHome)                        b |= 0x01;
        if (m_state->homePaused)                      b |= 0x02;
        if (m_state->pauseAtHomeEnabled)               b |= 0x04;
        if (m_state->soundEnabled)                    b |= 0x08;
        if (m_state->autoFlipEnabled)                 b |= 0x10;
        if (m_state->homeState == HomeState::HOMING)  b |= 0x20;
        if (m_state->autoHomeAtBoot)                  b |= 0x40;
        buf[2] = b;
    }

    // byte[3]: mount type bits / pier side
    {
        uint8_t b = 0x80;
        if (m_cfg->hasMount) {
            switch (m_cfg->mountType) {
            case MOUNT_GEM:
            case MOUNT_GEM_TA:
            case MOUNT_GEM_TAC:    b |= 0x01; break;
            case MOUNT_FORK:
            case MOUNT_FORK_TA:
            case MOUNT_FORK_TAC:   b |= 0x02; break;
            case MOUNT_ALTALT:     b |= 0x04; break;
            case MOUNT_ALTAZM:
            case MOUNT_ALTAZM_UNL: b |= 0x08; break;
            default: break;
            }
        }
        switch (m_state->pierSide) {
        case PIER_SIDE_EAST: b |= 0x20; break;
        case PIER_SIDE_WEST: b |= 0x30; break;
        default: break;
        }
        buf[3] = b;
    }

    // byte[4]: PEC state | 0x80, bit6 if pecRecorded
    {
        uint8_t b = 0x80;
        b |= static_cast<uint8_t>(m_state->pecState) & 0x0F;
        if (m_state->pecRecorded) b |= 0x40;
        buf[4] = b;
    }

    // byte[5]: park state | 0x80
    buf[5] = static_cast<uint8_t>(m_state->parkState) | 0x80;

    // byte[6]: pulse guide rate | 0x80
    buf[6] = static_cast<uint8_t>(m_state->pulseRateSelect & 0x0F) | 0x80;

    // byte[7]: guide rate | 0x80
    buf[7] = static_cast<uint8_t>(m_state->guideRateSelect & 0x0F) | 0x80;

    // byte[8]: error code | 0x80
    buf[8] = (m_state->errorCode & 0x0F) | 0x80;

    return 9;
}
