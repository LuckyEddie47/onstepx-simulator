#pragma once
// WeatherHandler.h — Handles weather sensor read/write commands.
//
// Protocol source: Weather.command.cpp (GX9A-F, SX9A-C sections of Telescope.command.cpp)
//
// Commands handled (all gated on cfg.hasWeather except :GX9F# which is MCU temp):
//
//   :GX9A#   Temperature (°C)    -> "+15.0"# (signed, non-zero for probe)
//   :GX9B#   Pressure (mb)       -> "1013.0"#
//   :GX9C#   Humidity (%)        -> "60.0"#
//   :GX9E#   Dew point (°C)      -> "7.0"#
//   :GX9F#   MCU temperature (°C)-> "25"# (integer; always present, no hasWeather gate)
//
//   :SX9A,[v]#  Set temperature  -> '1' (single char, no '#')
//   :SX9B,[v]#  Set pressure     -> '1'
//   :SX9C,[v]#  Set humidity     -> '1'
//
// Critical:
//   - :GX9A# must return a non-zero float (e.g. "+15.0") — probeController() checks this.
//   - :SX9A,15.0# is sent via sendCommandSingleChar to test writeability; must return '1'.
//   - :GX9F# is the MCU temperature and is always present (hasMcuTemp=true in sim).

#include "HandlerBase.h"

class WeatherHandler : public HandlerBase {
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
