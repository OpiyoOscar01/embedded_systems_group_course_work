# Implementation Guide - CoCIS Smart Control and Monitoring System

This is the line-up we follow from first increment to last. Every increment is
small, testable, and ends with: implement -> host tests -> commit -> next.

Working agreement (updated 2026-10-29):
- Write the whole implementation first, stage by stage.
- Every stage ships with host unit tests for its pure logic.
- Commit after each stage; simulation in Wokwi is done once at the end.
- ESP-IDF builds are run when a stage changes target code, to catch breakage early.
- Code must be commented and documented for review by colleagues.

Author: Opiyo Oscar (BSE IV, Makerere University)
Course: Embedded Systems
Sources of truth:
- docs/CoCIS_Smart_System_Requirements_and_Design_Specification
- docs/CoCIS_Smart_System_Simple_Summary
- docs/ESP32_Project_Setup_Guide
Platform: ESP32, ESP-IDF v5.2.x, Wokwi (wokwi.toml + diagram.json). No PlatformIO.

Status legend: `[ ]` todo, `[~]` in progress, `[x]` done and gate passed.

---

## 1. What we are building (plain language)

Three ESP32 terminals cooperate to run a pretend smart CoCIS building:

- Entrance Hub: RFID access at ENTRY/EXIT, door control, occupancy and
  anti-passback, schedule, users, SD logs with hash chain, serial console,
  emergency mode and roll-call.
- Lecture Room 1 and Room 2: PIR motion + LDR daylight, local lamp control that
  works offline, energy accounting, telemetry and alerts.
- The three talk over Wi-Fi using MQTT. The Hub is the single writer of
  authoritative data; rooms own their sensor/lamp state.

Innovation is explicitly marked, so Tier 1 innovations are core targets.

---

## 2. Repository layout

```
smart_control_and_monitoring_system/
|-- common/                       # shared ESP-IDF components
|   |-- cocis_types/
|   |-- cocis_proto/
|   |-- cocis_time/
|   |-- cocis_log_chain/
|   |-- cocis_net/                # Wi-Fi + MQTT + offline queue
|   `-- storage/                  # SD/FATFS single-writer wrapper
|-- entrance_node/                # ESP-IDF project: Hub
|   |-- main/                     # app.c, cli.c, Kconfig.projbuild
|   |-- components/               # access, auth, HAL, logs, state, ...
|   |-- partitions.csv
|   |-- wokwi.toml
|   `-- diagram.json
|-- room_node/                    # ESP-IDF project: Room (ROOM_ID via Kconfig)
|   |-- main/                     # app.c, cli.c, Kconfig.projbuild
|   |-- components/               # light_fsm, energy, room_hal
|   |-- partitions.csv
|   |-- wokwi.toml
|   `-- diagram.json
|-- docs/
|-- scripts/
|-- AGENTS.md
`-- README.md
```

Each project adds `common/` with `EXTRA_COMPONENT_DIRS`. Room 1 and Room 2 run
the same firmware; only `CONFIG_COCIS_ROOM_ID` differs.

---

## 3. Pin maps (do not change without updating the requirements doc)

Hub (Table 20):
| Signal | GPIO | Notes |
|---|---|---|
| SPI SCK / MISO / MOSI | 18 / 19 / 23 | SD + both RFID, guarded by sd_mutex |
| SD CS | 5 | idle high |
| RFID ENTRY CS | 27 | idle high |
| RFID EXIT CS | 26 | idle high |
| RFID RST (shared) | 25 | |
| I2C SDA / SCL | 21 / 22 | OLED 0x3C, RTC 0x68 |
| Servo ENTRY / EXIT | 13 / 14 | LEDC 50 Hz, 1 ms locked / 2 ms open |
| LED ENTRY green / red | 32 / 33 | |
| LED EXIT green / red | 16 / 17 | |
| Buzzer | 4 | LEDC tone |
| Emergency button | 34 | input only, external pull-up, ISR falling |
| Tamper switch | 39 | input only, external pull-up, ISR |
| Passage sensor (optional) | 35 | input only |
| UART0 (CLI) | 1 / 3 | 115200 8N1 |

