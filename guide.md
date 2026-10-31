# CoCIS Smart Building - Lecturer Demonstration Guide

This guide walks through setting up, building, running and demonstrating the
system step by step. It assumes a clean clone and no physical hardware.

---

## 1. What the system is

Three ESP32 terminals form one smart building:

| Terminal | Role |
|---|---|
| Entrance Hub | RFID-style access, anti-passback, occupancy, opening hours, emergency roll-call, audit logging, MQTT coordination, OLED status |
| Room 1 | PIR + LDR lighting automation, warning blink, override, energy accounting, intrusion alert |
| Room 2 | Same firmware as Room 1, different `CONFIG_COCIS_ROOM_ID` |

The terminals talk over MQTT:

- Hub publishes the versioned opening-hours schedule.
- Rooms publish telemetry and events every 5 s.
- Hub publishes light commands to rooms.
- Every node keeps working locally if the network is down.

What to tell the lecturer:

> "The system is split into three ESP-IDF projects sharing common components.
> Hardware access, business logic, networking and storage are separated. Host
> tests cover the pure logic, and Wokwi simulates the hardware."

---

## 2. Software you need

Install these before the demonstration:

1. **ESP-IDF v5.2.x** with the ESP32 toolchain.
2. **VS Code**.
3. **Wokwi for VS Code** extension, signed in.
4. **Git**.
5. **Git Bash** (Windows) or a normal shell (Linux/macOS).
6. A C compiler for host tests (`gcc`).

Check ESP-IDF:

```bash
idf.py --version
```

It must print `v5.2.x`. If not, open the ESP-IDF terminal/shortcut first.

---

## 3. Get the repository

```bash
git clone https://github.com/OpiyoOscar01/embedded_systems_group_course_work.git
cd embedded_systems_group_course_work
```

If already cloned:

```bash
git pull
```

Check the layout:

```bash
ls
```

You should see `common/`, `entrance_node/`, `room_node/`, `tests/`, `docs/`
and `README.md`.

---

## 4. Build the Entrance Hub

Open a terminal where `idf.py` works (ESP-IDF Command Prompt/PowerShell, or a
shell where `export.sh`/`export.bat` has been sourced).

```bash
cd entrance_node
idf.py build
```

First build takes several minutes. You are done when you see something like:

```text
Project build complete. To flash, run:
 idf.py flash
```

The Wokwi file needs these artifacts:

```text
entrance_node/build/entrance_node.bin
entrance_node/build/entrance_node.elf
```

If the build fails after changing Kconfig or components:

```bash
idf.py reconfigure
idf.py build
```

---

## 5. Build Room 1

```bash
cd ../room_node
idf.py build
```

Artifacts:

```text
room_node/build/room_node.bin
room_node/build/room_node.elf
```

This folder uses the default Room 1 configuration
(`CONFIG_COCIS_ROOM_ID=1`).

---

## 6. Build Room 2 in a separate workspace

Room 2 needs its own copy because Wokwi loads `build/room_node.bin` from the
folder that is open, and Room 1 must keep running at the same time.

From the repository root:

```bash
cp -r room_node room_node_room2
cd room_node_room2
rm -rf build sdkconfig sdkconfig.old
cp sdkconfig.defaults.room2 sdkconfig.defaults
sed -i 's/rfc2217ServerPort = 4001/rfc2217ServerPort = 4002/' wokwi.toml
idf.py set-target esp32
idf.py build
```

The copied folder is already ignored by Git (`/room_node_room2/`). Do not
commit it.

Confirm Room 2's config:

```bash
grep CONFIG_COCIS_ROOM_ID sdkconfig
```

Expected:

```text
CONFIG_COCIS_ROOM_ID=2
```

---

## 7. Run host unit tests (optional but good to show)

From the repository root:

```bash
bash tests/host/run_tests.sh
```

Expected:

```text
Running host tests...
ALL TESTS PASSED
```

What to tell the lecturer:

> "These tests run on the PC without ESP32 hardware. They cover the schedule
> evaluator, protocol encoding, access decisions, occupancy, door and lighting
> state machines, auth, energy maths and log hash chaining."

---

## 8. Start the three Wokwi simulations

Use **three separate VS Code windows**. Each window opens one project folder.

### Window 1 - Entrance Hub

1. File -> Open Folder -> `entrance_node`.
2. Press `Ctrl+Shift+P` (Windows/Linux) or `Cmd+Shift+P` (macOS).
3. Run `Wokwi: Start Simulator`.
4. Wait for the serial monitor at the bottom.

