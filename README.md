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

### Room 2 firmware

Room 2 uses the same source with a different room id. Either:

```bash
cd room_node
idf.py menuconfig        # CoCIS Room Node -> Room number = 2
idf.py build
```

or copy the provided defaults before the first build:

```bash
cp sdkconfig.defaults.room2 sdkconfig.defaults
rm -f sdkconfig
idf.py build
```

Build Room 1 and Room 2 into **separate build directories** (or rebuild and
copy the `.bin`/`.elf` pair) because both Wokwi instances need their own
firmware image.

## Run in Wokwi

1. Open the **project folder** (`entrance_node` or `room_node`) in VS Code.
2. Build it first (Wokwi loads the binary from `build/`).
3. Open `diagram.json`, press **F1 -> Wokwi: Start Simulator**.
4. Run three instances/tabs:
   - Entrance Hub: `entrance_node`
   - Room 1: `room_node` with `CONFIG_COCIS_ROOM_ID=1`
   - Room 2: `room_node` with `CONFIG_COCIS_ROOM_ID=2`
5. Open the serial monitor for each instance (115200 baud).

Both diagrams are already wired to the pin maps in
`docs/IMPLEMENTATION_GUIDE.md`: SD card, SSD1306 status display, servos,
LEDs, buzzer, buttons, PIR, LDR and relay.

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

## Serial console quick start

There are no physical RFID readers in the simulation. Tag scans are injected
through the hub console, which drives the same access-control path.

Default accounts (created on first boot):

| User | Password | Role |
|---|---|---|
| `admin` | `admin123` | ADMIN (must change on first login) |
| `security` | `security123` | SECURITY |
| `viewer` | `viewer123` | VIEWER |

Entrance Hub:

```text
login security security123
scan in A1B2C3D4          # demo tag already in the registry
status
occupancy
nodes
rooms
logs
login admin admin123
passwd admin123 <new-password>
release A1B2C3D4          # audited exit release
lights 1 on               # remote lamp command
schedule set mon 08:00 10:00
emergency on
```

Room node:

```text
sim motion on             # inject PIR motion
sim lux 50                # inject a dark room (lamp should turn on)
sim lux 900               # daylight (lamp stays off)
sim clear                 # back to the real sensors
light on|off|auto
schedule
energy
export                    # writes logs/export/energy_summary.csv
diag
```

Type `help` on either console for the full command list.

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