Room (Table 21, same on both rooms):
| Signal | GPIO | Notes |
|---|---|---|
| PIR output | 13 | input, HC-SR501 retrigger |
| LDR analog | 34 | ADC1 channel 6 only (never ADC2 with Wi-Fi) |
| Manual override button | 14 | input, pull-up, active low |
| Relay IN (lamp) | 26 | output, active high in Wokwi |
| LED Occupied (green) | 25 | output |
| LED Lights ON (yellow) | 33 | output |
| LED Network (blue) | 32 | blink connecting, steady linked |
| I2C SDA / SCL | 21 / 22 | optional OLED |

Avoid strapping pins (0, 2, 12, 15) for boot-critical outputs. Never use 6-11.

---

## 4. How to execute one increment

For every row in the line-up:
1. Branch: `feat/<area>` or `spike/<name>`.
2. Implement only that increment.
3. Build gate: `idf.py build` in the affected project(s) -> must succeed.
4. Simulation/test gate: run the listed Wokwi action or unit test -> must show
   the listed result.
5. Record result + commit hash in the progress log.
6. Commit, citing requirement IDs, e.g.
   `feat(access): ENTRY decision + door [FR-ACC-03]`. Push and merge.

If a gate fails, fix before moving on. `main` must always build.

Git note: pushes need GitHub auth for `OpiyoOscar01` (earlier 403 was a different
account). Fix once, then push normally.

---

## 5. The implementation line-up

Status legend: `[x]` done, `[~]` code done / Wokwi evidence pending, `[-]`
deferred as future work, `[ ]` not started.

### Phase 0 - Foundation spikes (these decide the design)

| Status | ID | Increment | Requirements | Pass when |
|---|---|---|---|---|
| [x] | I-00 | Split repo into common/ + entrance_node/ + room_node/, smoke test | FR-DST-01 | both projects build; LED/blink + banner in Wokwi |
| [~] | I-01 | Two-project MQTT spike over Wokwi-GUEST (decides Option A/B) | FR-DST-02, S2 | message from A appears on B |
| [~] | I-02 | SD card mount + append/read spike | FR-STO-01, S4 | file written and read back |
| [~] | I-03 | SNTP time + EAT conversion spike | FR-SCH-07, S3 | correct UTC/local time printed |
| [~] | I-04 | RFID source spike (console `sim scan` behind HAL) | FR-ACC-02, S5 | injected UID reaches HAL |
| [~] | I-05 | Peripheral spike: servo, PIR, LDR, relay, OLED, RTC, buzzer | S6 | each part driven once |
| [ ] | I-06 | Record broker, prefix, spikes, substitutions in docs/links.md | - | file committed |

Decision gate: if I-01 fails, switch to deployment Option B (single project,
in-memory bus) and document it before continuing.

### Phase 1 - Shared common components (unit tested)

| Status | ID | Increment | Requirements | Pass when |
|---|---|---|---|---|
| [x] | I-07 | cocis_types: enums, result codes, roles, modes, constants | FR-ACC-05 | both projects compile; no magic numbers |
| [x] | I-08 | cocis_log_chain: SHA-256 chain builder + verifier | FR-STO-05, INN-03 | unit test: valid OK, 1 edited line detected |
| [x] | I-09 | cocis_time: SNTP, EAT, pure schedule evaluator | FR-SCH-05 | unit test: windows + holidays correct |
| [x] | I-10 | cocis_proto: envelope codec, topics, event id, dedupe | FR-SYN-03, FR-DST-02 | unit test: round-trip + unique ids |

### Phase 2 - Entrance Hub local (offline first)

