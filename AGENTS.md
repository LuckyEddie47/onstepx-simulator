# AGENTS.md

## Purpose
This file helps AI coding agents understand the onstepx-simulator repository, its build/test flow, and key architecture patterns.

## Build and test commands
- Create a build directory and generate the project:
  - `cmake -S . -B build`
- Build the simulator and tests:
  - `cmake --build build`
- Run tests:
  - `ctest --test-dir build --output-on-failure`

## Project structure
- `CMakeLists.txt` defines:
  - `sim_core` static library
  - `onstepx-sim` main executable
  - `unit_tests` executable using Google Test
- `src/` contains simulator implementation:
  - `src/config` parser and config headers
  - `src/transport` terminal/PTY transport
  - `src/protocol` command framing
  - `src/handlers` command handlers for firmware, mount, guide, rotator, weather, etc.
  - `src/state` simulator clock and mount state machine
  - `src/fault` fault injection support
- `tests/unit/` contains unit test sources.
- `configs/` contains header-based simulator configuration profiles used by tests.

## Key conventions
- C++17 is required and enforced by `CMakeLists.txt`.
- Global compile warnings are enabled with `-Wall -Wextra -Wpedantic`.
- Unit tests are executed via `unit_tests` with config flags like `--sim-config`, `--gem-config`, and `--aux-config`.
- Tests are grouped by config names and labels in CTest.
- The source code uses a modular handler-based architecture; new simulator features typically add a handler and corresponding tests.

## Notes for AI agents
- Prefer updating existing `AGENTS.md` if it already exists, rather than creating duplicate instruction files.
- The repository has no top-level README or additional docs; use `CMakeLists.txt` and source tree for structure and conventions.
- Keep changes aligned with the current build/test setup and avoid altering the established `sim_core`/`unit_tests` layout unless fixing real issues.