### Window 2 - Room 1

1. File -> New Window.
2. Open Folder -> `room_node`.
3. `Wokwi: Start Simulator`.

### Window 3 - Room 2

1. File -> New Window.
2. Open Folder -> `room_node_room2`.
3. `Wokwi: Start Simulator`.

Start order does not matter, but waiting for all three to connect makes the
first demonstration smoother.

### Optional: use your own terminal instead of the Wokwi monitor

If typing in the Wokwi serial monitor is awkward, connect a normal terminal to
the simulated UART over RFC2217. Each `wokwi.toml` already exposes a port:

| Terminal | Command |
|---|---|
| Entrance Hub | `python -m serial.tools.miniterm rfc2217://localhost:4000 115200` |
| Room 1 | `python -m serial.tools.miniterm rfc2217://localhost:4001 115200` |
| Room 2 | `python -m serial.tools.miniterm rfc2217://localhost:4002 115200` |

If Python says `No module named serial`:

```bash
python -m pip install pyserial
```

Keep the Wokwi simulator tab visible while using the external terminal.
Quit miniterm with `Ctrl+]`. Do not type firmware commands into Git Bash
directly; they only work after this terminal connects to the simulated UART.

---

## 9. What to expect at boot

### Entrance Hub

Boot logs include:

```text
hub HAL ready (servo=1 buzzer=1 buttons=1 oled=1)
entrance hub ready
console ready
CoCIS Entrance Hub console. Type `help`.
HUB>
```

The OLED shows:

```text
COCIS HUB
MODE NORMAL
IN 0  NET ...
SD ...
```

### Room 1 and Room 2

Room 1:

```text
room 1 ready
CoCIS Room 1 console. Type `help`.
R1>
```

Room 2:

```text
room 2 ready
CoCIS Room 2 console. Type `help`.
R2>
```

Rooms boot with the relay off and publish telemetry every 5 s. The hub marks a
room offline after 15 s without telemetry.

Each room's SSD1306 status display shows `ROOM n`, `Lights: ON/OFF`,
`Motion: YES/NO` and the current lux, so lamp commands are visible on the
simulator.

If Wi-Fi or MQTT is still connecting, wait 15-30 s. `cocis net` shows the
current state.

---

## 10. CLI basics

Both consoles accept a git-style prefix:

```text
cocis status
```

The prefix is optional:

```text
status
```

`cocis` alone or `cocis help` prints help.

Commands are case-insensitive, one per line, and require Enter.

### Hub accounts

| User | Password | Role | Notes |
|---|---|---|---|
| `admin` | `admin123` | ADMIN | Must change password at first login |
| `security` | `security123` | SECURITY | Access and security commands |
| `viewer` | `viewer123` | VIEWER | Read-only |

The admin's first login:

```text
cocis login admin admin123
cocis passwd admin123 Cocis2026!
```

Use `Cocis2026!` for the rest of this guide. If the password was changed on a
previous run, use that password instead.

Sessions expire after 120 s idle, and three failed logins lock the account for
60 s. Log in again when a command says `Not authorised`.

### Room console

Rooms have no login. The local console is trusted.

---

## 11. Hub command reference

Read-only commands:

```text
cocis status
cocis users
cocis occupancy
cocis nodes
cocis rooms
cocis schedule
cocis diag
cocis net
```

Security commands:

```text
cocis scan in A1B2C3D4
cocis scan out A1B2C3D4
cocis alerts
cocis logs
cocis logs 20261030
cocis audit
cocis verify
cocis rollcall
cocis emergency on
cocis emergency off
cocis lights 1 on
cocis lights 2 off
cocis lights all auto
```

Administration commands:

```text
cocis adduser E5F6A7B8 "Jane Doe" viewer
cocis deluser E5F6A7B8
cocis suspend E5F6A7B8
cocis resume E5F6A7B8
cocis schedule set mon 08:00 21:00
cocis schedule close sun
cocis holiday add 2026-12-25
cocis holiday del 2026-12-25
cocis door entry
cocis door exit
cocis release A1B2C3D4
cocis config
cocis reboot
```

### Hub command meanings

