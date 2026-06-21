cmake_minimum_required(VERSION 3.16)
project(onstepx-sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Core simulator library
# ---------------------------------------------------------------------------
add_library(sim_core STATIC
    src/config/ConfigParser.cpp
    src/transport/PtyTransport.cpp
    src/protocol/CommandFramer.cpp
    src/handlers/FirmwareHandler.cpp
    src/handlers/StatusHandler.cpp
    src/state/SimClock.cpp
    src/state/MountStateMachine.cpp
    src/handlers/SiteHandler.cpp
    src/handlers/MountHandler.cpp
    src/handlers/LimitsHandler.cpp
    src/handlers/ParkHandler.cpp
    src/handlers/HomeHandler.cpp
    src/handlers/GotoHandler.cpp
    src/handlers/GuideHandler.cpp
    src/handlers/PecHandler.cpp
    # Phase 4:
    src/handlers/AxisHandler.cpp
    src/handlers/FocuserHandler.cpp
    src/handlers/RotatorHandler.cpp
    # Phase 5:
    src/handlers/FeaturesHandler.cpp
    src/handlers/WeatherHandler.cpp
    # Phase 6:
    src/fault/FaultInjector.cpp
    # Phase 7:
    src/handlers/LibraryHandler.cpp
    src/handlers/TelescopeHandler.cpp
)

target_include_directories(sim_core PUBLIC src)
target_link_libraries(sim_core PUBLIC pthread)

# ---------------------------------------------------------------------------
# Main simulator executable
# ---------------------------------------------------------------------------
add_executable(onstepx-sim src/main.cpp)
target_link_libraries(onstepx-sim PRIVATE sim_core)

# ---------------------------------------------------------------------------
# Unit test executable
# ---------------------------------------------------------------------------
find_package(GTest REQUIRED)
enable_testing()

add_executable(unit_tests
    tests/unit/SimTestBase.cpp
    tests/unit/test_config_parser.cpp
    tests/unit/test_command_framer.cpp
    tests/unit/test_firmware_handler.cpp
    tests/unit/test_status_handler.cpp
    tests/unit/test_site_handler.cpp
    tests/unit/test_sim_clock.cpp
    tests/unit/test_mount_handler.cpp
    tests/unit/test_mount_state_machine.cpp
    tests/unit/test_goto_handler.cpp
    tests/unit/test_park_handler.cpp
    tests/unit/test_home_handler.cpp
    tests/unit/test_guide_handler.cpp
    tests/unit/test_pec_handler.cpp
    # Phase 4:
    tests/unit/test_focuser_handler.cpp
    tests/unit/test_rotator_handler.cpp
    # Phase 5:
    tests/unit/test_features_handler.cpp
    tests/unit/test_weather_handler.cpp
    # Phase 6:
    tests/unit/test_fault_injector.cpp
    # Phase 7:
    tests/unit/test_library_handler.cpp
    tests/unit/test_telescope_handler.cpp
    # Phase 9:
    tests/unit/test_coord_format.cpp
)

target_include_directories(unit_tests PRIVATE src tests/unit)
target_link_libraries(unit_tests PRIVATE sim_core GTest::gtest)

# ---------------------------------------------------------------------------
# Config profiles (DEC-002).
# ---------------------------------------------------------------------------
set(SIM_CONFIGS
    gem_full
    aux_features
    minimal
    gem_basic
    fork_altaz
    gem_rotator
    gem_focuser1
    gem_focuser_multi
    aux_weather
    kitchen_sink
)

foreach(CFG ${SIM_CONFIGS})
    add_test(
        NAME unit_${CFG}
        COMMAND unit_tests
            --sim-config=${CMAKE_SOURCE_DIR}/configs/${CFG}.h
    )
    set_tests_properties(unit_${CFG} PROPERTIES
        LABELS "unit;${CFG}"
    )
endforeach()

# ---------------------------------------------------------------------------
# Config parser self-test (DEC-001 special case).
# ---------------------------------------------------------------------------
add_test(
    NAME config_parser_selftest
    COMMAND unit_tests
        --gtest_filter=ConfigParser*
        --sim-config=${CMAKE_SOURCE_DIR}/configs/gem_full.h
        --gem-config=${CMAKE_SOURCE_DIR}/configs/gem_full.h
        --aux-config=${CMAKE_SOURCE_DIR}/configs/aux_features.h
)
set_tests_properties(config_parser_selftest PROPERTIES
    LABELS "unit;parser"
)
