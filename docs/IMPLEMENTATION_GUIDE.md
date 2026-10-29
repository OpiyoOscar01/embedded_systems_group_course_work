# Implementation Guide - CoCIS Smart Control and Monitoring System

Purpose: the single guided checklist we follow, feature by feature, until every
requirement is implemented and demonstrated. Each step is small enough to build,
run in Wokwi, and commit on its own so we never do a "GOD build".

Author: Opiyo Oscar (BSE IV, Makerere University)
Course: Embedded Systems
Sources of truth: docs/CoCIS_Smart_System_Requirements_and_Design_Specification,
docs/CoCIS_Smart_System_Simple_Summary, docs/ESP32_Project_Setup_Guide.
Platform: ESP32, ESP-IDF v5.2.x, Wokwi (wokwi.toml + diagram.json). No PlatformIO.

Status legend: `[ ]` not started, `[~]` in progress, `[x]` done and verified.

---

## 1. What we are building (plain language)

Three ESP32 terminals cooperate to run a pretend smart CoCIS building:

- Entrance Hub: checks RFID cards at ENTRY/EXIT, decides access, counts who is
  inside (anti-passback), owns the schedule, users, SD logs and audit trail,
  runs the serial console, emergency mode and roll-call.
- Lecture Room 1 and Room 2: read PIR motion and LDR daylight, switch lamps on
  and off locally (even if the network is down), report telemetry and alerts.
- The three talk over Wi-Fi using MQTT. The Hub is the single writer of
  authoritative data; rooms own their own sensor/lamp state.

Innovation is explicitly marked, so Tier 1 innovations are part of the core
target, not extras.

---

## 2. Target repository layout

Chosen to match the spec (Sec 9.1, Table 26, Sec 17.2). Three terminals, shared
code in `common/`, room firmware reused by Room 1 and Room 2 via `ROOM_ID`.

```
smart_control_and_monitoring_system/
|-- common/                       # shared ESP-IDF components (added via EXTRA_COMPONENT_DIRS)
|   |-- cocis_types/              # enums, reason codes, event structs, constants
|   |-- cocis_proto/              # JSON envelope, topic builders, event id
|   |-- cocis_time/               # SNTP, RTC fallback, EAT helpers, schedule eval
|   `-- cocis_log_chain/          # SHA-256 chained record builder + verifier
|-- entrance_node/                # ESP-IDF project: Hub
|   |-- CMakeLists.txt
|   |-- sdkconfig.defaults
|   |-- main/
|   |-- components/               # hal_rfid, hal_door, hal_led, hal_buzzer, hal_oled,
|   |                             # hal_rtc, hal_sd, access_ctrl, occupancy, schedule,
|   |                             # auth, cli, storage, net_sync, emergency, display
|   |-- wokwi.toml
|   `-- diagram.json
|-- room_node/                    # ESP-IDF project: Room (ROOM_ID via Kconfig)
|   |-- CMakeLists.txt
|   |-- sdkconfig.defaults
|   |-- sdkconfig.defaults.room2
|   |-- main/
|   |-- components/               # hal_pir, hal_ldr, hal_relay, hal_led, light_fsm,
|   |                             # energy, net_sync, diag
|   |-- wokwi.toml
|   `-- diagram.json
|-- docs/
|-- scripts/                      # demo sequences, helper scripts
|-- AGENTS.md
`-- README.md
```

Notes:
- `common/` holds real ESP-IDF components. Each project adds them with
  `set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../common)`.
- ROOM_ID: `CONFIG_COCIS_ROOM_ID` in Kconfig. Room 2 uses
  `sdkconfig.defaults.room2`. For side-by-side Wokwi, `room2/` may later hold
  only `wokwi.toml` + `diagram.json` pointing at the Room 2 build; decide at
  Phase 4 after the multi-project spike.
- The current root-level single project + LED smoke test will be moved into
  `entrance_node/` during P0.2 and then deleted, so the repo has one layout.

---

## 3. Pin maps (do not change without updating the requirements doc)

