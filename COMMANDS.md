# CoCIS Smart Building - Command Reference

All commands work on the simulated serial console. Commands are case-insensitive,
one per line, and the optional `cocis` prefix is supported (e.g. `cocis status`
or just `status`).

---

## 1. Connect to a node

Each `wokwi.toml` exposes the simulated UART over RFC2217:

| Node | Port | VS Code folder |
|---|---|---|
| Entrance Hub | 4000 | `entrance_node` |
| Room 1 | 4001 | `room_node` |
| Room 2 | 4002 | `room_node_room2` |

The Wokwi simulator tab must stay visible, otherwise the simulation pauses and
the console stops responding.

### Helper script (recommended)

Run from the repository root:

```bash
# one command, print the reply, then exit
python scripts/hub.py "cocis status"
python scripts/hub.py -p r1 "status"
python scripts/hub.py -p r2 "status"

# interactive console (type commands at the ">" prompt, 'quit' to exit)
python scripts/hub.py
python scripts/hub.py -p r1
python scripts/hub.py -p r2
```

`--wait 30` waits for a paused simulator to resume before sending:

```bash
python scripts/hub.py --wait 30 "help"
```

If pyserial is missing:

```bash
python -m pip install pyserial
```

### Plain miniterm

```bash
python -m serial.tools.miniterm rfc2217://localhost:4000 115200
python -m serial.tools.miniterm rfc2217://localhost:4001 115200
python -m serial.tools.miniterm rfc2217://localhost:4002 115200
```

Quit miniterm with `Ctrl+]`.

### Waiting for the prompt

Do not type until the node prints its prompt:

```text
HUB>
R1>
R2>
```

---

## 2. Hub accounts and roles

Default accounts created on first boot:

| User | Password | Role | Notes |
|---|---|---|---|
| `admin` | `admin123` | ADMIN | Must change password at first login |
| `security` | `security123` | SECURITY | Access and security commands |
| `viewer` | `viewer123` | VIEWER | Read-only |

- Sessions idle out after 120 s; log in again when a command says
  `Not authorised`.
- Three failed logins lock the account for 60 s.
- The `admin` account must run `passwd` before any other admin command works.
- Rooms have no login: the local console is trusted.

### Login and logout

```text
cocis login <user>
Password: ********
cocis whoami
cocis logout
```

The password is not echoed; one `*` is printed per character. For scripts or
one-shot helpers you can also pass it inline: `cocis login security security123`.

Examples:

```text
cocis login security
cocis login viewer
cocis login admin
```

### Change a password

```text
cocis passwd
Current password: ********
New password: ********
```

The new password must be at least 8 characters. You can also pass both values
inline: `cocis passwd <old> <new>`.

The admin's first login:

```text
cocis login admin
Password: ********
cocis passwd
Current password: ********
New password: ********
```

Use `Cocis2026!` for the rest of the demo, or whatever password you set.

---

## 3. Entrance Hub commands

### General

| Command | Purpose |
|---|---|
| `help` | List commands available to your current role |
| `login <user> [pass]` | Open a session (prompts for the password when omitted) |
| `logout` | Close the session |
| `passwd <old> <new>` | Change your password |
| `whoami` | Current user and role |
| `reboot` | Restart the hub (ADMIN) |

### Read-only (VIEWER or higher)

| Command | Purpose |
|---|---|
| `status` | Time, mode, occupancy, schedule version, SD, network, users |
| `users` | Registered tags and their status |
| `occupancy` | Who is inside and since when |
| `nodes` | Room online/offline and schedule version |
| `rooms` | Room occupancy, lamp and last sensor values |
| `schedule` | Opening hours and holidays |
| `diag` | GPIO, SD, OLED, heap and button self-test |
| `net` | Wi-Fi and MQTT connection state |

Examples:

```text
cocis status
cocis nodes
cocis rooms
cocis net
```

### Security (SECURITY or ADMIN)

