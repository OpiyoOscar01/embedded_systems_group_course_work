# AGENTS.md - Smart Control and Monitoring System

## Project Info
- **Project**: CoCIS Smart Building System (Smart Control and Monitoring System)
- **Student**: Opiyo Oscar (Fourth Year Software Engineering, Makerere University)
- **Course**: Embedded Systems Course Unit
- **Platform**: ESP32 (3 terminals), ESP-IDF v5.2.x, Wokwi Simulation (wokwi.toml + diagram.json)
- **Environment**: VS Code + Git Bash/ESP-IDF extension, no PlatformIO
- **Submission Deadline**: 2 November 2026

## Project Summary
Three ESP32 terminals (Entrance Hub, Lecture Room 1, Lecture Room 2) form a smart building system simulating:
- RFID-based access control at main entrance (ENTRY/EXIT), anti-passback, occupancy tracking
- Room automation: PIR motion + LDR daylight-aware lighting, auto-off after inactivity with warning
- Tamper-evident logging to SD card (hash-chain)
- MQTT communication over Wi-Fi between terminals
- Serial console for admin/security (auth required for config)
- Opening hours enforcement, emergency button with roll call
- Offline resilience per room hub logic

## Architecture Principles (Embedded/IoT Best Practices)
- **Separation of Concerns**: decouple hardware drivers, business logic, comms, storage, UI/console
- **Modularity**: use components/ directory for reusable modules (drivers, sensors, access, logging, mqtt, nvs, etc.)
- **State Machines**: explicit FSMs for doors, lighting, access control, and system modes
- **Event-Driven/RTOS**: use FreeRTOS tasks, queues, semaphores/mutexes appropriately
- **Defensive Programming**: validate inputs, handle errors, timeouts, failures gracefully
- **Offline-First/Resilient**: room nodes must function locally if network drops
- **Security**: password auth for admin actions, avoid hardcoded secrets in logs, validate RFID/cards
- **Data Integrity**: append-only logs, hash chaining for tamper detection
- **Determinism**: use explicit timing/constants (see Appendix C of requirements)
- **Testability**: keep logic pure/testable where possible, hardware abstracted behind interfaces

## Folder Structure (Scalable & Decoupled)
Three ESP-IDF projects sharing common components (see docs/IMPLEMENTATION_GUIDE.md).
```
smart_control_and_monitoring_system/
├── common/                  # shared ESP-IDF components (EXTRA_COMPONENT_DIRS)
│   ├── cocis_types/         # enums, reason codes, event structs, constants
│   ├── cocis_proto/         # JSON envelope, topic builders, event id
│   ├── cocis_time/          # SNTP, RTC fallback, EAT + schedule evaluation
│   └── cocis_log_chain/     # SHA-256 chained log builder + verifier
├── entrance_node/           # ESP-IDF project: Entrance Hub
│   ├── main/                # app_main, orchestration only
│   ├── components/          # hal_*, access_ctrl, occupancy, schedule, auth,
│   │                        # cli, storage, net_sync, emergency, display
│   ├── CMakeLists.txt
│   ├── diagram.json         # Wokwi circuit
│   └── wokwi.toml
├── room_node/               # ESP-IDF project: Room (ROOM_ID via Kconfig)
│   ├── main/                # app_main, orchestration only
│   ├── components/          # hal_pir, hal_ldr, hal_relay, hal_led, light_fsm,
│   │                        # energy, net_sync, diag
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild    # CONFIG_COCIS_ROOM_ID
│   ├── diagram.json
│   └── wokwi.toml
├── docs/                    # requirements, summaries, implementation guide
├── scripts/                 # demo sequences, helper scripts
└── AGENTS.md
```
Rooms 1 and 2 use the same room_node firmware; only CONFIG_COCIS_ROOM_ID differs.

## Coding Standards
- C (ESP-IDF), follow ESP-IDF style/conventions
- Include guards or `#pragma once` where appropriate
- Functions: clear names, single responsibility
- Error checking on ESP-IDF calls (ESP_ERROR_CHECK or handle)
- Use `esp_log` with tags
- Avoid global mutable state where possible; pass contexts
- Comments only if necessary for complex logic
- Keep `main/` thin - delegate to components

## Wokwi Gotchas (verified against docs)
- Pin names in `diagram.json` must match the board definition, e.g. `esp:D2`, `esp:D4`, `esp:GND.1`, `esp:3V3` (not bare `2`/`4`). See https://github.com/wokwi/wokwi-boards/blob/main/boards/esp32-devkit-v1/board.json
- The ESP32 DevKit V1 has an onboard blue LED wired to `D2`/GPIO2, so driving GPIO2 lights it too. Do not confuse it with an external LED.
- `wokwi-led` pins are `A` (anode) and `C` (cathode); `wokwi-resistor` pins are `1` and `2`.

## Build & Simulation (Wokwi + ESP-IDF)
- Each terminal is its own ESP-IDF project: run `idf.py build` inside
  `entrance_node/` or `room_node/` (not the repo root).
- Shared components live in `common/` and are pulled in via `EXTRA_COMPONENT_DIRS`.
- Flash/Monitor not needed in pure sim, use Wokwi.
- Wokwi: use each project's own `wokwi.toml` + `diagram.json`.
- Firmware paths: `entrance_node` -> `build/entrance_node.bin`,
  `room_node` -> `build/room_node.bin` (project name drives the binary name).
- Room 2: use `sdkconfig.defaults.room2` (CONFIG_COCIS_ROOM_ID=2).

## Quick Start (Initial Setup Goal)
1. Understand requirements from docs/
2. Setup folder structure/components
3. Add `wokwi.toml` and `diagram.json` if missing
4. Make `app_main` print: `Welcome to Embedded Coursework.`
5. Build with ESP-IDF, run in Wokwi to verify

## References
- CoCIS Smart System Simple Summary (docs/)
- CoCIS Smart System Requirements and Design Specification (docs/)
- ESP32 Project Setup Guide (docs/)
- ESP-IDF Docs: https://docs.espressif.com/projects/esp-idf/
- ESP-IDF API Reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/
- ESP-IDF Programming Guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/
- Wokwi Docs: https://docs.wokwi.com/
- Wokwi ESP32: https://docs.wokwi.com/parts/wokwi-esp32-devkit-v1
- Wokwi LCD1602: https://docs.wokwi.com/parts/wokwi-lcd1602
- Always consult ESP-IDF and Wokwi documentation when implementing drivers/peripherals/simulation configuration.
