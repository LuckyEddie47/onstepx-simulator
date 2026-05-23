// ConfigParser.cpp — OnStepX Config.h runtime parser

#include "ConfigParser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SimConfig ConfigParser::parseFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("ConfigParser: cannot open '" + path + "'");
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parseString(ss.str());
}

SimConfig ConfigParser::parseString(const std::string& content) {
    SimConfig cfg;
    auto defines = extractDefines(content);
    applyDefines(defines, cfg);
    return cfg;
}

// ---------------------------------------------------------------------------
// Step 1: extract all #define SYMBOL VALUE pairs from raw content
// ---------------------------------------------------------------------------

std::vector<ConfigParser::Define> ConfigParser::extractDefines(const std::string& content) {
    std::vector<Define> result;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // Strip inline // comment
        line = stripComment(line);
        line = trim(line);

        if (line.empty()) continue;
        if (line.rfind("#define", 0) != 0) continue;  // must start with #define

        // Tokenise: #define SYMBOL VALUE
        std::istringstream ls(line);
        std::string directive, symbol, value;
        ls >> directive >> symbol;

        // remainder of line (after optional whitespace) is the value
        std::getline(ls, value);
        value = trim(value);

        if (symbol.empty()) continue;
        // value may be empty for flag-style defines — treat as ON
        if (value.empty()) value = "-2";  // ON sentinel

        result.push_back({symbol, value});
    }
    return result;
}

// ---------------------------------------------------------------------------
// Step 2: resolve a value string through token substitution
// Returns a numeric string (integer or float), or the original symbol if
// the value is an unresolvable named token.
// ---------------------------------------------------------------------------

std::string ConfigParser::substituteTokens(const std::string& value,
                                           const std::vector<Define>& defines) {
    // Direct numeric check first
    if (isNumeric(value)) return value;

    // Known tokens
    if (value == "OFF")  return "-1";
    if (value == "ON")   return "-2";
    if (value == "HIGH") return "-3";
    if (value == "LOW")  return "-4";
    if (value == "AUTO") return "-5";

    // Try to find a #define that matches this symbol (one level of indirection)
    for (const auto& d : defines) {
        if (d.symbol == value) {
            // Avoid infinite recursion — only substitute once
            if (isNumeric(d.rawValue)) return d.rawValue;
            if (d.rawValue == "OFF")  return "-1";
            if (d.rawValue == "ON")   return "-2";
            if (d.rawValue == "HIGH") return "-3";
            if (d.rawValue == "LOW")  return "-4";
            if (d.rawValue == "AUTO") return "-5";
            // Indirect still unresolved — return rawValue as-is
            return d.rawValue;
        }
    }

    // Unresolved named symbol — return verbatim for caller to handle
    return value;
}

// ---------------------------------------------------------------------------
// Step 3: apply extracted defines to populate SimConfig fields
// ---------------------------------------------------------------------------

