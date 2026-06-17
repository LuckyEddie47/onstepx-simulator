// main.cpp — OnStepX Simulator entry point

#include "config/ConfigParser.h"
#include "config/SimConfig.h"
#include "state/SimState.h"
#include "state/SimClock.h"
#include "state/MountStateMachine.h"
#include "transport/PtyTransport.h"
#include "protocol/CommandFramer.h"
#include "fault/FaultInjector.h"

// Handlers — Phase 1/2
#include "handlers/FirmwareHandler.h"
#include "handlers/StatusHandler.h"
#include "handlers/SiteHandler.h"
#include "handlers/MountHandler.h"
#include "handlers/LimitsHandler.h"
// Handlers — Phase 3
#include "handlers/ParkHandler.h"
#include "handlers/HomeHandler.h"
#include "handlers/GotoHandler.h"
#include "handlers/GuideHandler.h"
#include "handlers/PecHandler.h"
// Handlers — Phase 4
#include "handlers/AxisHandler.h"
#include "handlers/FocuserHandler.h"
#include "handlers/RotatorHandler.h"
// Handlers — Phase 5
#include "handlers/FeaturesHandler.h"
#include "handlers/WeatherHandler.h"
// Handlers — Phase 7
#include "handlers/LibraryHandler.h"
#include "handlers/TelescopeHandler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <initializer_list>
#include <stdexcept>

static volatile bool g_running = true;
static void signalHandler(int) { g_running = false; }

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------

struct CliOptions {
    const char* configPath      = nullptr;
    bool        verbose         = false;
    int         slewMultiplier  = 10;
    int         parkDurationMs  = 2000;
    int         homeDurationMs  = 3000;
    const char* ctlSocketPath   = "/tmp/onstepx-sim-ctl.sock";
    bool        noFaultSocket   = false;
};

static void printUsage(const char* argv0) {
    std::fprintf(stderr,
        "Usage: %s <config.h> [options]\n"
        "  --verbose                  Log commands/replies to stderr\n"
        "  --slew-multiplier N        Complete gotos N times faster (default: 10)\n"
        "  --park-duration-ms N       Park/unpark duration in ms (default: 2000)\n"
        "  --home-duration-ms N       Homing duration in ms (default: 3000)\n"
        "  --ctl-socket <path>        Fault injector socket path\n"
        "                             (default: /tmp/onstepx-sim-ctl.sock)\n"
        "  --no-fault-socket          Disable fault injector socket\n",
        argv0);
}

