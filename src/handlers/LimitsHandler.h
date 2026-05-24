#pragma once
// LimitsHandler.h — Handles horizon and meridian limit commands.
// Source reference: Limits.command.cpp

#include "HandlerBase.h"

class LimitsHandler : public HandlerBase {
public:
    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error
    ) override;
};
