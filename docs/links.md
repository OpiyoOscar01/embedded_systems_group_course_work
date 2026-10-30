# Simulation links and spike results

Wokwi share links, broker details and spike outcomes. Keep this current.

## Network

| Item | Value |
|---|---|
| Wi-Fi SSID | Wokwi-GUEST (open) |
| MQTT broker | mqtt://broker.hivemq.com:1883 |
| Group prefix | cocis26-oscar-a91f |
| Test topic | cocis26-oscar-a91f/test |

## Wokwi links

| Node | Link | Notes |
|---|---|---|
| Entrance Hub | (paste share link) | |
| Lecture Room 1 | (paste share link) | ROOM_ID=1 |
| Lecture Room 2 | (paste share link) | ROOM_ID=2 |

## Day-one spikes (Section 17.1)

| Spike | Question | Result |
|---|---|---|
| S1 | ESP-IDF project builds and runs in Wokwi | PASS |
| S2 | Two Wokwi projects exchange MQTT via Wokwi-GUEST | pending |
| S3 | SNTP returns time | pending |
| S4 | SD card mounts with FATFS, append/read | pending |
| S5 | RFID source (part / custom chip / console) | code path ready: serial `scan in|out <uid>` reaches the HAL |
| S6 | Servo, PIR, LDR, relay, buzzer, SD behave | diagrams wired; Wokwi run pending |

## Part substitutions

| Part | Substitute | Note |
|---|---|---|
| RFID reader | console `scan in|out <uid>` behind HAL | no reliable Wokwi part; scan injection is the demo path |
| Door mechanism | servo | angle logged |
| Mains lamp | relay module (indicator LED on room node) | Wokwi cannot model mains safely |
| OLED status display | serial CLI `status` / `diag` | OLED deferred (I-24) |
| RTC | SNTP with EAT conversion | Wokwi has no RTC part |
