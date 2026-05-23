#pragma once
// ConfigParser.h — Parses an OnStepX Config.h file at runtime into a SimConfig.
//
// Parsing rules:
//   - Lines are stripped of // comments before tokenising
//   - Only #define SYMBOL VALUE lines are processed
//   - #ifdef, #include, #if etc. are ignored
//   - Token substitution is applied: OFF->-1, ON->-2, HIGH->-3, LOW->-4, AUTO->-5
//   - Unresolved named symbols (e.g. LIMIT_SENSE, TMC2209) are stored verbatim
//   - Numeric values (integer or float) are parsed after token substitution

#include "SimConfig.h"
#include <string>
#include <vector>

class ConfigParser {
public:
    // Parse a Config.h file by path. Throws std::runtime_error on file open failure.
    static SimConfig parseFile(const std::string& path);

    // Parse from a string of Config.h content (for unit testing without files).
    static SimConfig parseString(const std::string& content);

private:
    struct Define {
        std::string symbol;
        std::string rawValue;
    };

    static std::vector<Define> extractDefines(const std::string& content);
    static void applyDefines(const std::vector<Define>& defines, SimConfig& cfg);

    static std::string substituteTokens(const std::string& value,
                                        const std::vector<Define>& defines);

    static int    toInt(const std::string& s, int defaultVal = TOKEN_OFF);
    static double toDouble(const std::string& s, double defaultVal = 0.0);
    static bool   isNumeric(const std::string& s);
    static std::string stripComment(const std::string& line);
    static std::string trim(const std::string& s);
    static std::string stripQuotes(const std::string& s);
    static bool isDriverModelActive(const std::string& resolvedValue);
};
