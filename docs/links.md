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
| S5 | RFID source (part / custom chip / console) | pending |
| S6 | Servo, PIR, LDR, relay, OLED, RTC behave | pending |

## Part substitutions

| Part | Substitute | Note |
|---|---|---|
| RFID reader | console `sim scan <entry|exit> <uid>` behind HAL | if no reliable Wokwi part |
| Door mechanism | servo | angle logged |
| Mains lamp | LED via relay | |