| Command | Purpose |
|---|---|
| `scan in <uid>` | Simulate an entry scan |
| `scan out <uid>` | Simulate an exit scan |
| `alerts` | Brute-force denials and tamper state |
| `logs [YYYYMMDD]` | Last 20 access rows for a date |
| `audit [YYYYMMDD]` | Last 20 admin/security audit rows |
| `verify` | Check today's access-log hash chain |
| `rollcall` | List the emergency roll-call |
| `emergency on` | Enter emergency mode; both doors release |
| `emergency off` | Clear emergency mode |
| `lights 1\|2\|all on\|off\|auto` | Send a lamp command to a room |

Examples:

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

The default demo tag is `A1B2C3D4` (Demo Student, VIEWER).

### Administration (ADMIN only)

| Command | Purpose |
|---|---|
| `adduser <uid> <name> [viewer\|security\|admin] [YYYY-MM-DD]` | Register a tag |
| `deluser <uid>` | Remove a tag |
| `suspend <uid>` | Suspend a tag |
| `resume <uid>` | Reactivate a tag |
| `schedule set <day> <open> <close>` | Set a day's opening window |
| `schedule close <day>` | Close a whole day |
| `holiday add <YYYY-MM-DD>` | Add a holiday |
| `holiday del <YYYY-MM-DD>` | Remove a holiday |
| `door entry` / `door exit` | Remote unlock for 5 s |
| `release <uid>` | Let an occupant out through the exit |
| `config` | Show active timing constants and limits |

Examples:

```text
cocis adduser E5F6A7B8 "Jane Doe" viewer
cocis adduser B1234567 "Seguridad" security 2027-01-31
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
```

Days are `mon tue wed thu fri sat sun`. Times are `HH:MM` and the open time
must be before the close time.

### Open the building for a demo

Default hours are Mon-Fri 07:00-21:00, Sat 08:00-14:00, Sun closed. To avoid
outside-hours denials during a demo:

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

Restore normal hours when finished:

```text
cocis schedule set mon 08:00 21:00
cocis schedule set tue 08:00 21:00
cocis schedule set wed 08:00 21:00
cocis schedule set thu 08:00 21:00
cocis schedule set fri 08:00 21:00
cocis schedule set sat 08:00 14:00
cocis schedule close sun
```

---

## 4. Room 1 and Room 2 commands

Rooms have no login. Use the same commands on either room.

| Command | Purpose |
|---|---|
| `help` | List commands |
| `status` | Motion, lux, schedule, relay, override, network, SD |
| `light on\|off\|auto` | Force the lamp on/off or return to automatic |
| `override` | Manual override for 60 minutes |
| `override <minutes>` | Manual override for N minutes |
| `sim motion on\|off\|clear` | Inject PIR motion or return to real sensor |
| `sim lux <value>` | Inject an LDR lux value |
| `sim lux clear` | Return the LDR to the real sensor |
| `sim clear` | Return PIR and LDR to the real sensors |
| `schedule` | Show the synced opening hours and current state |
| `energy` | Lamp-on time, energy used/saved, cost saved |
| `export` | Write `logs/export/energy_summary.csv` |
| `diag` | GPIO/ADC/button self-test and free heap |
| `net` | Wi-Fi and MQTT state |

Examples:

```text
status
sim motion on
sim lux 50
light on
override 1
energy
sim motion off
sim clear
net
```

### Lighting behaviour

| Event | Expected |
|---|---|
| Motion and lux < 600 | Lamp on immediately |
| Lux > 800 (daylight) | Lamp stays off even with motion |
| No motion for 5 min | 30 s warning blink, then lamp off |
| `override` | Lamp on for 60 min by default |
| Motion while building closed | Intrusion alert sent to the hub |
| Remote `lights` command | Room applies it locally and logs it |

The room's SSD1306 status display shows the live values, including
`Lights: ON` / `Lights: OFF`, so the lamp state is visible on the simulator.

---

## 5. Full demonstration script

Run these in order with all three terminals online.

### Step 1 - Log in and check the network

**HUB**
```text
cocis login security security123
cocis net
cocis nodes
```

Expected: Wi-Fi/MQTT up, `R1` and `R2` online.

### Step 2 - Authorised entry

**HUB**
```text
cocis scan in A1B2C3D4
cocis status
cocis occupancy
cocis logs
```

Expected: door opens 5 s, green LED, short beep, occupancy 1, logged.

### Step 3 - Anti-passback

**HUB**
```text
cocis scan in A1B2C3D4
```