void ConfigParser::applyDefines(const std::vector<Define>& defines, SimConfig& cfg) {
    // We need lookup-by-symbol frequently; build a flat search helper
    auto find = [&](const std::string& sym) -> std::string {
        for (const auto& d : defines) {
            if (d.symbol == sym) return substituteTokens(d.rawValue, defines);
        }
        return "";  // not present
    };

    auto findRaw = [&](const std::string& sym) -> std::string {
        for (const auto& d : defines) {
            if (d.symbol == sym) return d.rawValue;
        }
        return "";
    };

    // -----------------------------------------------------------------------
    // Firmware / controller identity
    // -----------------------------------------------------------------------
    {
        std::string hn = findRaw("HOST_NAME");
        if (!hn.empty()) {
            std::string stripped = stripQuotes(hn);
            std::strncpy(cfg.configName, stripped.c_str(), sizeof(cfg.configName) - 1);
            cfg.configName[sizeof(cfg.configName) - 1] = '\0';
        }
    }

    // -----------------------------------------------------------------------
    // Weather
    // -----------------------------------------------------------------------
    {
        std::string w = find("WEATHER");
        // OFF = -1; anything else (BME280_0x76, BME280, BMP280 …) = active
        cfg.hasWeather = (!w.empty() && w != "-1");
        cfg.hasWeatherWrite = cfg.hasWeather;
    }

    // -----------------------------------------------------------------------
    // Axis 1 — RA / AZM
    // -----------------------------------------------------------------------
    {
        std::string model = find("AXIS1_DRIVER_MODEL");
        bool axis1Active = isDriverModelActive(model);

        if (axis1Active) {
            // steps per degree
            std::string spd = find("AXIS1_STEPS_PER_DEGREE");
            if (!spd.empty() && isNumeric(spd))
                cfg.stepsPerDegree[0] = toDouble(spd, 12800.0);

            std::string lmin = find("AXIS1_LIMIT_MIN");
            if (!lmin.empty() && isNumeric(lmin))
                cfg.limitMin[0] = toDouble(lmin, -180.0);

            std::string lmax = find("AXIS1_LIMIT_MAX");
            if (!lmax.empty() && isNumeric(lmax))
                cfg.limitMax[0] = toDouble(lmax, 180.0);
        }

        // Home and limit sense — stored verbatim (raw value before token substitution)
        {
            std::string sh = findRaw("AXIS1_SENSE_HOME");
            cfg.senseHome[0].symbol = sh.empty() ? "OFF" : sh;

            std::string slmin = findRaw("AXIS1_SENSE_LIMIT_MIN");
            cfg.senseLimitMin[0].symbol = slmin.empty() ? "OFF" : slmin;

            std::string slmax = findRaw("AXIS1_SENSE_LIMIT_MAX");
            cfg.senseLimitMax[0].symbol = slmax.empty() ? "OFF" : slmax;
        }

        // hasHomeSense: AXIS1_SENSE_HOME is not OFF
        {
            std::string sh = find("AXIS1_SENSE_HOME");
            cfg.hasHomeSense = (!sh.empty() && sh != "-1");
        }

        // Store whether axis1 is active (needed to determine hasMount)
        // Done below after axis2 check.
    }

    // -----------------------------------------------------------------------
    // Axis 2 — Dec / ALT
    // -----------------------------------------------------------------------
    {
        std::string model2 = find("AXIS2_DRIVER_MODEL");
        bool axis2Active = isDriverModelActive(model2);

        if (axis2Active) {
            std::string spd = find("AXIS2_STEPS_PER_DEGREE");
            if (!spd.empty() && isNumeric(spd))
                cfg.stepsPerDegree[1] = toDouble(spd, 12800.0);

            std::string lmin = find("AXIS2_LIMIT_MIN");
            if (!lmin.empty() && isNumeric(lmin))
                cfg.limitMin[1] = toDouble(lmin, -90.0);

            std::string lmax = find("AXIS2_LIMIT_MAX");
            if (!lmax.empty() && isNumeric(lmax))
                cfg.limitMax[1] = toDouble(lmax, 90.0);
        }

        {
            std::string sh = findRaw("AXIS2_SENSE_HOME");
            cfg.senseHome[1].symbol = sh.empty() ? "OFF" : sh;
            std::string slmin = findRaw("AXIS2_SENSE_LIMIT_MIN");
            cfg.senseLimitMin[1].symbol = slmin.empty() ? "OFF" : slmin;
            std::string slmax = findRaw("AXIS2_SENSE_LIMIT_MAX");
            cfg.senseLimitMax[1].symbol = slmax.empty() ? "OFF" : slmax;
        }

        // hasMount: both axis1 and axis2 must be active
        {
            std::string model1 = find("AXIS1_DRIVER_MODEL");
            cfg.hasMount = isDriverModelActive(model1) && axis2Active;
        }
    }

    // -----------------------------------------------------------------------
    // Mount type
    // -----------------------------------------------------------------------
    if (cfg.hasMount) {
        std::string mt = find("MOUNT_TYPE");
        // The raw value is the symbolic name (GEM, FORK, etc.) which we
        // didn't substitute to a number — find the raw and map it.
        std::string mtRaw = findRaw("MOUNT_TYPE");
        if      (mtRaw == "GEM"        || mt == "1")  cfg.mountType = MOUNT_GEM;
        else if (mtRaw == "FORK"       || mt == "2")  cfg.mountType = MOUNT_FORK;
        else if (mtRaw == "ALTAZM"     || mt == "3")  cfg.mountType = MOUNT_ALTAZM;
        else if (mtRaw == "ALTALT"     || mt == "4")  cfg.mountType = MOUNT_ALTALT;
        else if (mtRaw == "GEM_TA"     || mt == "5")  cfg.mountType = MOUNT_GEM_TA;
        else if (mtRaw == "GEM_TAC"    || mt == "6")  cfg.mountType = MOUNT_GEM_TAC;
        else if (mtRaw == "FORK_TA"    || mt == "7")  cfg.mountType = MOUNT_FORK_TA;
        else if (mtRaw == "FORK_TAC"   || mt == "8")  cfg.mountType = MOUNT_FORK_TAC;
        else if (mtRaw == "ALTAZM_UNL" || mt == "9")  cfg.mountType = MOUNT_ALTAZM_UNL;
        else cfg.mountType = MOUNT_GEM;  // safe default
    }

    // -----------------------------------------------------------------------
    // Goto
    // -----------------------------------------------------------------------
    {
        std::string gf = find("GOTO_FEATURE");
        // ON = -2, OFF = -1. If present and not OFF, goto is enabled.
        cfg.hasGoto = (!gf.empty() && gf == "-2");
    }

    // -----------------------------------------------------------------------
    // PEC
    // -----------------------------------------------------------------------
    {
        std::string pec = find("PEC_STEPS_PER_WORM_ROTATION");
        if (!pec.empty() && isNumeric(pec)) {
            long steps = static_cast<long>(toDouble(pec, 0.0));
            cfg.pecStepsPerWorm = steps;
            cfg.hasPec = (steps != 0);
        }
    }

    // -----------------------------------------------------------------------
    // PPS
    // -----------------------------------------------------------------------
    {
        std::string pps = find("TIME_LOCATION_PPS_SENSE");
        cfg.hasPPS = (!pps.empty() && pps != "-1");
    }

    // -----------------------------------------------------------------------
    // Slew rate
    // -----------------------------------------------------------------------
    {
        std::string sr = find("SLEW_RATE_BASE_DESIRED");
        if (!sr.empty() && isNumeric(sr))
            cfg.slewRateBaseDesired = toDouble(sr, 1.0);
    }

    // -----------------------------------------------------------------------
    // Sound / buzzer
    // -----------------------------------------------------------------------
    {
        std::string buz = find("STATUS_BUZZER_DEFAULT");
        // ON = -2
        cfg.soundEnabled = (!buz.empty() && buz == "-2");
    }

    // -----------------------------------------------------------------------
    // Axis 3 — Rotator
    // -----------------------------------------------------------------------
    {
        std::string model = find("AXIS3_DRIVER_MODEL");
        cfg.hasRotator = isDriverModelActive(model);
        if (cfg.hasRotator) {
            std::string spd = find("AXIS3_STEPS_PER_DEGREE");
            if (!spd.empty() && isNumeric(spd))
                cfg.stepsPerDegree[2] = toDouble(spd, 64.0);

            std::string lmin = find("AXIS3_LIMIT_MIN");
            if (!lmin.empty() && isNumeric(lmin))
                cfg.limitMin[2] = toDouble(lmin, 0.0);

            std::string lmax = find("AXIS3_LIMIT_MAX");
            if (!lmax.empty() && isNumeric(lmax))
                cfg.limitMax[2] = toDouble(lmax, 360.0);

            std::string sr = find("AXIS3_SLEW_RATE_BASE_DESIRED");
            if (!sr.empty() && isNumeric(sr))
                cfg.focuserSlewRateBase[0] = toDouble(sr, 1.0); // not used for focusers but stored

            // Derotator: rotator present AND alt/az mount
            cfg.hasDerotator = cfg.hasRotator &&
                               (cfg.mountType == MOUNT_ALTAZM ||
                                cfg.mountType == MOUNT_ALTAZM_UNL);

            {
                std::string sh = findRaw("AXIS3_SENSE_HOME");
                cfg.senseHome[2].symbol = sh.empty() ? "OFF" : sh;
                std::string slmin = findRaw("AXIS3_SENSE_LIMIT_MIN");
                cfg.senseLimitMin[2].symbol = slmin.empty() ? "OFF" : slmin;
                std::string slmax = findRaw("AXIS3_SENSE_LIMIT_MAX");
                cfg.senseLimitMax[2].symbol = slmax.empty() ? "OFF" : slmax;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Axes 4-9 — Focusers
    // Contiguous: stop counting at first OFF axis
    // -----------------------------------------------------------------------
    {
        static const char* axisNames[] = {
            "AXIS4","AXIS5","AXIS6","AXIS7","AXIS8","AXIS9"
        };
        cfg.numFocusers = 0;
        for (int i = 0; i < 6; ++i) {
            std::string prefix = axisNames[i];
            std::string model = find(prefix + "_DRIVER_MODEL");
            if (!isDriverModelActive(model)) break;  // contiguous rule

            cfg.numFocusers++;
            int fi = i;  // focuser index 0..5

            std::string spm = find(prefix + "_STEPS_PER_MICRON");
            if (!spm.empty() && isNumeric(spm))
                cfg.stepsPerMicron[fi] = toDouble(spm, 0.5);

            std::string lmin = find(prefix + "_LIMIT_MIN");
            if (!lmin.empty() && isNumeric(lmin))
                cfg.limitMin[3 + fi] = toDouble(lmin, 0.0);

            std::string lmax = find(prefix + "_LIMIT_MAX");
            if (!lmax.empty() && isNumeric(lmax))
                cfg.limitMax[3 + fi] = toDouble(lmax, 50.0);

            std::string sr = find(prefix + "_SLEW_RATE_BASE_DESIRED");
            if (!sr.empty() && isNumeric(sr))
                cfg.focuserSlewRateBase[fi] = toDouble(sr, 500.0);

            cfg.stepsPerDegree[3 + fi] = 0.0;  // not applicable for focusers

            {
                std::string sh = findRaw(prefix + "_SENSE_HOME");
                cfg.senseHome[3 + fi].symbol = sh.empty() ? "OFF" : sh;
                std::string slmin = findRaw(prefix + "_SENSE_LIMIT_MIN");
                cfg.senseLimitMin[3 + fi].symbol = slmin.empty() ? "OFF" : slmin;
                std::string slmax = findRaw(prefix + "_SENSE_LIMIT_MAX");
                cfg.senseLimitMax[3 + fi].symbol = slmax.empty() ? "OFF" : slmax;
            }
        }

        // Focuser temperature
        std::string ft = find("FOCUSER_TEMPERATURE");
        cfg.hasFocuserTemp = (!ft.empty() && ft != "-1");
    }

    // -----------------------------------------------------------------------
    // Aux features (8 slots, 1-indexed in config, 0-indexed in SimConfig)
    // -----------------------------------------------------------------------
    {
        static const char* purposeNames[] = {
            "FEATURE1_PURPOSE","FEATURE2_PURPOSE","FEATURE3_PURPOSE","FEATURE4_PURPOSE",
            "FEATURE5_PURPOSE","FEATURE6_PURPOSE","FEATURE7_PURPOSE","FEATURE8_PURPOSE"
        };
        static const char* nameKeys[] = {
            "FEATURE1_NAME","FEATURE2_NAME","FEATURE3_NAME","FEATURE4_NAME",
            "FEATURE5_NAME","FEATURE6_NAME","FEATURE7_NAME","FEATURE8_NAME"
        };

        // Purpose name -> integer mapping
        auto purposeValue = [](const std::string& raw) -> int {
            if (raw == "-1" || raw == "OFF" || raw.empty()) return FEAT_OFF;
            if (raw == "1"  || raw == "SWITCH")            return FEAT_SWITCH;
            if (raw == "2"  || raw == "ANALOG_OUTPUT"
                            || raw == "ANALOG_OUT")         return FEAT_ANALOG_OUTPUT;
            if (raw == "3"  || raw == "DEW_HEATER")        return FEAT_DEW_HEATER;
            if (raw == "4"  || raw == "INTERVALOMETER")    return FEAT_INTERVALOMETER;
            if (raw == "5"  || raw == "MOMENTARY_SWITCH")  return FEAT_MOMENTARY_SWITCH;
            if (raw == "6"  || raw == "HIDDEN_SWITCH")     return FEAT_HIDDEN_SWITCH;
            if (raw == "7"  || raw == "COVER_SWITCH")      return FEAT_COVER_SWITCH;
            // numeric fallback
            try { return std::stoi(raw); } catch(...) {}
            return FEAT_OFF;
        };

        for (int i = 0; i < 8; ++i) {
            // Purpose: use raw value (before token substitution) for named tokens
            std::string praw = findRaw(purposeNames[i]);
            // Also try substituted form for numeric configs
            std::string psub = find(purposeNames[i]);
            // Prefer raw symbolic names for purpose lookup
            cfg.featurePurpose[i] = purposeValue(praw.empty() ? psub : praw);

            // Name: strip quotes
            std::string nameRaw = findRaw(nameKeys[i]);
            if (!nameRaw.empty()) {
                std::string stripped = stripQuotes(nameRaw);
                // Clamp to 10 chars + NUL
                if (stripped.size() > 10) stripped.resize(10);
                std::strncpy(cfg.featureName[i], stripped.c_str(), 10);
                cfg.featureName[i][10] = '\0';
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool ConfigParser::isDriverModelActive(const std::string& resolvedValue) {
    // A driver model is active when it is NOT the OFF token (-1).
    // Named driver models (TMC2209, A4988, etc.) resolve to their symbol name
    // (not a number) because they aren't in our token table — they are active.
    // Numeric -1 (OFF) means inactive.
    if (resolvedValue.empty()) return false;
    if (resolvedValue == "-1") return false;
    if (resolvedValue == "OFF") return false;
    // Any other value (named driver model string, or non-(-1) numeric) = active
    return true;
}

std::string ConfigParser::stripComment(const std::string& line) {
    // Find // that is not inside a string literal
    bool inStr = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') inStr = !inStr;
        if (!inStr && i + 1 < line.size() && line[i] == '/' && line[i+1] == '/') {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string ConfigParser::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string ConfigParser::stripQuotes(const std::string& s) {
    std::string t = trim(s);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        return t.substr(1, t.size() - 2);
    }
    return t;
}

bool ConfigParser::isNumeric(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[i] == '-' || s[i] == '+') ++i;
    if (i >= s.size()) return false;
    bool hasDot = false;
    for (; i < s.size(); ++i) {
        if (s[i] == '.') {
            if (hasDot) return false;
            hasDot = true;
        } else if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

int ConfigParser::toInt(const std::string& s, int defaultVal) {
    if (!isNumeric(s)) return defaultVal;
    try { return static_cast<int>(std::stod(s)); } catch(...) { return defaultVal; }
}

double ConfigParser::toDouble(const std::string& s, double defaultVal) {
    try { return std::stod(s); } catch(...) { return defaultVal; }
}
