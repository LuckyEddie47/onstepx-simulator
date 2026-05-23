// main.cpp — OnStepX Simulator entry point
//
// Usage: onstepx-sim <config.h> [options]
//
// Options:
//   --verbose               Log all commands/replies to stderr
//   --slew-multiplier N     Complete gotos N× faster (default: 10)
//   --park-duration-ms N    Simulated park duration in ms (default: 2000)
//   --home-duration-ms N    Simulated homing duration in ms (default: 3000)
//
// Startup output (stdout, parsed by test harnesses):
//   SIMULATOR_PTY=/dev/pts/N
//   SIMULATOR_CTL=/tmp/onstepx-sim-ctl.sock

#include "config/ConfigParser.h"
#include "config/SimConfig.h"
#include "state/SimState.h"
#include "transport/PtyTransport.h"
#include "protocol/CommandFramer.h"
#include "handlers/FirmwareHandler.h"
#include "handlers/StatusHandler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Global run flag (set to false by SIGINT/SIGTERM)
// ---------------------------------------------------------------------------
static volatile bool g_running = true;

static void signalHandler(int) {
    g_running = false;
}

// ---------------------------------------------------------------------------
// CLI parsing
// ---------------------------------------------------------------------------
struct CliOptions {
    const char* configPath      = nullptr;
    bool        verbose         = false;
    int         slewMultiplier  = 10;
    int         parkDurationMs  = 2000;
    int         homeDurationMs  = 3000;
};

static void printUsage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s <config.h> [options]\n"
        "\n"
        "Options:\n"
        "  --verbose               Log commands/replies to stderr\n"
        "  --slew-multiplier N     Complete gotos N times faster (default: 10)\n"
        "  --park-duration-ms N    Park/unpark duration in ms (default: 2000)\n"
        "  --home-duration-ms N    Homing duration in ms (default: 3000)\n",
        argv0);
}

static CliOptions parseCli(int argc, char* argv[]) {
    CliOptions opts;
    if (argc < 2) {
        printUsage(argv[0]);
        std::exit(EXIT_FAILURE);
    }
    opts.configPath = argv[1];

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0) {
            opts.verbose = true;
        } else if (std::strcmp(argv[i], "--slew-multiplier") == 0 && i + 1 < argc) {
            opts.slewMultiplier = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--park-duration-ms") == 0 && i + 1 < argc) {
            opts.parkDurationMs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--home-duration-ms") == 0 && i + 1 < argc) {
            opts.homeDurationMs = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            std::exit(EXIT_FAILURE);
        }
    }
    return opts;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    CliOptions opts = parseCli(argc, argv);

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse config
    SimConfig cfg;
    try {
        cfg = ConfigParser::parseFile(opts.configPath);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Config parse error: %s\n", ex.what());
        return EXIT_FAILURE;
    }

    if (opts.verbose) {
        std::fprintf(stderr, "[sim] Config: %s  mount=%s  focusers=%d  weather=%s\n",
                     cfg.configName,
                     cfg.hasMount ? "yes" : "no",
                     cfg.numFocusers,
                     cfg.hasWeather ? "yes" : "no");
    }

    // Initialise state from config
    SimState state;
    state.init(cfg);

    // Open PTY (prints SIMULATOR_PTY= line to stdout)
    PtyTransport transport;
    if (!transport.open()) {
        std::fprintf(stderr, "[sim] Failed to open PTY\n");
        return EXIT_FAILURE;
    }

    // Announce control socket path (Phase 6 stub)
    std::printf("SIMULATOR_CTL=/tmp/onstepx-sim-ctl.sock\n");
    std::fflush(stdout);

    // Build handler chain
    FirmwareHandler firmwareHandler;
    StatusHandler   statusHandler;

    firmwareHandler.setConfig(&cfg);
    firmwareHandler.setState(&state);
    statusHandler.setConfig(&cfg);
    statusHandler.setState(&state);

    CommandFramer framer;
    framer.setConfig(&cfg);
    framer.setState(&state);
    framer.addHandler(&statusHandler);
    framer.addHandler(&firmwareHandler);

    if (opts.verbose) {
        std::fprintf(stderr, "[sim] PTY open: %s\n", transport.slavePath());
    }

    // Main I/O loop
    while (g_running) {
        if (!framer.tick(transport, 50)) {
            if (opts.verbose) {
                std::fprintf(stderr, "[sim] Transport error\n");
            }
            break;
        }
    }

    transport.close();
    return EXIT_SUCCESS;
}