Hub (Table 20):
| Signal | GPIO | Notes |
|---|---|---|
| SPI SCK / MISO / MOSI | 18 / 19 / 23 | shared by SD + both RFID readers, guarded by sd_mutex |
| SD CS | 5 | idle high |
| RFID ENTRY CS | 27 | idle high |
| RFID EXIT CS | 26 | idle high |
| RFID RST (shared) | 25 | |
| I2C SDA / SCL | 21 / 22 | OLED 0x3C, RTC 0x68 |
| Servo ENTRY / EXIT | 13 / 14 | LEDC PWM 50 Hz, 1 ms locked / 2 ms open |
| LED ENTRY green / red | 32 / 33 | |
| LED EXIT green / red | 16 / 17 | |
| Buzzer | 4 | LEDC tone |
| Emergency button | 34 | input only, external pull-up, ISR falling |
| Tamper switch | 39 | input only, external pull-up, ISR |
| Passage sensor (INN-12) | 35 | optional, input only |
| UART0 (CLI) | 1 / 3 | 115200 8N1 |

Room (Table 21, identical on Room 1 and Room 2):
| Signal | GPIO | Notes |
|---|---|---|
| PIR output | 13 | input, HC-SR501 retrigger, 3.3 V |
| LDR analog | 34 | ADC1 channel 6 (never ADC2 with Wi-Fi) |
| Manual override button | 14 | input, pull-up, active low, debounced |
| Relay IN (lamp) | 26 | output, active high in Wokwi |
| LED Occupied (green) | 25 | output |
| LED Lights ON (yellow) | 33 | output |
| LED Network (blue) | 32 | blink while connecting, steady when linked |
| I2C SDA / SCL | 21 / 22 | optional OLED |

Avoid strapping pins (0, 2, 12, 15) for outputs that affect boot. Never use
GPIO 6-11 (flash).

---

## 4. Working agreement (no GOD builds)

For every step in this guide:
1. Create a short-lived branch: `feat/<area>` or `fix/<area>`.
2. Implement only that step.
3. Build locally: `idf.py build` (from the project folder).
4. Run the Wokwi scenario that proves the step. Record the result in the
   progress log below.
5. Commit with a requirement-citing message, for example:
   `feat(access): ENTRY/EXIT decision + occupancy [FR-ACC-03,FR-OCC-01]`.
6. Push the branch, open a PR, review, merge. Keep `main` always buildable.

Definition of done for a step: code merged, requirement ID in the file header
comment, the step's acceptance check passed in Wokwi, and we can explain it.

GitHub note: the earlier push failed with `403` because the local credential
was a different account (`Custospark`). Fix auth (`gh auth login` or a personal
access token for OpiyoOscar01) before the first push of this phase.

---

## 5. Roadmap at a glance

| Phase | Theme | Key requirement IDs | Exit condition |
|---|---|---|---|
| P0 | Spikes + repo restructure | S1-S6 | deployment option decided, 3 projects build |
| P1 | Shared common components | FR-SYN-03, FR-STO-05 | codec, time, hash chain unit-tested |
| P2 | Hub local core | FR-ACC-*, FR-OCC-*, FR-STO-*, FR-CLI-*, FR-SCH-* | doors work offline, logs chain-verify |
| P3 | Room node local | FR-LGT-*, INN-05/06/07 | lighting FSM works offline |
| P4 | Comms + sync | FR-DST-*, FR-SYN-*, INN-08 | 3 nodes in step, buffering works |
| P5 | Innovations | INN-01..06, 08..10, 13 | Tier 1 all demonstrated |
| P6 | Test + harden + submit | TC-*, NFR-*, D1-D12 | acceptance criteria met, docs ready |

---

## 6. Phase 0 - Spikes and foundation

### P0.1 Record decisions in docs/links.md
- [ ] Create `docs/links.md` for Wokwi share links and spike results.
- Acceptance: file exists with a table of spikes S1-S6.

