# Citizen firmware

The host side of the badge: a PlatformIO project on the Arduino framework for the ESP32-C3 that owns time, touch, storage, WiFi and the backlight, and pushes the core's framebuffer to the panel over SPI DMA. Everything about the pet lives in [`core/`](../core/README.md); this directory is `platformio.ini`, `src/main.cpp` and `stub_core.c`.

`stub_core.c` is always compiled but is empty once `core/citizen.c` exists (it checks `__has_include`), so a fresh clone builds with no edits here.

Facts below come from `platformio.ini`, `src/main.cpp`, `../docs/HARDWARE.md` and `../scripts/check.sh` as of 16 Oct 2026, firmware 1.1.0.

## Board and pins

ESP32-2424S012C, sold as the DIYMORE ESP32-C3 1.28 inch round touch display with case. Board facts are from the rzeldent/platformio-espressif32-sunton definition `esp32-2424S012C.json`; the project uses the generic `esp32-c3-devkitm-1` board and overrides what differs. Full table in [`docs/HARDWARE.md`](../docs/HARDWARE.md).

| Item | Value |
|---|---|
| MCU | ESP32-C3-MINI-1U, single RISC-V core at 160 MHz, 327,680 bytes RAM, no PSRAM |
| Flash | 4 MB, QIO at 80 MHz |
| Display | GC9A01 240x240 round IPS on SPI2, BGR, mirror X. Driven at `LCD_SPI_HZ` = 40 MHz (panel can do 80) |
| Touch | CST816S capacitive on I2C at 400 kHz, address 0x15, one point |
| USB | USB-C, native USB CDC on boot (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`), serial at 115200 |
| Buttons | BOOT on GPIO 9 and RESET, on the back of the case |
| Battery | JST 1.25 connector with charger; V1 ships without a battery |

| Signal | GPIO | Build flag |
|---|---|---|
| LCD MOSI | 7 | `LCD_MOSI` |
| LCD SCLK | 6 | `LCD_SCLK` |
| LCD CS | 10 | `LCD_CS` |
| LCD DC | 2 | `LCD_DC` |
| LCD MISO, RST | none | |
| Backlight (active high, PWM) | 3 | `LCD_BL` |
| Touch SDA | 4 | `TOUCH_SDA` |
| Touch SCL | 5 | `TOUCH_SCL` |
| Touch RST | 1 | `TOUCH_RST` |
| Touch INT | 0 | `TOUCH_INT` |
| BOOT button | 9 | `BOOT_PIN` in `main.cpp` |

Other build flags in `platformio.ini`: `CZ_VERSION` (the running version string, "1.1.0"), `CZ_DEFAULT_BASE` (the server, `https://connect.prmpt.city`), `CZ_RGB_ORDER` and `CZ_ROTATION` (panel orientation, see the checklist), `CZ_LIGHT_SLEEP_FALLBACK` (commented out), and `CZ_WIFI` (defaults to 1 in `main.cpp`; 0 builds a radio-free badge).
