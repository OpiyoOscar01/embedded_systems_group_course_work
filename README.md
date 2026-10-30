# CoCIS Smart Control and Monitoring System

Embedded Systems coursework: a three-terminal smart building for the CoCIS
building, built with **ESP-IDF v5.2.x**, **FreeRTOS** and **Wokwi** (no
PlatformIO).

- **Entrance Hub** - RFID-style access control (ENTRY/EXIT), anti-passback,
  occupancy, opening-hours enforcement, emergency mode with roll-call,
  tamper-evident SD logging, MQTT sync and a role-based serial console.
- **Lecture Room 1 / Room 2** - PIR + LDR lighting automation, pre-off warning,
  manual override, daylight gating, energy accounting, room telemetry and an
  offline-first local control loop.

Rooms 1 and 2 run the **same firmware**; only `CONFIG_COCIS_ROOM_ID` differs.

## Repository layout

```
smart_control_and_monitoring_system/
├── common/                  shared ESP-IDF components
│   ├── cocis_types/         enums, result codes, constants
│   ├── cocis_proto/         JSON envelope, topics, event ids, dedupe
│   ├── cocis_time/          SNTP/EAT clock + schedule + schedule JSON
│   ├── cocis_log_chain/     SHA-256 hash-chain builder/verifier
│   ├── cocis_net/           Wi-Fi station + MQTT + offline queue
│   └── storage/             SD card (SDSPI/FATFS) single-writer helpers
├── entrance_node/           ESP-IDF project: Entrance Hub
│   ├── main/                app.c, cli.c, main.c, Kconfig.projbuild
│   ├── components/          hub_hal, log_store, state_store, auth,
│   │                        access_ctrl, occupancy, door_fsm, emergency, alerts
│   ├── diagram.json         Wokwi circuit
│   ├── wokwi.toml
│   └── partitions.csv
├── room_node/               ESP-IDF project: Room node
│   ├── main/                app.c, cli.c, main.c, Kconfig.projbuild
│   ├── components/          room_hal, light_fsm, energy
│   ├── diagram.json
│   ├── wokwi.toml
│   ├── partitions.csv
│   └── sdkconfig.defaults.room2
├── tests/host/              host unit tests (no hardware)
└── docs/                    requirements, implementation guide, todo, links
```

## Prerequisites

1. **ESP-IDF v5.2.x** installed and exported in your shell
   (`idf.py --version` should print `v5.2.x`).
   - Windows: ESP-IDF Tools Installer or the VS Code ESP-IDF extension.
   - Linux/macOS: `git clone -b v5.2.3 --recursive https://github.com/espressif/esp-idf.git`,
     then `./install.sh esp32` and `. ./export.sh`.
2. **Python 3** and the ESP-IDF Python environment (installed by ESP-IDF).
3. **VS Code** with:
   - the **ESP-IDF** extension (optional but convenient), and
   - the **Wokwi for VS Code** extension (sign in; simulation may require a
     Wokwi license).
4. A C compiler for the host tests (`gcc`/`clang`) and `bash` (Git Bash on
   Windows is fine).

No physical hardware is required: the demo uses the Wokwi circuit and the
serial console.

## Clone

```bash
git clone https://github.com/OpiyoOscar01/embedded_systems_group_course_work.git
cd embedded_systems_group_course_work
```

## Build

Each terminal is its own ESP-IDF project. Open a terminal with ESP-IDF
exported, then:

```bash
# Entrance Hub
cd entrance_node
idf.py set-target esp32        # first time only
idf.py build

# Room node (Room 1 by default)
cd ../room_node
idf.py set-target esp32        # first time only
idf.py build
```

The Wokwi config expects:

- `entrance_node/build/entrance_node.bin` + `.elf`
- `room_node/build/room_node.bin` + `.elf`

If Kconfig or component layout changes, run `idf.py reconfigure` before
building. If you previously built with a different ESP-IDF version, delete
`build/` and `sdkconfig` and build again (both are regenerated).

### Room 2 firmware (separate workspace)

Room 2 uses the same source with a different room id, but its Wokwi config
loads `build/room_node.bin` from its own folder. Make a second copy of the
project so Room 1 and Room 2 can run at the same time:

```bash
# from the repository root
cp -r room_node room_node_room2
cd room_node_room2
rm -rf build sdkconfig
cp sdkconfig.defaults.room2 sdkconfig.defaults
idf.py set-target esp32
idf.py build
```

`room_node_room2` is only a local simulation copy; do not commit it. The
`../common` relative path still resolves because the copy is inside the
repository root.

## Run in Wokwi (three terminals)

You need **three Wokwi simulations**, each in its **own VS Code window**:

| Instance | Folder to open | Node |
|---|---|---|
| 1 | `entrance_node` | Entrance Hub |
| 2 | `room_node` | Room 1 |
| 3 | `room_node_room2` | Room 2 |

For each window:

1. Build that folder first (Wokwi loads the `.bin` from `build/`).
2. Open the project's `diagram.json`.
3. Press **F1 -> Wokwi: Start Simulator**.
4. Click the serial monitor at the bottom and type commands directly there.
   Use `help` to list commands.

Both diagrams are already wired to the pin maps in
`docs/IMPLEMENTATION_GUIDE.md`: SD card, SSD1306 status display, servos,
LEDs, buzzer, buttons, PIR, LDR and relay.

### What you should see

**Entrance Hub serial monitor**

```text
CoCIS Entrance Hub console. Type `help`.
HUB>
```

Before that you should see ESP-IDF boot logs including `SD card ...`,
`hub HAL ready (...)`, Wi-Fi `got ip`, MQTT `connected`, `entrance hub
ready` and `console ready`. The OLED shows `COCIS HUB`, `MODE NORMAL`,
`IN 0  NET ...` and `SD ...`.

**Room 1 / Room 2 serial monitors**

```text
CoCIS Room 1 console. Type `help`.
R1>
```

```text
CoCIS Room 2 console. Type `help`.
R2>
```

Rooms boot with the relay off, connect to Wi-Fi/MQTT, apply the retained
schedule from the hub and publish telemetry about every 5 s. `nodes` on the
hub should list `R1` and `R2` as online; after 15 s without telemetry they
show offline.

**All three running together**

- `scan in A1B2C3D4` at the hub opens the entry door (servo), lights the green
  LED, beeps, increments occupancy and publishes an `access` event.
- The second `scan in A1B2C3D4` is denied by anti-passback (red LED, longer
  beep). `scan out A1B2C3D4` releases it.
- `sim motion on` then `sim lux 50` on a room turns that room's lamp on.
  `sim lux 900` keeps it off (daylight); `sim clear` returns to real sensors.
- `lights 1 on` on the hub publishes a command; Room 1 logs the command and
  turns its lamp on.
- `nodes`, `rooms`, `alerts`, `logs` on the hub show the live system state.
- Pressing the red emergency button on the hub latches emergency mode, opens
  both doors and captures a roll-call. Clear it with `emergency off` (login
  as admin/security first).

If the current time is outside opening hours (Mon-Fri 07:00-21:00, Sat
08:00-14:00, Sun closed), access is denied and room lamps stay off. Check
with `schedule`, force a lamp with `light on`, or set a test window with
`schedule set mon 08:00 23:59` (admin).

Wokwi parts are clickable (buttons, sensors), but the CLI commands above are
the deterministic way to reproduce the demo.

## Configuration

Networking defaults are in each project's `main/Kconfig.projbuild` and can be
changed with `idf.py menuconfig`:

| Setting | Default |
|---|---|
| Wi-Fi SSID | `Wokwi-GUEST` |
| Wi-Fi password | empty (open network) |
| MQTT broker | `mqtt://broker.hivemq.com:1883` |
| Group prefix | `cocis26-oscar-a91f` |

All three terminals must use the **same group prefix**. Change it for your own
team so you do not collide with other groups on the public broker.

## Interacting with the system (CLI)

There are no physical RFID readers in the simulation. Tag scans are injected
through the **hub serial console**, which drives the same access-control path
as a real reader.

### How to type commands

