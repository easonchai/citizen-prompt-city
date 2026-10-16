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

## Build and flash

```
cd firmware
pio run -e citizen               # compile; prints RAM and Flash usage
pio run -e citizen -t upload     # flash over USB-C
pio device monitor               # serial console at 115200
```

Platform `espressif32@6.12.0`, one library dependency, `lovyan03/LovyanGFX@1.2.7`. `build_src_filter` compiles every `../core/*.c` except `libc_shim.c` (the wasm-only libc stand-in, which would clash with newlib).

Web flasher: open `/flash/` on the deployed site (or `http://localhost:3000/flash/` from `pnpm dev` in `web/`) in Chrome or Edge, plug the badge in over USB-C and press Connect. It is ESP Web Tools driven by `web/public/flash/manifest.json`, which writes `bootloader.bin` at 0x0, `partitions.bin` at 0x8000, `boot_app0.bin` at 0xE000 and `firmware.bin` at 0x10000, with a full erase on a new install. The four images in `web/public/flash/` are copied there by hand from `.pio/build/citizen/`. OTA images are separate: `pnpm firmware:publish <version>` in `web/` (`web/scripts/firmware-publish.ts`) copies `.pio/build/citizen/firmware.bin` to `web/public/firmware/firmware-<version>.bin` and writes `latest.json` next to it with `version`, `file`, `sha256`, `size` and `publishedAt`.

If the badge is not detected, hold BOOT while plugging in USB to enter the ROM download mode (GPIO 9 is a strapping pin), then release.

## Partition layout

`board_build.partitions = min_spiffs.csv` on the 4 MB flash:

| Partition | Offset | Size |
|---|---|---|
| nvs | 0x9000 | 20 KB |
| otadata | 0xE000 | 8 KB |
| app0 | 0x10000 | 1,966,080 bytes |
| app1 | 0x1F0000 | 1,966,080 bytes |
| spiffs | 0x3D0000 | 128 KB |
| coredump | 0x3F0000 | 64 KB |

Two app slots are what make OTA possible: a new build is written to the idle slot and booted on success. nvs keeps the same offset as the earlier `huge_app.csv` layout, so Preferences (the save blob, WiFi credentials, settings) survived the switch and survive every update.

## Serial console

Lines over USB CDC at 115200, one command per line, from `run_command` in `main.cpp`. Most answer `ok`; an unknown command prints the full list. Settings that persist go to Preferences namespace `citizen`.

| Command | Effect |
|---|---|
| `name X` | Set the pet's name (prefs `name`). |
| `creature N` | 0 tapir, 1 hornbill, 2 tiger (prefs `creature`). Resets the QR slug to the creature default and saves. |
| `prompt ...` | The speaker's system prompt typed out on Hatch (prefs `prompt`). |
| `wifi SSID PASS` | Store credentials (prefs `ssid`, `pass`) and restart. |
| `url BASE` | Server base URL (prefs `base`), e.g. `https://connect.prmpt.city`. Rebuilds the QR URL. |
| `slug S` | Badge slug (prefs `slug`). The QR and API paths become `<base>/c/<slug>`. |
| `msg ...` | Show a Talk message now. |
| `checkin [CITY]` | Show a Check-in, default KL. |
| `brightness 100|40|10` | Backlight level; stored in the core's save blob. |
| `time HH:MM` | Set the local clock and save. |
| `tz N` | Timezone -12..14 (prefs `tz`); re-anchors the clock from SNTP. |
| `version` | Print `CZ_VERSION`. |
| `update` | Check `<base>/api/firmware/latest` and install if newer (WiFi builds). |
| `autoupdate 0|1` | Allow the idle automatic install (prefs `autoupd`, default 1). |
| `stats` | Print the last telemetry line. |
| `reset` | Clear every preference and the RTC re-fire guards, then restart. The pet hatches again. |

The badge also prints one telemetry JSON line per second on its own (see Sync) and `boot: first frame at N ms` on startup.
