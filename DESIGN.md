# Citizen firmware design

Target board: ESP32-2424S012C (sold as DIYMORE ESP32-C3 1.28" round touch, with case).

## Verified hardware (source: rzeldent/platformio-espressif32-sunton board definition esp32-2424S012C.json)

| Item | Value |
|---|---|
| MCU | ESP32-C3-MINI-1U, RISC-V, 160 MHz, no PSRAM |
| Flash | 4 MB (4,194,304 bytes) |
| RAM | 327,680 bytes max app RAM |
| Display | GC9A01, 240x240 IPS, SPI2: MOSI 7, SCLK 6, CS 10, DC 2, no MISO, no reset pin, BGR, mirror X, 80 MHz |
| Backlight | GPIO 3 |
| Touch | CST816S/D, I2C: SDA 4, SCL 5, RST 1, INT 0, address 0x15, 400 kHz |
| USB | native USB CDC on boot |

No IMU. No battery included (connector present).

## Budget

- Framebuffer: 240x240 at 8-bit palette = 57,600 bytes, static. Converted to RGB565 in 8-row chunks while pushing over SPI, so no 115 KB 16-bit buffer is ever allocated.
- Sprites: 3 creatures x 8 poses x 64x64 x 1 byte = 98,304 bytes in flash (const).
- App partition: use `huge_app.csv` (3 MB app, no OTA, 1 MB SPIFFS). WiFi + HTTPClient + LovyanGFX + core must fit under 3 MB with room.
- Everything in the core is static. No malloc in the core.

## Layers

```
core/        C99, platform independent. Pet state machine, renderer, screens, gestures, QR.
             Builds for: native tests (clang), wasm32 (sim), xtensa/riscv (firmware).
sim/         Browser simulator. core.wasm + canvas + input + time controls.
firmware/    PlatformIO, Arduino framework. LovyanGFX for GC9A01 + CST816S. Preferences for save. WiFi for QR chat sync.
tools/       pack_sprites.py: ../sprites/*.png -> core/sprites.c
tests/       test_core.c: state machine, timers, save/restore, render every screen.
```

The screens are a port of `../screen/index.html` (the design source of truth). Same palette, same layout numbers, same 3x5 font, same icons.

## Gestures (core detects from raw touch)

- tap: down and up within 300 ms, moved < 12 px -> feed (on Home/Sulk), wake (on Sleep)
- swipe: moved >= 40 px horizontally within 600 ms -> play
- hold: down >= 1000 ms without moving -> QR screen; tap anywhere on QR returns Home

## Timers

- hearts: 4 max. One heart lost every 90 minutes without food. Feed adds 1. Play adds 1 every 30 minutes at most.
- sulk: 0 or 1 hearts, or 6 h since last feed.
- sleep: 30 s without touch -> Sleep screen, then host may deep sleep. Wake on touch.
- events (Feed, Play, Talk, Check-in, Hatch) show their screen for a fixed time then return Home.

## Save blob (CZ_SAVE_SIZE)

magic, version, creature, hearts, last_feed_epoch, last_play_epoch, hatched flag, name, crc. The host passes slept seconds on init so the timers continue across deep sleep.
