// WeatherHandler.cpp — Weather sensor command handler.
//
// Protocol source: Weather section of Telescope.command.cpp

#include "handlers/WeatherHandler.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>

bool WeatherHandler::handle(
    const char*   cmd,
    const char*   param,
    char*         reply,
    bool*         suppressFrame,
    bool*         numericReply,
    CommandError* error)
{
    (void)suppressFrame;
    (void)error;

    // -----------------------------------------------------------------------
    // GX — get weather values
    // -----------------------------------------------------------------------
    if (cmd[0] == 'G' && cmd[1] == 'X') {
        *numericReply = false;  // Phase 12: all GX paths return '#'-terminated text

        // :GX9F# — MCU temperature; always present (hasMcuTemp always true in sim)
        if (param[0] == '9' && param[1] == 'F' && param[2] == '\0') {
            float t;
            { std::lock_guard<std::mutex> lk(m_state->mutex); t = m_state->weather.mcuTemp; }
            // Integer format: "25" not "25.0"
            std::snprintf(reply, 256, "%d", static_cast<int>(t));
            return true;
        }

        // All remaining GX9* weather reads require hasWeather
        if (!m_cfg->hasWeather) return false;

        // :GX9A# — temperature (°C); signed, non-zero for probe success
        if (param[0] == '9' && param[1] == 'A' && param[2] == '\0') {
            float t;
            { std::lock_guard<std::mutex> lk(m_state->mutex); t = m_state->weather.temperature; }
            std::snprintf(reply, 256, "%+.1f", static_cast<double>(t));
            return true;
        }

        // :GX9B# — pressure (mb)
        if (param[0] == '9' && param[1] == 'B' && param[2] == '\0') {
            float p;
            { std::lock_guard<std::mutex> lk(m_state->mutex); p = m_state->weather.pressure; }
            std::snprintf(reply, 256, "%.1f", static_cast<double>(p));
            return true;
        }

        // :GX9C# — humidity (%)
        if (param[0] == '9' && param[1] == 'C' && param[2] == '\0') {
            float h;
            { std::lock_guard<std::mutex> lk(m_state->mutex); h = m_state->weather.humidity; }
            std::snprintf(reply, 256, "%.1f", static_cast<double>(h));
            return true;
        }

        // :GX9E# — dew point (°C)
        if (param[0] == '9' && param[1] == 'E' && param[2] == '\0') {
            float d;
            { std::lock_guard<std::mutex> lk(m_state->mutex); d = m_state->weather.dewPoint; }
            std::snprintf(reply, 256, "%.1f", static_cast<double>(d));
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // SX — set weather values (single char reply, no '#')
    // -----------------------------------------------------------------------
    if (cmd[0] == 'S' && cmd[1] == 'X') {
        if (!m_cfg->hasWeather) return false;

        *numericReply = true;

        // :SX9A,[v]# — set temperature
        if (param[0] == '9' && param[1] == 'A' && param[2] == ',') {
            float v = static_cast<float>(std::atof(&param[3]));
            { std::lock_guard<std::mutex> lk(m_state->mutex); m_state->weather.temperature = v; }
            reply[0] = '1';
            return true;
        }

        // :SX9B,[v]# — set pressure
        if (param[0] == '9' && param[1] == 'B' && param[2] == ',') {
            float v = static_cast<float>(std::atof(&param[3]));
            { std::lock_guard<std::mutex> lk(m_state->mutex); m_state->weather.pressure = v; }
            reply[0] = '1';
            return true;
        }

        // :SX9C,[v]# — set humidity
        if (param[0] == '9' && param[1] == 'C' && param[2] == ',') {
            float v = static_cast<float>(std::atof(&param[3]));
            { std::lock_guard<std::mutex> lk(m_state->mutex); m_state->weather.humidity = v; }
            reply[0] = '1';
            return true;
        }

        return false;
    }

    return false;
}
