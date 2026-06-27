// FocuserHandler.cpp — Focuser command handler.
//
// Protocol source: Focuser.command.cpp

#include "handlers/FocuserHandler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

// ---------------------------------------------------------------------------
// Working slew rate µm/s returned by :FW# for each gotoRate preset.
// The firmware computes this from axis speed parameters; we use fixed
// plausible values so the INDI driver's rate display is non-zero.
//
// gotoRate:    1    2    3     4      5
// µm/s:       10   50  200   800   3200
// ---------------------------------------------------------------------------
static constexpr double FOCUSER_RATE_UM_PER_SEC[6] = { 0.0, 10.0, 50.0, 200.0, 800.0, 3200.0 };

// ---------------------------------------------------------------------------
// handle()
// ---------------------------------------------------------------------------

bool FocuserHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    // Guard: no focusers in this config
    if (m_cfg->numFocusers == 0) return false;

    // Phase 12: default to non-numeric (text/'#'-terminated or suppress-frame)
    // reply. Paths that produce a numeric '0'/'1' response explicitly set
    // *numericReply = true (or call replyNumericOk/Fail) below.
    *numericReply = false;

    // -----------------------------------------------------------------------
    // :FA# / :FA[n]#  — focuser selection
    // -----------------------------------------------------------------------
    if (cmd[0] == 'F' && cmd[1] == 'A') {
        if (param[0] == '\0') {
            // :FA# — get active focuser (1-based), no '#'
            *suppressFrame = true;
            reply[0] = static_cast<char>('0' + m_state->activeFocuser + 1);
            reply[1] = '\0';
            return true;
        }
        // :FA[n]# — select focuser n; single char reply, no '#'
        if (param[0] >= '1' && param[0] <= '6' && param[1] == '\0') {
            int n = param[0] - '0';
            *numericReply = true;
            if (n > m_cfg->numFocusers) {
                reply[0] = '0';
            } else {
                std::lock_guard<std::mutex> lk(m_state->mutex);
                m_state->activeFocuser = n - 1;
                reply[0] = '1';
            }
            return true;
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // :F[cmd]# or :Fn[cmd]# where n is 1-6
    // -----------------------------------------------------------------------
    if (cmd[0] != 'F') return false;

    // Determine if cmd[1] is a focuser-select digit (1-6).
    //
    // Disambiguation rule: a digit in cmd[1] is a slot prefix ONLY when
    // param is non-empty (e.g. :F2G# -> cmd="F2", param="G").
    // When param is empty (e.g. :F2# -> cmd="F2", param=""), cmd[1] is
    // the sub-command character and the active focuser is used.
    // This correctly handles :F[1-4]# (set move rate) and :F[5-9]# (set
    // goto rate) which are bare digit commands with no slot prefix.
    char subCmd;
    int  slot;
    bool haveDigit = (cmd[1] >= '1' && cmd[1] <= '6') && (param[0] != '\0');

    if (haveDigit) {
        // Slot-prefixed command: cmd[1] selects focuser, param[0] is sub-cmd.
        slot   = resolveSlot(cmd[1]);
        subCmd = param[0];
        param  = param + 1;
    } else {
        slot   = resolveSlot('\0');   // use active focuser
        subCmd = cmd[1];
        // param stays as-is
    }

    if (slot < 0) return false;  // focuser not present

    // -----------------------------------------------------------------------
    // :Fa# — primary focuser present?
    // -----------------------------------------------------------------------
    if (subCmd == 'a' && param[0] == '\0') {
        *numericReply = true;
        reply[0] = '1';
        return true;
    }

    // -----------------------------------------------------------------------
    // :FT# — status
    // -----------------------------------------------------------------------
    if (subCmd == 'T' && param[0] == '\0') {
        cmdStatus(slot, reply);
        return true;
    }

    // -----------------------------------------------------------------------
    // :Fp# — mode (DC=CE_0, absolute=CE_1)
    // -----------------------------------------------------------------------
    if (subCmd == 'p' && param[0] == '\0') {
        bool isDC;
        { std::lock_guard<std::mutex> lk(m_state->mutex); isDC = m_state->focuser[slot].isDC; }
        *error = isDC ? CE_0 : CE_1;
        return true;
    }

    // -----------------------------------------------------------------------
    // Position get commands
    // -----------------------------------------------------------------------
    if (subCmd == 'G' && param[0] == '\0') { cmdGetPosMicrons(slot, reply); return true; }
    if (subCmd == 'g' && param[0] == '\0') { cmdGetPosSteps  (slot, reply); return true; }
    if (subCmd == 'I' && param[0] == '\0') { cmdGetMinMicrons(slot, reply); return true; }
    if (subCmd == 'i' && param[0] == '\0') { cmdGetMinSteps  (slot, reply); return true; }
    if (subCmd == 'M' && param[0] == '\0') { cmdGetMaxMicrons(slot, reply); return true; }
    if (subCmd == 'm' && param[0] == '\0') { cmdGetMaxSteps  (slot, reply); return true; }

    // -----------------------------------------------------------------------
    // :FW# — working slew rate µm/s
    // -----------------------------------------------------------------------
    if (subCmd == 'W' && param[0] == '\0') {
        cmdGetWorkingRate(slot, reply);
        return true;
    }

    // -----------------------------------------------------------------------
    // :FQ# — stop
    // -----------------------------------------------------------------------
    if (subCmd == 'Q' && param[0] == '\0') {
        cmdStop(slot);
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :F[1-4]# — set move rate; :F[5-9]# — set goto rate
    // -----------------------------------------------------------------------
    if (subCmd >= '1' && subCmd <= '4' && param[0] == '\0') {
        cmdSetMoveRate(slot, subCmd - '0');
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }
    if (subCmd >= '5' && subCmd <= '9' && param[0] == '\0') {
        cmdSetGotoRate(slot, subCmd - '0');
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // :F+# / :F-# — continuous move
    // -----------------------------------------------------------------------
    if (subCmd == '+' && param[0] == '\0') {
        cmdMoveIn(slot);
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }
    if (subCmd == '-' && param[0] == '\0') {
        cmdMoveOut(slot);
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // Goto commands
    // -----------------------------------------------------------------------

    // :FR[sn]# — relative goto microns; :Fr[sn]# — relative goto steps
    if (subCmd == 'R') {
        long delta = std::atol(param);
        cmdRelGotoMicrons(slot, delta);
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }
    if (subCmd == 'r') {
        long delta = std::atol(param);
        cmdRelGotoSteps(slot, delta);
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // :FS[n]# — absolute goto microns -> '0'/'1'
    if (subCmd == 'S') {
        long targetUm = std::atol(param);
        bool ok = cmdAbsGotoMicrons(slot, targetUm);
        *numericReply = true;
        reply[0] = ok ? '1' : '0';
        return true;
    }
    // :Fs[n]# — absolute goto steps -> '0'/'1'
    if (subCmd == 's') {
        long targetStps = std::atol(param);
        bool ok = cmdAbsGotoSteps(slot, targetStps);
        *numericReply = true;
        reply[0] = ok ? '1' : '0';
        return true;
    }

    // -----------------------------------------------------------------------
    // Home / zero
    // -----------------------------------------------------------------------
    if (subCmd == 'Z' && param[0] == '\0') {
        cmdSetZero(slot);
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }
    if (subCmd == 'H' && param[0] == '\0') {
        cmdSetHome(slot);
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }
    if (subCmd == 'h' && param[0] == '\0') {
        cmdGotoHome(slot);
        *suppressFrame = true;
        reply[0] = '\0';
        return true;
    }

    // -----------------------------------------------------------------------
    // Backlash
    // -----------------------------------------------------------------------
    if (subCmd == 'B') {
        if (param[0] == '\0') {
            // :FB# — get backlash microns
            long bl;
            { std::lock_guard<std::mutex> lk(m_state->mutex); bl = m_state->focuser[slot].backlashSteps; }
            std::snprintf(reply, 256, "%ld", static_cast<long>(stepsToMicrons(slot, bl)));
            return true;
        }
        // :FB[n]# — set backlash microns
        long um = std::atol(param);
        *numericReply = true;
        reply[0] = cmdSetBacklashMicrons(slot, um) ? '1' : '0';
        return true;
    }
    if (subCmd == 'b') {
        if (param[0] == '\0') {
            // :Fb# — get backlash steps
            long bl;
            { std::lock_guard<std::mutex> lk(m_state->mutex); bl = m_state->focuser[slot].backlashSteps; }
            std::snprintf(reply, 256, "%ld", bl);
            return true;
        }
        // :Fb[n]# — set backlash steps
        long steps = std::atol(param);
        *numericReply = true;
        reply[0] = cmdSetBacklashSteps(slot, steps) ? '1' : '0';
        return true;
    }

    // -----------------------------------------------------------------------
    // Temperature / TCF
    // -----------------------------------------------------------------------
    if (subCmd == 'e' && param[0] == '\0') {
        // :Fe# — temp differential
        float temp;
        { std::lock_guard<std::mutex> lk(m_state->mutex); temp = m_state->focuser[slot].tcfT0; }
        // delta = current temperature - reference temperature
        float cur;
        { std::lock_guard<std::mutex> lk(m_state->mutex); cur = m_state->focuser[slot].temperature; }
        std::snprintf(reply, 256, "%+.1f", static_cast<double>(cur - temp));
        return true;
    }
    if (subCmd == 't' && param[0] == '\0') {
        // :Ft# — temperature
        float temp;
        { std::lock_guard<std::mutex> lk(m_state->mutex); temp = m_state->focuser[slot].temperature; }
        std::snprintf(reply, 256, "%.1f", static_cast<double>(temp));
        return true;
    }
    if (subCmd == 'u' && param[0] == '\0') {
        // :Fu# — microns per step
        float spm;
        { std::lock_guard<std::mutex> lk(m_state->mutex); spm = m_state->focuser[slot].stepsPerMicron; }
        double umPerStep = (spm > 0.0f) ? (1.0 / spm) : 0.0;
        std::snprintf(reply, 256, "%.5f", umPerStep);
        return true;
    }
    if (subCmd == 'C') {
        if (param[0] == '\0') {
            // :FC# — get TCF coefficient
            float coef;
            { std::lock_guard<std::mutex> lk(m_state->mutex); coef = m_state->focuser[slot].tcfCoef; }
            std::snprintf(reply, 256, "%.5f", static_cast<double>(coef));
            return true;
        }
        // :FC[v]# — set TCF coefficient
        float coef = static_cast<float>(std::atof(param));
        *numericReply = true;
        reply[0] = cmdSetTcfCoef(slot, coef) ? '1' : '0';
        return true;
    }
    if (subCmd == 'c') {
        if (param[0] == '\0') {
            // :Fc# — TCF enabled? -> CE_0 / CE_1
            bool en;
            { std::lock_guard<std::mutex> lk(m_state->mutex); en = m_state->focuser[slot].tcfEnabled; }
            *error = en ? CE_1 : CE_0;
            return true;
        }
        // :Fc[n]# — set TCF enable
        int en = std::atoi(param);
        *numericReply = true;
        reply[0] = cmdSetTcfEnable(slot, en) ? '1' : '0';
        return true;
    }
    if (subCmd == 'D') {
        if (param[0] == '\0') {
            // :FD# — get TCF deadband (microns)
            long db;
            { std::lock_guard<std::mutex> lk(m_state->mutex); db = m_state->focuser[slot].tcfDeadband; }
            std::snprintf(reply, 256, "%ld", db);
            return true;
        }
        // :FD[n]# — set TCF deadband (microns)
        long db = std::atol(param);
        *numericReply = true;
        reply[0] = cmdSetTcfDeadband(slot, db) ? '1' : '0';
        return true;
    }
    if (subCmd == 'd') {
        if (param[0] == '\0') {
            // :Fd# — get TCF deadband (steps variant — same value)
            long db;
            { std::lock_guard<std::mutex> lk(m_state->mutex); db = m_state->focuser[slot].tcfDeadband; }
            std::snprintf(reply, 256, "%ld", db);
            return true;
        }
        // :Fd[n]# — set TCF deadband (steps)
        long steps = std::atol(param);
        *numericReply = true;
        reply[0] = cmdSetTcfDeadbandSteps(slot, steps) ? '1' : '0';
        return true;
    }

    // -----------------------------------------------------------------------
    // DC power
    // -----------------------------------------------------------------------
    if (subCmd == 'P') {
        if (param[0] == '\0') {
            // :FP# — get DC power
            int pwr;
            { std::lock_guard<std::mutex> lk(m_state->mutex); pwr = m_state->focuser[slot].dcPower; }
            std::snprintf(reply, 256, "%d", pwr);
            return true;
        }
        // :FP[n]# — set DC power
        int pct = std::atoi(param);
        *numericReply = true;
        reply[0] = cmdSetDcPower(slot, pct) ? '1' : '0';
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Slot resolution
// ---------------------------------------------------------------------------

int FocuserHandler::resolveSlot(char digitOrCmd) const {
    int slot;
    if (digitOrCmd >= '1' && digitOrCmd <= '6') {
        slot = digitOrCmd - '1';
    } else {
        slot = m_state->activeFocuser;
    }
    if (slot < 0 || slot >= m_cfg->numFocusers) return -1;
    return slot;
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void FocuserHandler::cmdStatus(int slot, char* reply) const {
    bool moving;
    int  rate;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        moving = m_state->focuser[slot].isMoving;
        rate   = m_state->focuser[slot].moveRate;
    }
    if (rate < 1) rate = 1;
    if (rate > 4) rate = 4;
    std::snprintf(reply, 256, "%c%d", moving ? 'M' : 'S', rate);
}

// ---------------------------------------------------------------------------
// Position get helpers
// ---------------------------------------------------------------------------

void FocuserHandler::cmdGetPosMicrons(int slot, char* reply) const {
    long steps;
    { std::lock_guard<std::mutex> lk(m_state->mutex); steps = m_state->focuser[slot].positionSteps; }
    long um = static_cast<long>(stepsToMicrons(slot, steps));
    std::snprintf(reply, 256, "%ld", um);
}

void FocuserHandler::cmdGetPosSteps(int slot, char* reply) const {
    long steps;
    { std::lock_guard<std::mutex> lk(m_state->mutex); steps = m_state->focuser[slot].positionSteps; }
    std::snprintf(reply, 256, "%ld", steps);
}

void FocuserHandler::cmdGetMinMicrons(int slot, char* reply) const {
    long steps;
    { std::lock_guard<std::mutex> lk(m_state->mutex); steps = m_state->focuser[slot].limitMinSteps; }
    std::snprintf(reply, 256, "%ld", static_cast<long>(stepsToMicrons(slot, steps)));
}

void FocuserHandler::cmdGetMinSteps(int slot, char* reply) const {
    long steps;
    { std::lock_guard<std::mutex> lk(m_state->mutex); steps = m_state->focuser[slot].limitMinSteps; }
    std::snprintf(reply, 256, "%ld", steps);
}

void FocuserHandler::cmdGetMaxMicrons(int slot, char* reply) const {
    long steps;
    { std::lock_guard<std::mutex> lk(m_state->mutex); steps = m_state->focuser[slot].limitMaxSteps; }
    std::snprintf(reply, 256, "%ld", static_cast<long>(stepsToMicrons(slot, steps)));
}

void FocuserHandler::cmdGetMaxSteps(int slot, char* reply) const {
    long steps;
    { std::lock_guard<std::mutex> lk(m_state->mutex); steps = m_state->focuser[slot].limitMaxSteps; }
    std::snprintf(reply, 256, "%ld", steps);
}

// ---------------------------------------------------------------------------
// Working rate
// ---------------------------------------------------------------------------

void FocuserHandler::cmdGetWorkingRate(int slot, char* reply) const {
    int rate;
    { std::lock_guard<std::mutex> lk(m_state->mutex); rate = m_state->focuser[slot].gotoRate; }
    if (rate < 1) rate = 1;
    if (rate > 5) rate = 5;
    std::snprintf(reply, 256, "%.0f", FOCUSER_RATE_UM_PER_SEC[rate]);
}

// ---------------------------------------------------------------------------
// Motion commands
// ---------------------------------------------------------------------------

void FocuserHandler::cmdMoveIn(int slot) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    FocuserState& f = m_state->focuser[slot];
    f.targetSteps = f.limitMaxSteps;
    f.isMoving    = true;
}

void FocuserHandler::cmdMoveOut(int slot) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    FocuserState& f = m_state->focuser[slot];
    f.targetSteps = f.limitMinSteps;
    f.isMoving    = true;
}

void FocuserHandler::cmdStop(int slot) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    FocuserState& f = m_state->focuser[slot];
    f.targetSteps = f.positionSteps;  // cancel move by making target == current
    f.isMoving    = false;
}

void FocuserHandler::cmdSetMoveRate(int slot, int rate) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].moveRate = rate;
}

void FocuserHandler::cmdSetGotoRate(int slot, int rate) {
    // gotoRate 5-9 maps to internal 1-5 (subtract 4)
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].gotoRate = rate - 4;
}

void FocuserHandler::cmdRelGotoMicrons(int slot, long deltaUm) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    FocuserState& f = m_state->focuser[slot];
    long deltaSteps = micronsToSteps(slot, static_cast<double>(deltaUm));
    f.targetSteps = f.positionSteps + deltaSteps;
    // Clamp to limits
    if (f.targetSteps < f.limitMinSteps) f.targetSteps = f.limitMinSteps;
    if (f.limitMaxSteps > 0 && f.targetSteps > f.limitMaxSteps) f.targetSteps = f.limitMaxSteps;
    f.isMoving = (f.targetSteps != f.positionSteps);
}

