# Hardware

Board: ESP32-2424S012C. Source: rzeldent/platformio-espressif32-sunton board definition `esp32-2424S012C.json`, and the vendor teardown. Do not design against any other numbers.

## Verified table

| Item | Value |
|---|---|
| MCU | ESP32-C3-MINI-1U, single RISC-V core, 160 MHz, 400 KB SRAM (327,680 bytes usable), no PSRAM |
| Flash | 4 MB. Partition `huge_app.csv`: 3 MB app, 1 MB SPIFFS, no OTA |
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

`huge_app.csv`: 3 MB app (3,145,728 bytes), 1 MB SPIFFS, no OTA. Chosen because WiFi, HTTPClient, LovyanGFX and the core must fit under 3 MB with room. The cost is no over-the-air updates; flash over USB-C.

## Build numbers

Verified build, 16 Oct 2026 (v2 firmware), `pio run -e citizen`, as recorded in PRD section 1.1:

- Flash: 1,131,454 of 3,145,728 bytes
- RAM: 108,460 of 327,680 bytes

Includes WiFi, HTTP, captive portal, LovyanGFX and all sprites.

## Day-one hardware checks

1. Colour order. The panel is BGR. If red and blue are swapped, set `-DCZ_RGB_ORDER=1` in `firmware/platformio.ini`. If the image is mirrored, try `-DCZ_ROTATION` 4..7.
2. Touch wake. Confirm deep-sleep wake on touch INT (GPIO 0) is reliable. If not, enable `-DCZ_LIGHT_SLEEP_FALLBACK` (light sleep plus a 2 s timer).
3. SPI speed. Default is `-DLCD_SPI_HZ=40000000`. The panel can do 80 MHz; raise it only if the picture stays clean.
