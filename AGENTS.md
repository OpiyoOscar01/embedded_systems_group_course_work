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
```
smart_control_and_monitoring_system/
├── CMakeLists.txt
├── main/                    # app entry (app_main), wiring/orchestration only
├── components/              # reusable modules
│   ├── common/              # types, constants, utils, logging helpers
│   ├── drivers/             # HAL for RFID, PIR, LDR, relay/LED, buzzer, keypad/button, SD
│   ├── access_control/      # card validation, occupancy list, anti-passback
│   ├── lighting/            # lighting FSM, daylight logic
│   ├── storage/             # SD card, hash-chain, NVS, data models
│   ├── comms/               # MQTT client, message envelope, topics, sync
│   ├── console/             # serial CLI, auth, menus
│   ├── security/            # auth/session, emergency handling
│   └── time_sync/           # time/opening hours logic
├── docs/                    # requirements, summaries, guides
├── config/                  # defaults, pinmaps, system config
├── test/                    # unit/integration tests (if added)
├── diagram.json             # Wokwi circuit diagram
└── wokwi.toml               # Wokwi config
```

## Coding Standards
- C (ESP-IDF), follow ESP-IDF style/conventions
- Include guards or `#pragma once` where appropriate
- Functions: clear names, single responsibility
- Error checking on ESP-IDF calls (ESP_ERROR_CHECK or handle)
- Use `esp_log` with tags
- Avoid global mutable state where possible; pass contexts
- Comments only if necessary for complex logic
- Keep `main/` thin - delegate to components

## Build & Simulation (Wokwi + ESP-IDF)
- Build: `idf.py build` (Git Bash via helper `idf` or VS Code ESP-IDF)
- Flash/Monitor not needed in pure sim, use Wokwi
- Wokwi: use `wokwi.toml` + `diagram.json` at project root
- Ensure firmware binary referenced in `wokwi.toml` points to correct build output

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
- Wokwi ESP32: https://docs.wokwi.com/parts/wokwi-esp32-devkit-v1