1. Start the simulation and wait for the prompt (`HUB>`, `R1>` or `R2>`).
2. Click inside the Wokwi serial monitor, type a command and press **Enter**.
3. ESP-IDF log lines and command output share the monitor; this is normal.
4. Type `help` to see the commands for the current node.

Commands are line-based and case-insensitive for the command name. Empty
lines are ignored.

### Hub login and roles

Default accounts are created on first boot:

| User | Password | Role | Notes |
|---|---|---|---|
| `admin` | `admin123` | ADMIN | Must change password at first login |
| `security` | `security123` | SECURITY | Access and security operations |
| `viewer` | `viewer123` | VIEWER | Read-only status |

```text
login security security123
whoami
```

Sessions idle out after 120 s; log in again if a command says
`Not authorised`. Three bad passwords lock the account for 60 s. The admin
must run `passwd admin123 <new-password>` before other admin commands work.

### Entrance Hub commands

| Area | Commands | Minimum role |
|---|---|---|
| Session | `help`, `login`, `logout`, `passwd`, `whoami` | none / logged in |
| Read-only | `status`, `users`, `occupancy`, `nodes`, `rooms`, `schedule`, `diag`, `net` | VIEWER |
| Security | `scan in\|out <uid>`, `alerts`, `logs [YYYYMMDD]`, `audit [YYYYMMDD]`, `verify`, `rollcall`, `emergency on\|off` | SECURITY |
| Room control | `lights 1\|2\|all on\|off\|auto` | SECURITY |
| Administration | `adduser`, `deluser`, `suspend`, `resume`, `schedule set\|close`, `holiday`, `config`, `door entry\|exit`, `release <uid>`, `reboot` | ADMIN |

Useful details:

- `scan in A1B2C3D4` = entry attempt; `scan out <uid>` = exit attempt.
- `occupancy` lists who is inside; `rollcall` lists the emergency roll-call.
- `logs` / `audit` show the last 20 rows; pass a date to read another day.
- `verify` checks today's access-log hash chain and reports tampering.
- `nodes` / `rooms` show room link, schedule version and sensor state.
- `lights` publishes an MQTT command; the room applies it locally.

### Room node commands

Rooms have no login: the local console is trusted.

| Command | Purpose |
|---|---|
| `help` | List commands |
| `status` | Motion, lux, schedule, relay, override, network, SD |
| `light on\|off\|auto` | Force or release the lamp |
| `override [minutes]` | Manual override; default 60 minutes |
| `sim motion on\|off\|clear` | Inject PIR state |
| `sim lux <value>` / `sim clear` | Inject LDR lux / return to real sensors |
| `schedule` | Show the synced opening hours and current state |
| `energy` | Lamp-on time and energy used/saved/cost |
| `export` | Write `logs/export/energy_summary.csv` |
| `diag`, `net` | GPIO/ADC/button/heap self-test and connectivity |

### Expected behaviour and timings

| Event | Expected result |
|---|---|
| Registered tag IN during opening hours | Green LED, short beep, door servo opens for 5 s, occupancy +1, access row logged |
| Same tag IN again | Anti-passback denial, red LED, long beep |
| Unknown tag 3 times | Brute-force alert + audit entry |
| Tag OUT | Green LED, exit door opens, occupancy -1, access row logged |
| Outside opening hours | Denial; rooms keep lamps off |
| Room motion + dark (lux < 600) | Relay/lamp on immediately |
| No motion for 5 min | 30 s warning blink, then lamp off |
| Lux > 800 | Daylight hold; lamp off even when occupied |
| Override button / `override` | Lamp on for 60 min by default |
| `sim motion on` while room closed | Intrusion alert published to the hub |
| Emergency button / `emergency on` | Both doors release, roll-call captured, alert published |
| Room heartbeat | Every 5 s; hub marks a room offline after 15 s |
| Hub closes with occupants inside | Closing sweep alert + audit list of UIDs |

Opening hours default to Mon-Fri 07:00-21:00, Sat 08:00-14:00, Sun closed,
evaluated in EAT after SNTP sync. If you demo outside these hours, check
`schedule`, use `light on` for a room, or set a test window as admin:
`schedule set mon 08:00 23:59`.

### Simulation controls