### P0.2 Restructure the repo
- [ ] Create `common/`, `entrance_node/`, `room_node/` with the tree above.
- [ ] Move the working LED smoke test into `entrance_node/main` (keep it green).
- [ ] Add `set(EXTRA_COMPONENT_DIRS .../../common)` to both project CMakeLists.
- [ ] `idf.py set-target esp32 && idf.py build` in `entrance_node` and
  `room_node`; both must build.
- Acceptance: both project folders build; old root `main/` removed.
- Commit: `chore(repo): split into common + entrance_node + room_node`.

### P0.3 Spike S1 - ESP-IDF + Wokwi (already proven)
- [x] Hello world runs in Wokwi over serial.
- Record in links.md.

### P0.4 Spike S2 - MQTT between two Wokwi projects (DECIDES deployment)
- [ ] Two tiny firmwares connect to Wokwi-GUEST Wi-Fi, publish/subscribe on a
  test topic through a public broker (e.g. broker.hivemq.com) with a unique
  group prefix.
- [ ] Prove message A -> B arrives.
- Acceptance: payload received on B. If it fails, switch to deployment Option B
  (single project, in-memory bus mirroring MQTT topics).
- Record the group prefix and broker in links.md and Kconfig.
- Commit: `spike(net): two-project MQTT over Wokwi-GUEST [S2]`.

### P0.5 Spike S3 - SNTP time
- [ ] Obtain UTC via SNTP, set TZ `EAT-3`, print local time.
- Fallback: DS3231 RTC manual-set command.
- Commit: `spike(time): SNTP + EAT conversion [S3]`.

### P0.6 Spike S4 - SD card FATFS
- [ ] Mount `wokwi-microsd-card` with FATFS, create folders, append a line,
  read it back.
- Acceptance: file written and read back. If mount fails, fall back to NVS +
  RAM ring buffer and document it.
- Commit: `spike(storage): SD mount + append/read [S4]`.

### P0.7 Spike S5 - RFID source (DECIDES HAL)
- [ ] Decide the RFID source: existing Wokwi part, custom chip, or console
  injection via `sim scan <entry|exit> <uid>`.
- Baseline (per spec): console injection behind `rfid_read_uid(reader_id)` HAL;
  keep an MFRC522 SPI implementation selectable in Kconfig.
- Commit: `spike(rfid): simulated scan HAL [S5]`.

### P0.8 Spike S6 - Peripheral parts
- [ ] Verify servo (LEDC), PIR, LDR (ADC1), relay, OLED (I2C), RTC, buzzer.
- Note any part that must be substituted (LED/label) in links.md.
- Commit: `spike(hw): servo/PIR/LDR/relay/OLED/RTC checks [S6]`.

---

## 7. Phase 1 - Shared common components

### P1.1 cocis_types
- [ ] Enums: direction (ENTRY/EXIT), result codes (Table 31), roles
  (VIEWER/SECURITY/ADMIN), operating modes (NORMAL/AFTER_HOURS/EMERGENCY/
  DEGRADED/MAINTENANCE), room ids.
- [ ] Structs: access_event, telemetry, occupancy snapshot, schedule, message
  envelope. Constants from Appendix C.
- Acceptance: both projects compile against the header; no magic numbers in apps.
- Commit: `feat(common): types, reason codes, constants [FR-ACC-05]`.

### P1.2 cocis_time
- [ ] SNTP init, TZ EAT-3, `now_utc()`, UTC<->local helpers.
- [ ] Pure `schedule_is_open(day, hh:mm, schedule)` with holidays and per-day
  windows.
- [ ] Host-testable: a small test main / Unity test for the schedule evaluator.
- Commit: `feat(common): time + schedule evaluator [FR-SCH-01,FR-SCH-05]`.

### P1.3 cocis_proto
- [ ] Envelope encode/decode: `{v,id,ts,src,type,data}`.
- [ ] Topic builders for all Table 33 topics with group prefix from Kconfig.
- [ ] Event id generator `node-boot-seq`; processed-id cache helper (idempotency).
- [ ] Pure JSON codec with unit tests.
- Commit: `feat(common): protocol codec + topics [FR-SYN-03,FR-DST-02]`.