| Command | What it does |
|---|---|
| `status` | Time, mode, occupancy, schedule version, SD, network, users |
| `users` | Registered tags and status |
| `occupancy` | Who is inside and since when |
| `nodes` | Room online/offline, schedule version, last sensor values |
| `rooms` | Room occupancy and lamp state |
| `alerts` | Brute-force denials and tamper state |
| `logs` | Last 20 access rows for today or a given date |
| `audit` | Last 20 admin/security audit rows |
| `verify` | Recomputes today's access-log hash chain |
| `diag` | GPIO, SD, OLED, heap and button self-test |
| `net` | Wi-Fi and MQTT connection state |
| `lights` | Publish a lamp command to a room |
| `door` | Remote unlock for 5 s |
| `release` | Let an occupant out through the exit and audit it |
| `config` | Show timing constants and limits |

---

## 12. Room command reference

```text
cocis status
cocis light on
cocis light off
cocis light auto
cocis override
cocis override 1
cocis sim motion on
cocis sim motion off
cocis sim lux 50
cocis sim lux 900
cocis sim clear
cocis schedule
cocis energy
cocis export
cocis diag
cocis net
```

| Command | What it does |
|---|---|
| `status` | Motion, lux, schedule, relay, override, network, SD |
| `light on/off/auto` | Force on, force off, or return to automatic |
| `override [minutes]` | Manual override; default 60 minutes |
| `sim motion on/off` | Inject PIR motion or silence |
| `sim lux <value>` | Inject an LDR value |
| `sim clear` | Return PIR and LDR to real Wokwi parts |
| `schedule` | Show the schedule received from the hub |
| `energy` | Lamp-on time, energy used/saved, cost saved |
| `export` | Write `logs/export/energy_summary.csv` |
| `diag` | GPIO/ADC/button self-test and free heap |

---

## 13. Configure the building for the demo

Log in on the hub as admin and open the building for every day so the first
access demonstration is not blocked by the current time:

```text
cocis login admin admin123
cocis passwd admin123 Cocis2026!
cocis schedule set mon 00:00 23:59
cocis schedule set tue 00:00 23:59
cocis schedule set wed 00:00 23:59
cocis schedule set thu 00:00 23:59
cocis schedule set fri 00:00 23:59
cocis schedule set sat 00:00 23:59
cocis schedule set sun 00:00 23:59
cocis schedule
```

Expected: each command prints `schedule updated to vN`, and `schedule` shows
every day open. Rooms receive the new version over MQTT and print
`schedule vN applied`.

Check that all three nodes are connected:

```text
cocis net
cocis nodes
cocis rooms
```

Expected: `R1` and `R2` online, with a schedule version matching the hub.

---

## 14. Demonstration script (about 15 minutes)

Run the steps in order. Commands on the hub are marked **HUB**, on Room 1
**R1**, and on Room 2 **R2**. If the hub says `Not authorised`, the 120 s
session expired; log in again with `cocis login admin Cocis2026!` (or the
security account) and repeat the last command.

### Step 1 - Show the architecture

Show the three Wokwi windows and the OLED.

Say:

> "The hub owns access control and the schedule. Each room runs its own local
> lighting loop and publishes telemetry. If MQTT drops, both sides continue
> locally and resynchronise later."

### Step 2 - Show network health

**HUB**

```text
cocis net
cocis nodes
```

Expected: Wi-Fi and MQTT up; `R1` and `R2` online.

### Step 3 - Authorised entry

Log in as security:

```text
cocis login security security123
cocis status
```

Then:

```text
cocis scan in A1B2C3D4
```

Expected:

- Entry servo opens for 5 s.
- Green entry LED turns on.
- Short beep.
- Hub OLED shows `ACCESS GRANTED` with the UID and reason.
- Log line showing `A1B2C3D4 IN -> GRANTED (1 inside)`.
- `cocis occupancy` lists the demo student.
- `cocis logs` shows the hash-chained access row.

### Step 4 - Anti-passback

```text
cocis scan in A1B2C3D4
```

Expected: denied, red entry LED, longer beep, hub OLED `ACCESS DENIED`
with the reason, occupancy stays 1.

Say:

> "The same card cannot enter twice without leaving. This is software
> anti-passback, and the decision is logged with the reason."

### Step 5 - Unknown card and brute-force alert

```text
cocis scan in BAD00001
cocis scan in BAD00001
cocis scan in BAD00001
cocis alerts
```

Expected: three denials, then a brute-force alert and an audit entry.

### Step 6 - Exit

```text
cocis scan out A1B2C3D4
cocis occupancy
```

Expected: exit green LED, exit servo opens for 5 s, short beep, hub OLED
`ACCESS GRANTED`, occupancy returns to 0.

### Step 7 - User administration

```text
cocis login admin admin123
cocis passwd admin123 Cocis2026!
cocis adduser E5F6A7B8 "Jane Doe" viewer
cocis users
cocis suspend E5F6A7B8
cocis resume E5F6A7B8
```

