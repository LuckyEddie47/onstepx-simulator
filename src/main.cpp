// main.cpp — OnStepX Simulator entry point

#include "config/ConfigParser.h"
#include "config/SimConfig.h"
#include "state/SimState.h"
#include "state/SimClock.h"
#include "state/MountStateMachine.h"
#include "transport/PtyTransport.h"
#include "protocol/CommandFramer.h"
#include "handlers/FirmwareHandler.h"
#include "handlers/StatusHandler.h"
#include "handlers/SiteHandler.h"
#include "handlers/MountHandler.h"
#include "handlers/LimitsHandler.h"
#include "handlers/ParkHandler.h"
#include "handlers/HomeHandler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <initializer_list>
#include <stdexcept>

static volatile bool g_running = true;
static void signalHandler(int) { g_running = false; }

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
        "  --verbose               Log commands/replies to stderr\n"
        "  --slew-multiplier N     Complete gotos N times faster (default: 10)\n"
        "  --park-duration-ms N    Park/unpark duration in ms (default: 2000)\n"
        "  --home-duration-ms N    Homing duration in ms (default: 3000)\n",
        argv0);
}

static CliOptions parseCli(int argc, char* argv[]) {
    CliOptions opts;
    if (argc < 2) { printUsage(argv[0]); std::exit(EXIT_FAILURE); }
    opts.configPath = argv[1];
    for (int i = 2; i < argc; ++i) {
        if      (!std::strcmp(argv[i], "--verbose"))               opts.verbose = true;
        else if (!std::strcmp(argv[i], "--slew-multiplier") && i+1<argc) opts.slewMultiplier = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--park-duration-ms") && i+1<argc) opts.parkDurationMs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--home-duration-ms") && i+1<argc) opts.homeDurationMs = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "Unknown option: %s\n", argv[i]); printUsage(argv[0]); std::exit(EXIT_FAILURE); }
    }
    return opts;
}

int main(int argc, char* argv[]) {
    CliOptions opts = parseCli(argc, argv);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    SimConfig cfg;
    try { cfg = ConfigParser::parseFile(opts.configPath); }
    catch (const std::exception& ex) {
        std::fprintf(stderr, "Config parse error: %s\n", ex.what());
        return EXIT_FAILURE;
    }

    if (opts.verbose) {
        std::fprintf(stderr, "[sim] Config: %s  mount=%s  focusers=%d  weather=%s\n",
                     cfg.configName, cfg.hasMount?"yes":"no",
                     cfg.numFocusers, cfg.hasWeather?"yes":"no");
    }

    SimState state;
    state.init(cfg);

    SimClock clock;
    clock.setConfig(&cfg); clock.setState(&state);
    clock.setSlewMultiplier(opts.slewMultiplier);
    clock.setParkDurationMs(opts.parkDurationMs);
    clock.setHomeDurationMs(opts.homeDurationMs);
    clock.start();

    MountStateMachine msm;
    msm.setConfig(&cfg); msm.setState(&state); msm.setClock(&clock);

    PtyTransport transport;
    if (!transport.open()) {
        std::fprintf(stderr, "[sim] Failed to open PTY\n");
        clock.stop(); return EXIT_FAILURE;
    }
    std::printf("SIMULATOR_CTL=/tmp/onstepx-sim-ctl.sock\n");
    std::fflush(stdout);

    // Handlers — dispatcher order per plan Section 1.4
    MountHandler  mountHandler;
    StatusHandler statusHandler;
    SiteHandler   siteHandler;
    LimitsHandler limitsHandler;
    ParkHandler   parkHandler;
    HomeHandler   homeHandler;
    FirmwareHandler firmwareHandler;

    mountHandler.setStateMachine(&msm);
    parkHandler.setStateMachine(&msm);
    homeHandler.setStateMachine(&msm);

    for (HandlerBase* h : {
            static_cast<HandlerBase*>(&mountHandler),
            static_cast<HandlerBase*>(&statusHandler),
            static_cast<HandlerBase*>(&siteHandler),
            static_cast<HandlerBase*>(&limitsHandler),
            static_cast<HandlerBase*>(&parkHandler),
            static_cast<HandlerBase*>(&homeHandler),
            static_cast<HandlerBase*>(&firmwareHandler) }) {
        h->setConfig(&cfg);
        h->setState(&state);
    }

    CommandFramer framer;
    framer.setConfig(&cfg); framer.setState(&state);
    framer.addHandler(&mountHandler);
    framer.addHandler(&statusHandler);
    framer.addHandler(&siteHandler);
    framer.addHandler(&limitsHandler);
    framer.addHandler(&parkHandler);
    framer.addHandler(&homeHandler);
    framer.addHandler(&firmwareHandler);

    if (opts.verbose)
        std::fprintf(stderr, "[sim] PTY open: %s\n", transport.slavePath());

    while (g_running) {
        if (!framer.tick(transport, 50)) {
            if (opts.verbose) std::fprintf(stderr, "[sim] Transport error\n");
            break;
        }
    }

    clock.stop();
    transport.close();
    return EXIT_SUCCESS;
}
