#pragma once
// PecHandler.h — Handles PEC read/write/control commands.
//
// Commands handled (matching Pec.command.cpp):
//
// Always (regardless of hasPec):
//   :GXE6#     Steps per sidereal second (fixed value from config)
//   :GXE7#     PEC worm rotation steps
//   :GXE8#     PEC buffer size in seconds
//   :VS#       Steps per sidereal second (same as GXE6, different format)
//   :VW#       Worm rotation steps (formatted as %06ld)
//   :$QZ?#     Get PEC state string ("IpPrR" + optional '.')
//
// Only when hasPec (AXIS1_PEC == ON in config):
//   :GX91#     PEC analog value (always 0)
//   :VH#       PEC index sense position in sidereal seconds
//   :VR[n]#    Read PEC table entry
//   :Vr[n]#    Read 10-byte frame in hex
//   :WR+#      Shift PEC table forward one second
//   :WR-#      Shift PEC table back one second
//   :WR[n,sn]# Write PEC table entry
//   :SXE7,[n]# Set worm rotation steps
//   :$QZ+#     Enable PEC playback
//   :$QZ-#     Disable PEC
//   :$QZ/#     Ready record PEC
//   :$QZZ#     Clear PEC buffer
//   :$QZ!#     Write PEC to NV (sim: mark as recorded)

#include "HandlerBase.h"

class PecHandler : public HandlerBase {
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
    // PEC buffer size in seconds (matches firmware default for a typical worm)
    // The config may override this; for now use the stored pecWormSteps / sidereal rate.
    // Firmware uses bufferSize derived from wormRotationSteps; we mirror this.
    long bufferSize() const;

    // Steps per sidereal second — derived from config stepsPerDegreeAxis1
    double stepsPerSiderealSecond() const;
};