- **Buttons**: click (or click and hold) the Wokwi pushbuttons. The hub has
  emergency and tamper buttons; each room has an override button.
- **Sensors**: the CLI `sim` commands are the deterministic way to drive PIR
  and LDR; `sim clear` returns control to the real Wokwi parts.
- **Outputs**: LEDs, servo, buzzer, OLED, relay and SD card all update live.
- **Network**: `net` shows Wi-Fi/MQTT state. If MQTT is down the hub and rooms
  keep working locally; `nodes` shows rooms offline and rooms resync when the
  connection returns.

### End-to-end demo script (D1-D12)

Run these in order with all three terminals online:

| # | Action | Expected |
|---|---|---|
| D1 | `login security security123` then `scan in A1B2C3D4` | Access granted, occupancy 1 |
| D2 | `scan in A1B2C3D4` again | Anti-passback denial |
| D3 | `scan in BAD00001` three times | Brute-force alert in `alerts` |
| D4 | Admin runs `schedule close <today>`, then `scan in A1B2C3D4` | Outside-hours denial and rooms suppress lamps; restore with `schedule set <today> 00:00 23:59` |
| D5 | Scan someone in, then admin runs `schedule close <today>` | Closing sweep alert + roll-call; `release <uid>` frees them; restore the window afterwards |
| D6 | Room 1: `sim motion on`, `sim lux 50` | Lamp on, energy starts counting |
| D7 | Room 1: `sim motion off`, wait 5 min | 30 s blink then lamp off |
| D8 | Room 1: `sim lux 900` with motion | Daylight hold, lamp stays off |
| D9 | Room 2 closed + `sim motion on` | Intrusion alert at the hub |
| D10 | Press hub emergency button | Both doors release, roll-call captured |
| D11 | Stop Wi-Fi/MQTT (or power one room) | Hub shows room offline; local control continues |
| D12 | Edit an access CSV row, then `verify` | Tamper detected |

Restore normal operation with `schedule set mon 08:00 21:00`,
`emergency off`, and `sim clear` on the rooms.

## Host tests

Pure logic (schedules, protocol, auth, access decisions, FSMs, energy, etc.)
is testable without hardware:

```bash
bash tests/host/run_tests.sh
```

Expected output: `ALL TESTS PASSED`.

Add new `test_*.c` files to `tests/host/main.c` and
`tests/host/run_tests.sh`.

## Data and logs

When the SD card mounts, the layout under `/sdcard` is:

```
logs/access/YYYYMMDD.csv   hash-chained access records
logs/audit/YYYYMMDD.csv    hash-chained admin/security actions
logs/energy/YYYYMMDD.csv   room energy samples (one per minute)
config/                    users.csv + schedule.json mirror
state/                     reserved for state exports
export/                    energy_summary.csv and other reports
```

Access and audit rows are SHA-256 hash-chained. Use `verify` on the hub console
to check today's chain.

## Troubleshooting

- **`idf.py` not found** - export the ESP-IDF environment first
  (`export.bat` on Windows, `. ./export.sh` on Linux/macOS).
- **`storage.h` not found / option missing** - run `idf.py reconfigure`; shared
  components come from `common/` via `EXTRA_COMPONENT_DIRS`.
- **App image too large** - both projects use a custom 4 MB partition table
  (`partitions.csv`); keep `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`.
- **No MQTT traffic** - check the Wi-Fi SSID/password, the broker URI and that
  all terminals share the same `CONFIG_COCIS_GROUP_PREFIX`.
- **OLED absent** - optional; `diag` reports `oled absent` and the console still
  works.
- **Room 2 shows Room 1 data** - rebuild with `CONFIG_COCIS_ROOM_ID=2`.
- **Push rejected (403)** - make sure Git is authenticated as the repository
  owner (`OpiyoOscar01`), not another GitHub account.

## Documentation

- `docs/IMPLEMENTATION_GUIDE.md` - requirements mapping, pin map, build order,
  progress log and demo checklist.
- `docs/todo.txt` - current implementation status.
- `docs/links.md` - broker/spike results and Wokwi share links.
- `docs/CoCIS_Smart_System_Requirements_and_Design_Specification.pdf` - source
  requirements.