void FocuserHandler::cmdRelGotoSteps(int slot, long deltaSteps) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    FocuserState& f = m_state->focuser[slot];
    f.targetSteps = f.positionSteps + deltaSteps;
    if (f.targetSteps < f.limitMinSteps) f.targetSteps = f.limitMinSteps;
    if (f.limitMaxSteps > 0 && f.targetSteps > f.limitMaxSteps) f.targetSteps = f.limitMaxSteps;
    f.isMoving = (f.targetSteps != f.positionSteps);
}

bool FocuserHandler::cmdAbsGotoMicrons(int slot, long targetUm) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    FocuserState& f = m_state->focuser[slot];
    long targetStps = micronsToSteps(slot, static_cast<double>(targetUm));
    if (targetStps < f.limitMinSteps) return false;
    if (f.limitMaxSteps > 0 && targetStps > f.limitMaxSteps) return false;
    f.targetSteps = targetStps;
    f.isMoving    = (f.targetSteps != f.positionSteps);
    return true;
}

bool FocuserHandler::cmdAbsGotoSteps(int slot, long targetSteps) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    FocuserState& f = m_state->focuser[slot];
    if (targetSteps < f.limitMinSteps) return false;
    if (f.limitMaxSteps > 0 && targetSteps > f.limitMaxSteps) return false;
    f.targetSteps = targetSteps;
    f.isMoving    = (f.targetSteps != f.positionSteps);
    return true;
}

