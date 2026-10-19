# Hardware

Board: ESP32-2424S012C. Source: rzeldent/platformio-espressif32-sunton board definition `esp32-2424S012C.json`, and the vendor teardown. Do not design against any other numbers.

How the firmware uses these pins, builds, flashes, syncs and sleeps: [`firmware/README.md`](../firmware/README.md).

## Verified table

| Item | Value |
|---|---|
| MCU | ESP32-C3-MINI-1U, single RISC-V core, 160 MHz, 400 KB SRAM (327,680 bytes usable), no PSRAM |
| Flash | 4 MB. Partition `min_spiffs.csv`: two 1.9 MB OTA app slots, 128 KB SPIFFS |
| Display | GC9A01, 240x240 round IPS, SPI2: MOSI 7, SCLK 6, CS 10, DC 2, no MISO, no reset pin, BGR, mirror X. 40 MHz default, 80 MHz possible |
| Backlight | GPIO 3, active high, PWM |
| Touch | CST816S/D capacitive, I2C: SDA 4, SCL 5, RST 1, INT 0, address 0x15, 400 kHz, one point, gestures in hardware (not used) |
| Radio | WiFi 2.4 GHz b/g/n, Bluetooth 5 LE |
| Power | USB-C 5 V. Battery connector JST 1.25 with charger on board. V1 ships without a battery |
| Buttons | BOOT (GPIO 9) and RESET on the back of the case, reachable |
| Case | Black plastic, round, 1.28" window. Buttons and battery cutout exposed on the back |
| No IMU | Play is a swipe, not a shake |

## Pin map

| Signal | GPIO |
|---|---|
| LCD MOSI | 7 |
| LCD SCLK | 6 |
| LCD CS | 10 |
| LCD DC | 2 |
| LCD MISO / RST | none |
| Backlight | 3 |
| Touch SDA | 4 |
| Touch SCL | 5 |
| Touch RST | 1 |
| Touch INT | 0 |
| BOOT button | 9 |

## Partition

`min_spiffs.csv`: nvs 0x9000 (20 KB), otadata 0xe000 (8 KB), app0 0x10000 and app1 0x1F0000 (1,966,080 bytes each), SPIFFS 0x3D0000 (128 KB), coredump 0x3F0000 (64 KB). Two app slots let the badge update over WiFi (F11): the new build is written to the idle slot and booted on success. nvs keeps its offset, so Preferences (save blob, WiFi, settings) survive the switch from `huge_app.csv`. The USB flasher writes bootloader 0x0, partitions 0x8000, boot_app0 0xe000, firmware 0x10000.

## Build numbers

Verified build, 16 Oct 2026 (firmware 1.1.0, `min_spiffs.csv`), `pio run -e citizen`:

- Flash: 1,171,402 of 1,966,080 bytes (59.6%)
- RAM: 108,956 of 327,680 bytes (33.3%)
- `firmware.bin`: 1,218,768 bytes

Includes WiFi, HTTP, captive portal, LovyanGFX and all sprites. Firmware 1.3.0 (5 saved networks, portal QR, Improv, Sync, telemetry every 10 minutes) ran on the real boards on 2026-10-19 (display, touch, WiFi setup and sync, sleep and wake, power cut) and is published for OTA and the web flasher.

Board facts from that day: ESP32-C3 rev v0.4 with 4 MB XMC flash and native USB (it enumerates as Espressif's USB JTAG/serial at `/dev/cu.usbmodem*`; there is no CH340, so the leaflet's driver is not needed). It needs a USB-A to USB-C data cable: some bundled cables are charge-only, and a C-to-C cable may not power it.

## Day-one hardware checks

1. Colour order. The panel is BGR. If red and blue are swapped, set `-DCZ_RGB_ORDER=1` in `firmware/platformio.ini`. If the image is mirrored, try `-DCZ_ROTATION` 4..7.
2. Touch wake. Confirm deep-sleep wake on touch INT (GPIO 0) is reliable. If not, enable `-DCZ_LIGHT_SLEEP_FALLBACK` (light sleep plus a 2 s timer).
3. SPI speed. Default is `-DLCD_SPI_HZ=40000000`. The panel can do 80 MHz; raise it only if the picture stays clean.