static CliOptions parseCli(int argc, char* argv[]) {
    CliOptions opts;
    if (argc < 2) { printUsage(argv[0]); std::exit(EXIT_FAILURE); }
    opts.configPath = argv[1];
    for (int i = 2; i < argc; ++i) {
        if      (!std::strcmp(argv[i], "--verbose"))
            opts.verbose = true;
        else if (!std::strcmp(argv[i], "--no-fault-socket"))
            opts.noFaultSocket = true;
        else if (!std::strcmp(argv[i], "--slew-multiplier") && i+1 < argc)
            opts.slewMultiplier = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--park-duration-ms") && i+1 < argc)
            opts.parkDurationMs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--home-duration-ms") && i+1 < argc)
            opts.homeDurationMs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--ctl-socket") && i+1 < argc)
            opts.ctlSocketPath = argv[++i];
        else {
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
    try { cfg = ConfigParser::parseFile(opts.configPath); }
    catch (const std::exception& ex) {
        std::fprintf(stderr, "Config parse error: %s\n", ex.what());
        return EXIT_FAILURE;
    }

    if (opts.verbose) {
        std::fprintf(stderr,
            "[sim] Config: %s  mount=%s  goto=%s  focusers=%d  rotator=%s  weather=%s\n",
            cfg.configName,
            cfg.hasMount    ? "yes" : "no",
            cfg.hasGoto     ? "yes" : "no",
            cfg.numFocusers,
            cfg.hasRotator  ? "yes" : "no",
            cfg.hasWeather  ? "yes" : "no");
    }

    // State
    SimState state;
    state.init(cfg);

    // Clock
    SimClock clock;
    clock.setConfig(&cfg);
    clock.setState(&state);
    clock.setSlewMultiplier(opts.slewMultiplier);
    clock.setParkDurationMs(opts.parkDurationMs);
    clock.setHomeDurationMs(opts.homeDurationMs);
    clock.start();

    // Mount state machine
    MountStateMachine msm;
    msm.setConfig(&cfg);
    msm.setState(&state);
    msm.setClock(&clock);

    // Transport
    PtyTransport transport;
    if (!transport.open()) {
        std::fprintf(stderr, "[sim] Failed to open PTY\n");
        clock.stop();
        return EXIT_FAILURE;
    }

    // Fault injector (Phase 6)
    FaultInjector faultInjector;
    bool faultSocketOk = false;
    if (!opts.noFaultSocket) {
        faultInjector.setState(&state);
        faultSocketOk = faultInjector.start(opts.ctlSocketPath);
        if (!faultSocketOk && opts.verbose) {
            std::fprintf(stderr,
                "[sim] Warning: fault injector socket failed to start at %s\n",
                opts.ctlSocketPath);
        }
    }

    if (faultSocketOk) {
        std::printf("SIMULATOR_CTL=%s\n", opts.ctlSocketPath);
        std::fflush(stdout);
    }

    // -----------------------------------------------------------------------
    // Handlers — dispatcher order per plan Section 1.4.
    // ParkHandler before RotatorHandler (hP/hR routing).
    // LibraryHandler before FirmwareHandler (L* commands).
    // TelescopeHandler before FirmwareHandler (B+, EC, E* commands).
    // FirmwareHandler last — catches remaining GV* etc.
    // -----------------------------------------------------------------------

    // Phase 1/2
    MountHandler    mountHandler;
    StatusHandler   statusHandler;
    SiteHandler     siteHandler;
    LimitsHandler   limitsHandler;
    FirmwareHandler firmwareHandler;
    // Phase 3
    ParkHandler     parkHandler;
    HomeHandler     homeHandler;
    GotoHandler     gotoHandler;
    GuideHandler    guideHandler;
    PecHandler      pecHandler;
    // Phase 4
    AxisHandler     axisHandler;
    FocuserHandler  focuserHandler;
    RotatorHandler  rotatorHandler;
    // Phase 5
    FeaturesHandler featuresHandler;
    WeatherHandler  weatherHandler;
    // Phase 7
    LibraryHandler  libraryHandler;
    TelescopeHandler telescopeHandler;

    // Wire state machine into handlers that need it
    mountHandler.setStateMachine(&msm);
    parkHandler.setStateMachine(&msm);
    homeHandler.setStateMachine(&msm);
    gotoHandler.setStateMachine(&msm);
    guideHandler.setStateMachine(&msm);

    // Wire fault injector into AxisHandler (Phase 6)
    axisHandler.setFaultInjector(faultSocketOk ? &faultInjector : nullptr);

    // Set config and state on all handlers
    for (HandlerBase* h : {
            static_cast<HandlerBase*>(&mountHandler),
            static_cast<HandlerBase*>(&statusHandler),
            static_cast<HandlerBase*>(&siteHandler),
            static_cast<HandlerBase*>(&limitsHandler),
            static_cast<HandlerBase*>(&firmwareHandler),
            static_cast<HandlerBase*>(&parkHandler),
            static_cast<HandlerBase*>(&homeHandler),
            static_cast<HandlerBase*>(&gotoHandler),
            static_cast<HandlerBase*>(&guideHandler),
            static_cast<HandlerBase*>(&pecHandler),
            static_cast<HandlerBase*>(&axisHandler),
            static_cast<HandlerBase*>(&focuserHandler),
            static_cast<HandlerBase*>(&rotatorHandler),
            static_cast<HandlerBase*>(&featuresHandler),
            static_cast<HandlerBase*>(&weatherHandler),
            static_cast<HandlerBase*>(&libraryHandler),
            static_cast<HandlerBase*>(&telescopeHandler) }) {
        h->setConfig(&cfg);
        h->setState(&state);
    }

    // Framer
    CommandFramer framer;
    framer.setConfig(&cfg);
    framer.setState(&state);
    if (faultSocketOk) framer.setFaultInjector(&faultInjector);

    // Register handlers in dispatcher order
    framer.addHandler(&mountHandler);
    framer.addHandler(&statusHandler);
    framer.addHandler(&siteHandler);
    framer.addHandler(&limitsHandler);
    framer.addHandler(&parkHandler);
    framer.addHandler(&homeHandler);
    framer.addHandler(&gotoHandler);
    framer.addHandler(&guideHandler);
    framer.addHandler(&pecHandler);
    framer.addHandler(&axisHandler);
    framer.addHandler(&focuserHandler);
    framer.addHandler(&rotatorHandler);
    framer.addHandler(&featuresHandler);
    framer.addHandler(&weatherHandler);
    framer.addHandler(&libraryHandler);
    framer.addHandler(&telescopeHandler);
    framer.addHandler(&firmwareHandler);  // last — catches remaining GV* etc.

    if (opts.verbose) {
        std::fprintf(stderr, "[sim] PTY: %s\n", transport.slavePath());
        if (faultSocketOk)
            std::fprintf(stderr, "[sim] CTL: %s\n", opts.ctlSocketPath);
    }

    // Main loop
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