void FocuserHandler::cmdSetZero(int slot) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    FocuserState& f = m_state->focuser[slot];
    f.positionSteps = 0;
    f.targetSteps   = 0;
    f.isMoving      = false;
}

void FocuserHandler::cmdSetHome(int slot) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].homePositionSteps = m_state->focuser[slot].positionSteps;
}

void FocuserHandler::cmdGotoHome(int slot) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    FocuserState& f = m_state->focuser[slot];
    f.targetSteps = f.homePositionSteps;
    f.isMoving    = (f.targetSteps != f.positionSteps);
}

// ---------------------------------------------------------------------------
// Backlash
// ---------------------------------------------------------------------------

bool FocuserHandler::cmdSetBacklashMicrons(int slot, long um) {
    if (um < 0) return false;
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].backlashSteps = micronsToSteps(slot, static_cast<double>(um));
    return true;
}

bool FocuserHandler::cmdSetBacklashSteps(int slot, long steps) {
    if (steps < 0) return false;
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].backlashSteps = steps;
    return true;
}

// ---------------------------------------------------------------------------
// TCF
// ---------------------------------------------------------------------------

bool FocuserHandler::cmdSetTcfCoef(int slot, float coef) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].tcfCoef = coef;
    return true;
}

bool FocuserHandler::cmdSetTcfEnable(int slot, int enable) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].tcfEnabled = (enable != 0);
    return true;
}

bool FocuserHandler::cmdSetTcfDeadband(int slot, long deadband) {
    if (deadband < 0) return false;
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].tcfDeadband = deadband;
    return true;
}

bool FocuserHandler::cmdSetTcfDeadbandSteps(int slot, long steps) {
    if (steps < 0) return false;
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].tcfDeadband = steps;
    return true;
}

// ---------------------------------------------------------------------------
// DC power
// ---------------------------------------------------------------------------

bool FocuserHandler::cmdSetDcPower(int slot, int pct) {
    if (pct < 0 || pct > 100) return false;
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->focuser[slot].dcPower = pct;
    return true;
}

// ---------------------------------------------------------------------------
// Unit conversions
// ---------------------------------------------------------------------------

long FocuserHandler::micronsToSteps(int slot, double um) const {
    float spm = m_state->focuser[slot].stepsPerMicron;
    return static_cast<long>(um * spm + 0.5);
}

double FocuserHandler::stepsToMicrons(int slot, long steps) const {
    float spm = m_state->focuser[slot].stepsPerMicron;
    if (spm <= 0.0f) return 0.0;
    return static_cast<double>(steps) / spm;
}
