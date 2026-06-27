// FirmwareHandler.cpp — Firmware identity command handler

#include "FirmwareHandler.h"

#include <cstring>
#include <cstdio>

bool FirmwareHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)suppressFrame;
    *numericReply = false;   // Phase 12: all GV replies are '#'-terminated text
    (void)error;

    // Only handles "GV" commands
    if (cmd[0] != 'G' || cmd[1] != 'V') return false;
    if (!param) return false;

    // All GV replies are plain '#'-terminated strings.
    // numericReply stays false, suppressFrame stays false.

    if (param[0] == 'P' && param[1] == '\0') {
        // :GVP# — product name; driver does exact strcmp("On-Step")
        setReply(reply, m_cfg->firmwareName);
        return true;
    }

    if (param[0] == 'N' && param[1] == '\0') {
        // :GVN# — version string; driver checks major version >= 10
        setReply(reply, m_cfg->firmwareVersion);
        return true;
    }

    if (param[0] == 'D' && param[1] == '\0') {
        // :GVD# — firmware build date
        setReply(reply, m_cfg->firmwareDate);
        return true;
    }

    if (param[0] == 'T' && param[1] == '\0') {
        // :GVT# — firmware build time
        setReply(reply, m_cfg->firmwareTime);
        return true;
    }

    if (param[0] == 'C' && param[1] == '\0') {
        // :GVC# — config (controller) name from HOST_NAME
        setReply(reply, m_cfg->configName);
        return true;
    }

    if (param[0] == 'H' && param[1] == '\0') {
        // :GVH# — hardware description
        setReply(reply, m_cfg->firmwareHardware);
        return true;
    }

    if (param[0] == 'M' && param[1] == '\0') {
        // :GVM# — general message combining name and version
        std::snprintf(reply, 255, "%s %s",
                      m_cfg->firmwareName,
                      m_cfg->firmwareVersion);
        return true;
    }

    // Unknown GV sub-command — not handled by this handler
    return false;
}