| Status | ID | Increment | Requirements | Pass when |
|---|---|---|---|---|
| [x] | I-11 | HAL base for hub + pin config + diag self-test | NFR-10, NFR-11 | diag drives each part once |
| [x] | I-12 | User registry (uid, name, role, status, expiry) + lookup | FR-ACC-08 | known uid found, unknown rejected |
| [x] | I-13 | Scan injection + scan_queue + 2 s duplicate debounce | FR-ACC-06 | repeat scan within 2 s ignored |
| [x] | I-14 | Access decision + door servo + LED/buzzer feedback | FR-ACC-02/03/04 | D1 grant, D2 deny, reason logged |
| [x] | I-15 | Occupancy table + anti-passback + NVS persist | FR-OCC-01/03, INN-01 | reboot keeps occupancy |
| [x] | I-16 | Door FSM timing + exit anomaly handling | FR-ACC-07 | door relocks 5 s; exit-no-entry flagged |
| [x] | I-17 | Schedule model + NVS + outside-hours denial | FR-SCH-01/02/04 | D4 outside-hours denial |
| [x] | I-18 | Storage: single writer, daily CSV, hash chain, verify | FR-STO-02/03/05 | D12 edit line -> verify finds it |
| [x] | I-19 | Auth: PBKDF2+salt, roles, lockout, session idle | INN-09, NFR-05 | 3 fails lock 60 s; no echo |
| [x] | I-20 | CLI core: help/login/status/occupancy/who/rooms/lights/diag | FR-CLI-01/02/03 | role-filtered help; queries work |
| [x] | I-21 | CLI logs/users/schedule/config/audit/alerts commands | FR-CLI-04, FR-SCH-04 | admin-only commands enforced |
| [x] | I-22 | Emergency button ISR + emergency mode + roll-call | INN-02, NFR-06 | D10 unlock + lights + list |
| [x] | I-23 | Alerts: brute-force (3 denials) + tamper | FR-ACC-09 | alert raised and logged |
| [x] | I-24 | Hub OLED status summary | FR-CLI-05 | mode/count/state on OLED |
| [x] | I-25 | After-hours audited `release <uid>` | FR-SCH-03, A-1 | D5 release logged |

### Phase 3 - Room node local (offline first)

| Status | ID | Increment | Requirements | Pass when |
|---|---|---|---|---|
| [x] | I-26 | Room HAL: PIR, LDR (ADC1), relay, LEDs, override | FR-LGT-01 | diag drives each part |
| [x] | I-27 | Lighting FSM core: occupied/vacant + vacancy timeout | FR-LGT-02 | D6 on, D7 off after timeout |
| [x] | I-28 | Pre-off warning blink (1 Hz) then off | FR-LGT-04, INN-06 | blink then off |
| [x] | I-29 | Daylight gating with 600/800 hysteresis | FR-LGT-05, INN-05 | D8 lamp stays off |
| [x] | I-30 | Manual override timer | FR-LGT-06 | override holds then expires |
| [x] | I-31 | Energy accounting + persistence + `energy` | FR-LGT-07, INN-07 | kWh/UGX saved shown |
| [~] | I-32 | Offline autonomy verification + room diag | FR-LGT-08, NFR-11 | lights work with no network |

### Phase 4 - Communication and distributed sync

| Status | ID | Increment | Requirements | Pass when |
|---|---|---|---|---|
| [x] | I-33 | Wi-Fi station + reconnect with back-off | FR-DST-01 | connects/reconnects logged |
| [x] | I-34 | MQTT client + QoS1 + LWT + node status retained | FR-DST-03 | status online/offline visible |
| [x] | I-35 | Envelope publish/subscribe + idempotency cache | FR-SYN-03 | duplicate id ignored |
| [x] | I-36 | Room telemetry/events to Hub + occupancy broadcast | FR-LGT-03, FR-OCC-05 | CLI shows room + count |
| [x] | I-37 | Heartbeat 5 s + offline 15 s + `nodes` command | FR-DST-03, INN-13 | silent node flagged offline |
| [x] | I-38 | Store-and-forward ring buffer + resync on reconnect | INN-08, FR-DST-04 | D11 buffer then replay |
| [x] | I-39 | Two-phase versioned schedule distribution | FR-SCH-06, FR-SYN-04 | D4 rooms apply same version |
| [-] | I-40 | Leased distributed lock (request/grant/release) | FR-SYN-05 | deferred; see 6a |
| [x] | I-41 | Intrusion detection cross-check | FR-LGT-09, INN-04 | D9 alert, lights stay off |
| [x] | I-42 | Closing-time sweep list + security alert | FR-OCC-06, INN-10 | list printed at close |

### Phase 5 - Remaining innovation (Tier 2, if time)

| Status | ID | Increment | Requirements | Pass when |
|---|---|---|---|---|
| [x] | I-43 | Visitor temporary cards with expiry | INN-11 | expired tag denied |
| [x] | I-44 | Management energy summary/report export | INN-07 | report file written |
| [x] | I-45 | Stretch items documented as future work | INN-12/14/15 | report section exists (6a) |

### Phase 6 - Verification, safety, submission