### P1.4 cocis_log_chain
- [ ] `hash_i = SHA256(hash_(i-1) || canonical_record_i)` using mbedtls.
- [ ] Record builder + `verify_chain()` returning first broken line.
- [ ] Unit tests: valid chain OK; one edited line detected.
- Commit: `feat(common): SHA-256 log chain + verifier [FR-STO-05,INN-03]`.

---

## 8. Phase 2 - Entrance Hub local core

Build this fully offline first (no MQTT). Then P4 adds the network.

### P2.1 HAL layer
- [ ] hal_rfid (simulated console source + MFRC522 stub), hal_door (servo
  LEDC), hal_led, hal_buzzer, hal_oled, hal_rtc, hal_sd.
- [ ] Every HAL returns esp_err_t; no app code touches GPIO directly.
- Acceptance: a `diag`-style self-test drives each part.
- Commit: `feat(hub): HAL drivers [NFR-10]`.

### P2.2 Access decision + door FSM
- [ ] scan_queue (depth 8), access_ctrl_task (prio 6), door_task (prio 5).
- [ ] Decision order: valid uid -> registry status -> hours -> anti-passback ->
  duplicate window -> actuate 5 s -> feedback table (Table 23).
- [ ] Result codes: GRANTED, DENY_UNKNOWN, DENY_SUSPENDED/EXPIRED,
  DENY_OUTSIDE_HOURS, DENY_EXIT_OUTSIDE_HOURS, DENY_ANTIPASSBACK,
  ANOMALY_EXIT_NO_ENTRY, EMERGENCY_PASS.
- Covers FR-ACC-01..07, FR-SCH-02/03.
- Acceptance: D1/D2/D3 basics; door opens 5 s; red/green + buzzer correct.
- Commit: `feat(access): decision + door FSM [FR-ACC-03,FR-ACC-04]`.

### P2.3 Occupancy + anti-passback + persistence
- [ ] state_mutex-protected table, inside flag, entered_at, last_scan.
- [ ] Count never negative; snapshot to NVS on change, SD every 60 s.
- [ ] Boot reload: NVS -> SD snapshot -> reconcile with last log lines.
- [ ] Duplicate scan window 2 s.
- Covers FR-OCC-01..04, FR-ACC-06/07, INN-01.
- Acceptance: reboot preserves occupancy.
- Commit: `feat(occupancy): table + anti-passback + NVS [FR-OCC-03,INN-01]`.

### P2.4 Storage (single writer + hash chain)
- [ ] storage_task sole SD writer, log_queue depth 32.
- [ ] Daily CSV: logs/access, logs/audit, logs/energy; config/, state/.
- [ ] Chain head in NVS `sys/last_hash`; append-only; fflush each record,
  fsync every 10 s.
- [ ] RAM ring buffer + SD_OK bit when card missing (DEGRADED).
- Covers FR-STO-01..06, INN-03.
- Acceptance: D12 edit-a-line then `log verify` reports first broken line.
- Commit: `feat(storage): single-writer CSV + hash chain [FR-STO-03,FR-STO-05]`.

### P2.5 Schedule + settings in NVS
- [ ] Schedule model (per weekday + holidays), version counter, NVS `sched`.
- [ ] Settings in `cfg` namespace with Appendix C defaults.
- Covers FR-SCH-01/02/04/05/07.
- Acceptance: change schedule persists across reboot; fail-secure when time
  invalid.
- Commit: `feat(schedule): model + NVS persistence [FR-SCH-01,FR-SCH-04]`.

### P2.6 Auth + RBAC + CLI
- [ ] CLI over UART0 115200; login/logout/whoami, no echo, session idle 120 s.
- [ ] PBKDF2-HMAC-SHA256 + per-user salt; lockout 3 fails / 60 s.
- [ ] Command dispatcher with role checks for every command in Table 42.
- [ ] `help` lists only permitted commands; unknown commands helpful.
- Covers FR-CLI-01..06, INN-09, NFR-05.
- Acceptance: viewer cannot run admin commands; login/change logged to audit.
- Commit: `feat(cli): auth + RBAC + command dispatcher [FR-CLI-04,INN-09]`.