Expected: the new tag appears, can be suspended and resumed.

If already logged in as admin from step 13, skip the login/passwd lines.

### Step 8 - Opening-hours enforcement

```text
cocis schedule close fri
cocis scan in A1B2C3D4
```

Use the current weekday instead of `fri` if necessary. Expected: denied with
an outside-hours reason.

Restore the window:

```text
cocis schedule set fri 00:00 23:59
cocis scan in A1B2C3D4
cocis scan out A1B2C3D4
```

The second scan should be granted, proving the schedule change propagated.

### Step 9 - Room lighting automation

**R1**

```text
cocis sim lux 50
cocis sim motion on
cocis status
```

Expected:

- Relay/lamp turns on and the **Lamp** LED lights.
- Occupied LED on.
- Room display shows `Motion: YES`, `Lights: ON` and the lux value.
- `status` shows occupied, dark lux and relay on.

You can also use the real Wokwi parts instead of `sim`: run `cocis sim clear`,
click the **PIR** part and choose **Simulate Motion**, and click the **LDR**
part to set a low lux. The same LEDs, relay and room display react.

**R1**

```text
cocis energy
```

Expected: lamp-on seconds increasing, energy used and cost saved.

### Step 10 - Daylight hold

**R1**

```text
cocis sim lux 900
cocis status
```

Expected: relay off even though motion is active; `status` shows daylight.

Return to a dark room:

```text
cocis sim lux 50
```

Expected: lamp on again.

### Step 11 - Manual override and remote control

**R1**

```text
cocis light off
cocis override 1
cocis status
```

Expected: override active for 60 s; lamp on.

**HUB**

```text
cocis lights 1 on
cocis lights 1 auto
```

Expected: Room 1 logs the command, turns on, then returns to automatic.

### Step 12 - Pre-off warning

**R1**

```text
cocis sim motion off
```

Expected after 5 minutes:

1. Warning blink for 30 s.
2. Relay off.

Say:

> "The five-minute vacancy hold and thirty-second warning are Appendix C
> constants. The blink gives occupants time to react before the lamp cuts."

This step takes 5.5 minutes; run it in the background during the other
explanations if time is short.

### Step 13 - Intrusion while closed

Close the building:

**HUB**

```text
cocis schedule close fri
```

Use the current day. Then:

**R2**

```text
cocis sim motion on
```

Expected:

- Room 2 publishes an intrusion alert (rate-limited to one per 30 s).
- Hub prints the alert and adds an audit row.
- `cocis alerts` on the hub shows the event.
- Room 2's lamp stays off because the building is closed.

Restore:

```text
cocis schedule set fri 00:00 23:59
```

**R2**

```text
cocis sim clear
```

### Step 14 - Emergency and roll-call

Make sure someone is inside:

**HUB**

```text
cocis scan in A1B2C3D4
cocis rollcall
```

Then press the red emergency button, or run:

```text
cocis emergency on
```

Expected:

- Both door servos open.
- Long beep.
- OLED shows `MODE EMERGENCY`.
- Roll-call captured.
- Audit entry and event published.

Clear it:

```text
cocis emergency off
cocis scan out A1B2C3D4
```

### Step 15 - Logs and tamper evidence

**HUB**

```text
cocis login security security123
cocis logs
cocis audit
cocis verify
```

Expected:

- Access and audit rows are visible.
- `verify` reports the chain intact.

Say:

> "Every row contains the previous row's hash. Editing or deleting a row
> breaks the chain and `verify` detects it."

### Step 16 - Offline resilience

1. Stop the Room 1 simulation.
2. Wait 15 s on the hub and run `cocis nodes`.
3. Expected: `R1` offline.
4. Start Room 1 again.
5. Wait for reconnect, then run `cocis nodes`.
6. Expected: `R1` online again.

Optionally stop the hub and show that Room 1 still responds to
`cocis sim motion on`, proving local control does not depend on the network.

---

## 15. Behaviour reference for questions

| Event | Expected |
|---|---|
| Registered IN during hours | Granted, door 5 s, occupancy +1 |
| Same tag IN again | Anti-passback denial |
| Unknown tag 3 times | Brute-force alert |
| OUT | Granted, occupancy -1 |
| Outside hours | Denied; room lamps suppressed |
| Motion + lux < 600 | Lamp on |
| Lux > 800 | Daylight hold, lamp off |
| No motion 5 min | 30 s warning blink, then off |
| Override | Lamp on for 60 min by default |
| Motion while closed | Intrusion alert |
| Emergency | Both doors release, roll-call |
| Room silent 15 s | Hub marks it offline |
| Closing with occupants | Closing sweep alert + audit |