Expected: denied, red LED, longer beep, occupancy stays 1.

### Step 4 - Unknown tag and brute-force alert

**HUB**
```text
cocis scan in BAD00001
cocis scan in BAD00001
cocis scan in BAD00001
cocis alerts
```

Expected: three denials, then a brute-force alert and an audit row.

### Step 5 - Exit

**HUB**
```text
cocis scan out A1B2C3D4
cocis occupancy
```

Expected: exit granted, occupancy 0.

### Step 6 - User administration

**HUB**
```text
cocis login admin admin123
cocis passwd admin123 Cocis2026!
cocis adduser E5F6A7B8 "Jane Doe" viewer
cocis users
cocis suspend E5F6A7B8
cocis resume E5F6A7B8
```

### Step 7 - Opening-hours enforcement

**HUB**
```text
cocis schedule close fri
cocis scan in A1B2C3D4
cocis schedule set fri 00:00 23:59
cocis scan in A1B2C3D4
cocis scan out A1B2C3D4
```

Use the current weekday instead of `fri`.

### Step 8 - Room automation

**R1**
```text
sim lux 50
sim motion on
status
energy
sim lux 900
status
sim lux 50
```

Expected: lamp on in the dark, off in daylight, back on when dark.

### Step 9 - Manual override and remote control

**R1**
```text
light off
override 1
status
```

**HUB**
```text
cocis lights 1 on
cocis lights 1 auto
```

### Step 10 - Pre-off warning

**R1**
```text
sim motion off
```

Wait 5 minutes: 30 s warning blink, then the lamp turns off.

### Step 11 - Intrusion while closed

**HUB**
```text
cocis schedule close fri
```

**R2**
```text
sim motion on
```

Expected: intrusion alert at the hub, Room 2 lamp stays off.

Restore:
```text
cocis schedule set fri 00:00 23:59
```

**R2**
```text
sim clear
```

### Step 12 - Emergency and roll-call

**HUB**
```text
cocis scan in A1B2C3D4
cocis rollcall
cocis emergency on
cocis emergency off
cocis scan out A1B2C3D4
```

### Step 13 - Logs and tamper evidence

**HUB**
```text
cocis login security security123
cocis logs
cocis audit
cocis verify
```

Expected: `verify` reports the chain intact.

### Step 14 - Offline resilience

1. Stop the Room 1 simulation.
2. Wait 15 s, then on the hub run:
   ```text
   cocis nodes
   ```
   Expected: `R1` offline.
3. Start Room 1 again, wait, run `cocis nodes`.
   Expected: `R1` online.

---

## 6. Expected timings and limits

| Item | Value |
|---|---|
| Door open time | 5 s |
| Vacancy hold before auto-off | 300 s |
| Pre-off warning | 30 s |
| Manual override | 60 min |
| Heartbeat / telemetry | 5 s |
| Room offline threshold | 15 s |
| Dark threshold | lux < 600 |
| Daylight threshold | lux > 800 |
| Lamp power | 72 W |
| Tariff | 780 UGX/kWh |
| Console session idle timeout | 120 s |
| Login lockout | 3 failures, 60 s |

---

## 7. Troubleshooting

| Symptom | Fix |
|---|---|
| `no console response` from `hub.py` | Simulator paused or not running. Restart it and keep its tab visible; use `--wait 30`. |
| Commands type nothing in Wokwi monitor | The Wokwi VS Code monitor is output-only. Use `scripts/hub.py` or miniterm for input. |
| `Not authorised` | Log in again; SECURITY or ADMIN may be required. |
| Admin password rejected | It may have been changed earlier. Use the password you set, or reset simulated NVS. |
| Access denied unexpectedly | Run `schedule`; open the day with `schedule set` or use `emergency on`. |
| Rooms show offline | Check `net` on the room, wait for Wi-Fi/MQTT, confirm all nodes share the same group prefix. |
| Lamp does not turn on | Check the room is open, `sim lux 50`, `sim motion on`, and `light auto`. |
| `nodes`/`rooms` stale | Wait for the next 5 s heartbeat. |
| SD unavailable | Logging falls back to RAM; `diag` shows the state. |
| OLED absent | Optional; `diag` reports `oled absent` and the console still works. |
