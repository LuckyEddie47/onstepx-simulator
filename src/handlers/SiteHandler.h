#pragma once
// SiteHandler.h — Handles all site and time commands.
//
// Source reference: Site.command.cpp
//
// Get commands:
//   :Ga#  :GC#  :Gc#  :GG#  :Gg#/:GgH#  :GL#/:GLH#
//   :GM-P#  :GS#/:GSH#  :Gt#/:GtH#  :Gv#
//   :GX80#  :GX81#  :GX89#
//
// Set commands:
//   :SC[MM/DD/YY]#  :SG[sHH:MM]#  :Sg[...]#  :SL[HH:MM:SS]#
//   :SM-P[name]#    :St[...]#      :SU[s.s]#  :Sv[n.n]#
//
// Site selection:
//   :W[0-3]#  :W?#

#include "HandlerBase.h"

class SiteHandler : public HandlerBase {
public:
    bool handle(
        const char*   cmd,
        const char*   param,
        char*         reply,
        bool*         suppressFrame,
        bool*         numericReply,
        CommandError* error
    ) override;

private:
    // Format helpers matching firmware convert.*() output exactly
    void doubleToHms(char* buf, double hours, bool showSign, bool highPrec) const;
    void doubleToDms(char* buf, double deg,   bool showSign, bool azimuth,
                     bool highPrec) const;

    // Date/time helpers
    double getLST()  const;  // Local Sidereal Time in hours
    double getLocalTime() const;  // Local civil time in hours

    // Parse helpers
    bool parseDate(const char* param, int* y, int* m, int* d) const;
    bool parseTime(const char* param, double* hours) const;
    bool parseDms(const char* param, double* deg, bool latitude) const;
    bool parseTz(const char* param, double* hours) const;
};