Constants:

- Door open time: 5 s
- Vacancy hold: 300 s
- Warning: 30 s
- Override: 60 min
- Heartbeat: 5 s
- Offline threshold: 15 s
- Dark threshold: lux < 600
- Daylight threshold: lux > 800
- Lamp: 72 W
- Tariff: 780 UGX/kWh

---

## 16. What was implemented (summary for the lecturer)

### Architecture

- `common/cocis_types` - enums, result codes, constants.
- `common/cocis_proto` - JSON envelope, topic builders, event IDs, dedupe.
- `common/cocis_time` - SNTP/EAT clock, schedule model and JSON.
- `common/cocis_log_chain` - SHA-256 hash chain.
- `common/cocis_net` - Wi-Fi, MQTT, LWT, offline queue.
- `common/storage` - SDSPI/FATFS single-writer storage.

### Hub

- HAL for servos, LEDs, buzzer, buttons, SD, OLED.
- User registry with active/suspended/expired states.
- Access decision pipeline: emergency, unknown/expired, schedule,
  anti-passback, grant.
- Occupancy persisted in NVS.
- Door FSMs and exit anomaly detection.
- Auth with PBKDF2-HMAC-SHA256, roles, lockout and idle timeout.
- Hash-chained access/audit logs.
- Emergency mode and roll-call.
- Brute-force and tamper alerts.
- Closing sweep.
- MQTT room tracking, commands and schedule publication.

### Rooms

- PIR + LDR + relay + LEDs + override button HAL.
- Lighting FSM with daylight hysteresis and warning blink.
- Energy accounting and CSV export.
- Schedule subscription and local persistence.
- Intrusion detection.
- Telemetry and event publishing.

### Testing

- Host unit tests for pure logic.
- Wokwi integration demo for hardware, networking and CLI.
- Builds verified with ESP-IDF v5.2.x.

---

## 17. Troubleshooting during the demo

**`idf.py` not found**

Open the ESP-IDF terminal/shortcut, or source the environment:

```bash
# Windows
C:\esp\esp-idf\export.bat

# Linux/macOS
. $HOME/esp/esp-idf/export.sh
```

**Build fails after config changes**

```bash
idf.py reconfigure
idf.py build
```

**Wokwi says firmware not found**

Build that folder first. The paths must exist:

```text
build/entrance_node.bin
build/room_node.bin
```

**Serial monitor does not accept typing**

Click directly inside the Wokwi serial monitor input, not the VS Code
terminal. Wait for the prompt first.

**Rooms show offline**

Check `cocis net` on the room. Wait for Wi-Fi/MQTT, confirm all nodes use the
same `CONFIG_COCIS_GROUP_PREFIX`, and confirm the public broker is reachable.

**Access denied unexpectedly**

Run `cocis schedule` and check the current EAT time. Use the admin schedule
commands from step 13, or `cocis emergency on` for a full release.

**Admin password rejected**

The password may have been changed on an earlier run. Use the password you
set, or reset the simulated flash/NVS and let first boot create `admin123`
again.

**Lamp does not turn on**

Check the room is open (`cocis schedule`), lux is below 600
(`cocis sim lux 50`), motion is active (`cocis sim motion on`), and no forced
off is active (`cocis light auto`).

**OLED shows nothing**

The OLED is optional. `cocis diag` reports `oled absent`; everything else
still works on the serial console.

**SD card unavailable**

Logging falls back gracefully. `cocis diag` shows SD state. Check the SD
wiring in `diagram.json` if the card does not mount.

**Room 2 shows R1**

Rebuild `room_node_room2` and confirm `CONFIG_COCIS_ROOM_ID=2` in its
`sdkconfig`.

---

## 18. Clean finish

Stop the three simulations with the Wokwi stop button. If you changed the
schedule for the demo, restore normal hours:

```text
cocis schedule set mon 08:00 21:00
cocis schedule set tue 08:00 21:00
cocis schedule set wed 08:00 21:00
cocis schedule set thu 08:00 21:00
cocis schedule set fri 08:00 21:00
cocis schedule set sat 08:00 14:00
cocis schedule close sun
```

Show the final evidence:

```text
cocis status
cocis nodes
cocis logs
cocis audit
cocis verify
```

That completes the demonstration.