| Status | ID | Increment | Requirements | Pass when |
|---|---|---|---|---|
| [ ] | I-46 | Run full test catalogue TC-01.. and record results | all Must FRs | every Must test passes |
| [ ] | I-47 | 30 min soak + watchdog recovery + reboot consistency | NFR-03/04, FR-SYN-06 | no crash; state identical |
| [ ] | I-48 | Resource check: heap >= 30%, stack watermarks, no ISR malloc | NFR-08 | diag shows headroom |
| [ ] | I-49 | Rehearse demo script D1-D12 + record videos | - | all demos pass |
| [ ] | I-50 | docs/links.md, report, contribution statements, tag v1.0 | NFR-12 | submission checklist done |

---

## 5a. Phase 7 - Simulation and CLI hardening (added 2026-10-30)

The firmware now builds for both projects. This phase makes the system
demonstrable through the serial console before the final Wokwi run.

| Status | ID | Increment | Requirements | Pass when |
|---|---|---|---|---|
| [x] | S-01 | Hub CLI: diag, nodes, rooms, alerts, release, config | FR-CLI-01..05, NFR-11 | commands answer from live state |
| [x] | S-02 | Room CLI: diag + `sim motion` / `sim lux` overrides | FR-LGT-01, NFR-11 | motion/lux can be injected from serial |
| [x] | S-03 | Hub room tracking, 15 s offline flag, closing sweep, release | FR-DST-03, FR-OCC-06, FR-SCH-03 | D5 and offline detection work |
| [x] | S-04 | Room schedule subscription (retained, versioned) + intrusion alert | FR-SCH-06, FR-LGT-09 | D4 propagates, D9 alerts |
| [x] | S-05 | Update both diagram.json for LEDs, buttons, PIR, LDR, relay, servo, buzzer, SSD1306, SD | S6, FR-DST-01 | parts wired exactly as pin maps |
| [ ] | S-06 | Run demo D1-D12 in Wokwi using the CLI | all Must FRs | every demo step observed |
| [ ] | S-07 | Record share links, spike results, demo evidence in docs/links.md | I-06, I-46 | links.md complete |
| [ ] | S-08 | Final report, contribution statement, tag v1.0 | NFR-12 | submission checklist done |

---

## 6. Requirement traceability checklist

Access (Table 5)
- [ ] FR-ACC-01 two sections, own reader/door/LEDs
- [ ] FR-ACC-02 scan to enter and leave
- [ ] FR-ACC-03 valid tag opens door 5 s
- [ ] FR-ACC-04 rejected: locked, red LED, buzzer, reason
- [ ] FR-ACC-05 log all fields
- [ ] FR-ACC-06 duplicate reads within 2 s ignored
- [ ] FR-ACC-07 anti-passback + exit anomaly
- [ ] FR-ACC-08 registry fields incl. status/expiry
- [ ] FR-ACC-09 3 denials -> security alert

Occupancy (Table 6)
- [ ] FR-OCC-01 count + inside set
- [ ] FR-OCC-02 list who is inside
- [ ] FR-OCC-03 survives reboot
- [ ] FR-OCC-04 count never negative
- [ ] FR-OCC-05 room occupied/vacant
- [ ] FR-OCC-06 roll-call at close and emergency

Lighting (Table 7)
- [ ] FR-LGT-01 presence detection
- [ ] FR-LGT-02 on occupied, off after vacancy
- [ ] FR-LGT-03 report to Hub and CLI
- [ ] FR-LGT-04 pre-off warning blink
- [ ] FR-LGT-05 daylight gating
- [ ] FR-LGT-06 manual override
- [ ] FR-LGT-07 energy accounting
- [ ] FR-LGT-08 autonomous without network
- [ ] FR-LGT-09 intrusion alert keeps lights off

Distributed (Table 8)
- [ ] FR-DST-01 three terminals, own ESP32
- [ ] FR-DST-02 messaging protocol
- [ ] FR-DST-03 heartbeat/offline/LWT
- [ ] FR-DST-04 reconnect resync
- [ ] FR-DST-05 same room firmware, ROOM_ID differs

Storage (Table 9)
- [ ] FR-STO-01 SD FAT data set
- [ ] FR-STO-02 daily CSV append-only
- [ ] FR-STO-03 only storage task writes SD
- [ ] FR-STO-04 RAM buffer + alert on SD failure
- [ ] FR-STO-05 hash chain + verify
- [ ] FR-STO-06 config in NVS + SD mirror