### P2.7 Emergency mode + roll-call
- [ ] Emergency button ISR (GPIO34) + CLI `emergency on|off`.
- [ ] Event group EMERGENCY bit; highest-priority path cannot be starved.
- [ ] Unlock both doors, lights on, log EMERGENCY_PASS, print roll-call.
- Covers INN-02, FR-SCH-08, NFR-06.
- Acceptance: D10.
- Commit: `feat(emergency): unlock + roll-call [INN-02]`.

### P2.8 Alerts
- [ ] ALERT_BRUTE (3 denials same uid), ALERT_TAMPER, ALERT_INTRUSION hooks.
- [ ] Feedback per Table 23; audit entries.
- Covers FR-ACC-09, INN-04 (hook).
- Commit: `feat(alerts): brute-force + tamper [FR-ACC-09]`.

---

## 9. Phase 3 - Room node local

### P3.1 HAL: PIR, LDR, relay, LEDs
- [ ] hal_pir (GPIO13), hal_ldr (ADC1 ch6), hal_relay (GPIO26), hal_led.
- [ ] Debounce PIR hold time 2 s (demo 1 s); LDR 2 Hz averaging.
- Commit: `feat(room): sensor HAL [FR-LGT-01]`.

### P3.2 Lighting FSM
- [ ] States: OFF, ON, WARNING, DAYLIGHT_HOLD, OVERRIDE, INTRUSION_HOLD.
- [ ] Vacancy 300 s (demo 20 s), warning 30 s (demo 5 s), 1 Hz blink.
- [ ] LDR hysteresis 600/800 ADC; override button 60 min (demo 30 s).
- [ ] Intrusion: motion while building empty/closed -> alert, lights stay off.
- Covers FR-LGT-01..09, INN-04/05/06.
- Acceptance: D6/D7/D8.
- Commit: `feat(light): FSM with daylight + warning + override [FR-LGT-02,INN-05]`.

### P3.3 Energy accounting
- [ ] Accumulate lamp-on seconds; est_wh_used / est_wh_saved vs always-on.
- [ ] Persist every 5 min; expose via telemetry and CLI `energy`.
- Covers INN-07, FR-LGT-07.
- Commit: `feat(energy): counters + savings estimate [INN-07]`.

### P3.4 Offline autonomy + diag
- [ ] Lighting must keep working with Wi-Fi/broker down (local only).
- [ ] diag_task prints heap, stack watermarks, queue depth, uptime.
- Covers FR-LGT-08, NFR-11.
- Commit: `feat(room): offline autonomy + diagnostics [FR-LGT-08]`.

---

## 10. Phase 4 - Comms and distributed sync

### P4.1 Wi-Fi + MQTT client
- [ ] Station mode to Wokwi-GUEST, reconnect with back-off, LWT on node status.
- [ ] QoS 1; retained where Table 33 says retained.
- Covers FR-DST-01..02, NFR.
- Commit: `feat(net): Wi-Fi + MQTT + LWT [FR-DST-02]`.

### P4.2 Topics + envelope wiring
- [ ] Publish/subscribe per Table 33; validate envelope; dedupe by id.
- [ ] UIDs sent as short hashes; names never leave the Hub.
- Covers FR-DST-02, FR-SYN-03.
- Commit: `feat(net): topic wiring + idempotency [FR-SYN-03]`.

### P4.3 Heartbeat + offline detection
- [ ] 5 s heartbeat, 15 s offline threshold, node status retained.
- Covers FR-DST-03, INN-13.
- Commit: `feat(net): heartbeat + offline detection [FR-DST-03]`.

### P4.4 Store-and-forward + resync
- [ ] Bounded ring buffer (64) for unsent messages; replay in order on reconnect.
- [ ] Reconnect fetches retained config, uploads buffered events.
- Covers INN-08, FR-DST-04.
- Acceptance: D11.
- Commit: `feat(net): store-and-forward + resync [INN-08]`.