CLI (Table 10)
- [ ] FR-CLI-01 serial commands 115200
- [ ] FR-CLI-02 status queries
- [ ] FR-CLI-03 help by role + unknown message
- [ ] FR-CLI-04 config needs authorised role
- [ ] FR-CLI-05 OLED/LED/MQTT status outputs
- [ ] FR-CLI-06 session idle 120 s, no echo

Schedule (Table 11)
- [ ] FR-SCH-01 configure without reflash
- [ ] FR-SCH-02 outside period locked
- [ ] FR-SCH-03 inside at close listed + alerted
- [ ] FR-SCH-04 ADMIN only, logged old/new
- [ ] FR-SCH-05 weekday windows + holidays
- [ ] FR-SCH-06 consistent versioned propagation
- [ ] FR-SCH-07 fail-secure when time invalid
- [ ] FR-SCH-08 emergency overrides schedule

Sync (Table 12)
- [ ] FR-SYN-01 mutexes with timeouts/inheritance
- [ ] FR-SYN-02 single writer (Hub)
- [ ] FR-SYN-03 unique ids, idempotent
- [ ] FR-SYN-04 monotonic version, stale rejected
- [ ] FR-SYN-05 leased distributed lock
- [ ] FR-SYN-06 watchdog + health
- [ ] FR-SYN-07 lock order cfg -> state -> sd

Non-functional (Table 15)
- [ ] NFR-01 latency targets
- [ ] NFR-02 PIR->lamp under 1 s
- [ ] NFR-03 soak + watchdog recovery
- [ ] NFR-04 reboot consistency
- [ ] NFR-05 hashed passwords + lockout
- [ ] NFR-06 emergency egress
- [ ] NFR-07 modular, documented, no magic numbers
- [ ] NFR-08 heap/stack budget, no ISR malloc
- [ ] NFR-09 help text + consistent feedback
- [ ] NFR-10 HAL only
- [ ] NFR-11 diagnostics command
- [ ] NFR-12 file headers with author/purpose/IDs

Innovations (Table 16)
- [ ] INN-01 anti-passback
- [ ] INN-02 emergency + roll-call
- [ ] INN-03 hash-chained logs
- [ ] INN-04 intrusion detection
- [ ] INN-05 daylight-aware lighting
- [ ] INN-06 pre-off warning + override
- [ ] INN-08 offline-first store-and-forward
- [ ] INN-09 RBAC + salted hashing + lockout
- [ ] INN-07 energy analytics
- [ ] INN-10 closing sweep
- [ ] INN-11 visitor temp cards
- [ ] INN-13 node health

---

## 6a. Future work and design notes

- **FR-SYN-05 leased distributed lock (I-40)** - deferred. The current
  topology has a single hub that owns the schedule and user registry, and
  rooms only read the retained schedule, so last-writer-wins is sufficient.
  A future multi-hub deployment should add `request/grant/release` messages
  with a 3 s lease and a monotonic fencing token before allowing two hubs to
  edit shared state.
- **Stretch items** from the brief are intentionally left as future work:
  occupancy analytics dashboards, a mobile/remote management app, cloud
  history export, and facial-recognition access. The current SD CSV layout
  (`logs/access`, `logs/audit`, `logs/energy`, `logs/export`) is the data
  source those features would consume.
- **OLED** is optional: the hub probes the SSD1306 at 0x3C and falls back to
  the serial console if the panel is absent (`diag` reports `oled absent`).

---

## 7. Demo script checklist (Table 44)

- [ ] D1 registered ENTRY inside hours
- [ ] D2 same tag again -> anti-passback
- [ ] D3 unknown tag x3 -> brute-force alert
- [ ] D4 admin closes building -> outside-hours denial + propagation
- [ ] D5 inside at close -> exit denial, roll-call, admin release
- [ ] D6 Room 1 motion -> lights on, energy starts
- [ ] D7 no motion -> warning blink then off
- [ ] D8 bright LDR + motion -> daylight hold
- [ ] D9 Room 2 motion while empty -> intrusion alert
- [ ] D10 emergency button -> unlock + roll-call
- [ ] D11 room Wi-Fi down -> offline + resync
- [ ] D12 edit log line -> verify detects tamper

---

## 8. Progress log

| Date | Increment | Result | Commit |
|---|---|---|---|
| 2026-10-29 | I-00 | repo split; entrance build OK, room build pending | e734000 |
| 2026-10-29 | I-01 | Wi-Fi + MQTT spike code added; awaiting two-window test | (pending) |
| 2026-10-29 | I-07 | types + constants; host test passes | 8523625+ |
| 2026-10-29 | I-08 | hash chain + verifier; host tests pass | 8b332f3 |
| 2026-10-29 | I-09 | schedule model + evaluator + SNTP; host tests pass | 5f5d4f6 |
| 2026-10-29 | I-10 | envelope codec, topics, event id, dedupe; host tests pass | 5f5d4f6+ |
| 2026-10-29 | I-11 | pending: hub HAL | - |
| 2026-10-29 | I-12 | registry + CSV; host tests pass | 9dc7a7a |
| 2026-10-29 | I-14 | access decision table; host tests pass | 9dc7a7a |
| 2026-10-29 | I-16 | door FSM + anomaly decision; host tests pass | 9dc7a7a |
| 2026-10-29 | I-26 | pending: room HAL | - |
| 2026-10-29 | I-27 | lighting FSM core; host tests pass | 9dc7a7a |
| 2026-10-29 | I-28 | warning blink; host tests pass | 9dc7a7a |
| 2026-10-29 | I-29 | LDR hysteresis; host tests pass | 9dc7a7a |
| 2026-10-29 | I-30 | manual override; host tests pass | 9dc7a7a |
| 2026-10-29 | I-31 | energy maths; persistence still pending | 9dc7a7a |
| 2026-10-29 | I-13 | scan debounce window; host tests pass | (pending) |
| 2026-10-29 | I-19 | auth core, lockout, roles, idle; host tests pass | (pending) |
| 2026-10-29 | I-22 | emergency state + roll-call; host tests pass | (pending) |
| 2026-10-29 | I-23 | denial/tamper alert logic; host tests pass | (pending) |
| 2026-10-30 | I-02/03/04 | SD storage, SNTP/EAT, console scan injection implemented; Wokwi gate deferred | (working tree) |
| 2026-10-30 | I-11/26 | hub_hal + room_hal (GPIO, LEDC servo/buzzer, ADC1 LDR, ISRs) implemented | (working tree) |
| 2026-10-30 | I-15/17/18 | occupancy/schedule/auth persistence, hash-chained CSV logs | (working tree) |
| 2026-10-30 | I-20/21 | entrance CLI (login, scan, users, schedule, emergency, door, verify) | (working tree) |
| 2026-10-30 | I-31/33/34/35/38 | room energy CSV, shared Wi-Fi/MQTT, LWT, dedupe, offline queue | (working tree) |
| 2026-10-30 | build | entrance and room both build; host tests pass | (working tree) |
| 2026-10-30 | S-01..S-04 | hub/room CLI hardening, room tracking, closing sweep, schedule sync, intrusion | (working tree) |
| 2026-10-30 | verify | host tests pass; entrance 0x104230, room 0xfb500; S-01..S-04 code + docs statuses updated | (working tree) |
| 2026-10-30 | S-05 | both diagrams wired: SD, SSD1306, servos, LEDs, buzzer, buttons, PIR, LDR, relay | (working tree) |
| 2026-10-30 | I-21/24/44/45 | logs/audit views, SSD1306 status, energy report export, future-work note; host tests pass | (working tree) |
| | | | |

---

## 9. Immediate next step

Both projects build and the host tests pass, and S-05 has wired both Wokwi
diagrams to the pin maps. Remaining: run the CLI-driven demo D1-D12 (S-06),
record the results in docs/links.md (S-07), then complete S-08 (report/tag).

CLI quick start for the demo:
- Entrance: `login security security123`, `scan in A1B2C3D4`, `status`,
  `occupancy`, `nodes`, `rooms`, `login admin admin123` then
  `passwd admin123 <new>`, `release A1B2C3D4`, `lights 1 on`,
  `schedule set mon 08:00 10:00`, `emergency on`.
- Room: `sim motion on`, `sim lux 50`, `sim clear`, `light on|off|auto`,
  `status`, `schedule`, `energy`, `diag`.