### P4.5 Two-phase schedule distribution
- [ ] `schedule/prepare` -> room `ack` -> `commit` (retained), versioned,
  stale base rejected.
- Covers FR-SCH-06, FR-SYN-04, FR-DST.
- Acceptance: D4.
- Commit: `feat(sync): versioned two-phase schedule [FR-SCH-06]`.

### P4.6 Distributed lock
- [ ] request/grant/release with 3 s lease auto-expire.
- Covers FR-SYN-05.
- Commit: `feat(sync): leased distributed lock [FR-SYN-05]`.

---

## 11. Phase 5 - Innovations that remain

- [ ] INN-04 Intrusion cross-check (Hub compares motion vs occupancy/schedule).
  Commit: `feat(innovation): distributed intrusion detection [INN-04]`.
- [ ] INN-10 Closing-time sweep: list and alert people still inside.
  Commit: `feat(innovation): closing sweep [INN-10]`.
- [ ] INN-13 Node health in CLI `nodes` + diagnostics.
  Commit: `feat(innovation): node health [INN-13]`.
- [ ] INN-11 Visitor temporary cards with expiry (Tier 2, if time).
- [ ] INN-12/14/15 only as stretch, document as future work.

---

## 12. Phase 6 - Testing, hardening, submission

### P6.1 Test catalogue (Table 45)
- [ ] TC-01..TC-20+ written and passing; each cites its requirement.
- [ ] Unit tests for pure logic (schedule, hash chain, FSM step, codec).

### P6.2 Reliability and safety
- [ ] 30 min soak with random scans (NFR-03), reboot consistency (NFR-04).
- [ ] Task watchdog 10 s recovers stuck tasks (FR-SYN-06).
- [ ] Heap >= 30% free steady state; stack watermarks checked (NFR-08).

### P6.3 Docs and demo
- [ ] docs/links.md with three Wokwi links; demo sequence in scripts/.
- [ ] Demo script D1-D12 rehearsed.
- [ ] Report, individual videos, contribution statements, member details.
- [ ] Final push and tag `v1.0`.

---

## 13. Requirement traceability checklist

Access (Table 5)
- [ ] FR-ACC-01 two sections, own reader/door/LEDs/feedback
- [ ] FR-ACC-02 scan to enter and leave
- [ ] FR-ACC-03 valid tag opens door 5 s
- [ ] FR-ACC-04 rejected tag: locked, red LED, buzzer, reason logged
- [ ] FR-ACC-05 log timestamp, UID, name, direction, result, reason
- [ ] FR-ACC-06 duplicate reads within 2 s ignored
- [ ] FR-ACC-07 anti-passback + exit anomaly
- [ ] FR-ACC-08 registry fields incl. status/expiry
- [ ] FR-ACC-09 3 denials -> security alert

Occupancy (Table 6)
- [ ] FR-OCC-01 count + inside set
- [ ] FR-OCC-02 list who is inside
- [ ] FR-OCC-03 survives reboot
- [ ] FR-OCC-04 count never negative
- [ ] FR-OCC-05 room occupied/vacant status
- [ ] FR-OCC-06 roll-call at closing and emergency

Lighting (Table 7)
- [ ] FR-LGT-01 presence detection
- [ ] FR-LGT-02 on when occupied, off after vacancy timeout
- [ ] FR-LGT-03 report to Hub and CLI
- [ ] FR-LGT-04 pre-off warning blink
- [ ] FR-LGT-05 daylight gating
- [ ] FR-LGT-06 manual override
- [ ] FR-LGT-07 energy accounting
- [ ] FR-LGT-08 autonomous without Hub/network
- [ ] FR-LGT-09 intrusion alert keeps lights off

Distributed (Table 8)
- [ ] FR-DST-01 three terminals, own ESP32
- [ ] FR-DST-02 messaging protocol
- [ ] FR-DST-03 heartbeat 5 s / offline 15 s / LWT
- [ ] FR-DST-04 reconnect resync
- [ ] FR-DST-05 same room firmware, ROOM_ID differs

Storage (Table 9)
- [ ] FR-STO-01 SD FAT: access, occupancy, energy, users, schedule
- [ ] FR-STO-02 CSV per day, append-only, periodic flush
- [ ] FR-STO-03 only storage_task writes SD
- [ ] FR-STO-04 RAM ring buffer + alert on SD failure
- [ ] FR-STO-05 hash chained + verify
- [ ] FR-STO-06 config in NVS + SD mirror

CLI (Table 10)
- [ ] FR-CLI-01 serial commands 115200
- [ ] FR-CLI-02 access/occupancy/room/lighting/system queries
- [ ] FR-CLI-03 help by role, unknown-command message
- [ ] FR-CLI-04 config requires authorised role
- [ ] FR-CLI-05 OLED, LEDs, MQTT status outputs
- [ ] FR-CLI-06 session idle 120 s, no password echo

Schedule (Table 11)
- [ ] FR-SCH-01 configure period without reflash
- [ ] FR-SCH-02 outside period doors locked
- [ ] FR-SCH-03 people inside at close listed + security alerted
- [ ] FR-SCH-04 ADMIN only, change logged old/new
- [ ] FR-SCH-05 per-weekday windows + holidays
- [ ] FR-SCH-06 consistent versioned propagation
- [ ] FR-SCH-07 fail-secure when time invalid
- [ ] FR-SCH-08 emergency overrides schedule

Sync (Table 12)
- [ ] FR-SYN-01 mutexes with timeouts/priority inheritance
- [ ] FR-SYN-02 single writer (Hub)
- [ ] FR-SYN-03 unique event IDs, idempotent
- [ ] FR-SYN-04 monotonic config version, stale rejected
- [ ] FR-SYN-05 leased distributed lock
- [ ] FR-SYN-06 watchdog + health supervision
- [ ] FR-SYN-07 fixed lock order cfg -> state -> sd

Non-functional (Table 15)
- [ ] NFR-01 latency targets
- [ ] NFR-02 PIR->lamp under 1 s
- [ ] NFR-03 soak test, watchdog recovery
- [ ] NFR-04 reboot consistency
- [ ] NFR-05 no plaintext passwords, salted hash, lockout
- [ ] NFR-06 emergency egress always possible
- [ ] NFR-07 modular, documented headers, no magic numbers
- [ ] NFR-08 heap/stack budget, no alloc in ISR
- [ ] NFR-09 help text, consistent LED/buzzer
- [ ] NFR-10 HAL only
- [ ] NFR-11 diagnostics command
- [ ] NFR-12 file header with author/purpose/requirement IDs

Innovations (Table 16)
- [ ] INN-01 anti-passback
- [ ] INN-02 emergency + roll-call
- [ ] INN-03 hash-chained logs
- [ ] INN-04 intrusion detection
- [ ] INN-05 daylight-aware lighting
- [ ] INN-06 pre-off warning + override
- [ ] INN-08 offline-first store-and-forward
- [ ] INN-09 RBAC + salted hashing + lockout
- [ ] INN-07 energy analytics (Tier 2)
- [ ] INN-10 closing sweep (Tier 2)
- [ ] INN-11 visitor temp cards (Tier 2)
- [ ] INN-13 node health (Tier 2)

---

## 14. Demo script checklist (Table 44)

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

## 15. Progress log

Keep entries short: date, step ID, what passed, commit hash.

| Date | Step | Result | Commit |
|---|---|---|---|
| 2026-10-29 | Setup + LED smoke test | green/red alternate in Wokwi | initial |
| | | | |

---

## 16. Immediate next actions

1. Fix GitHub auth so pushes work.
2. P0.2 restructure into `common/`, `entrance_node/`, `room_node/`.
3. P0.4 MQTT spike (this decides Option A vs B) and P0.6 SD spike.
4. Start P1.1 `cocis_types`.

Each action ends with a build, a Wokwi check, and a commit.
