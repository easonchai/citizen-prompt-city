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

## WiFi portal

Credentials are provisioned through a captive portal, `run_portal` in `main.cpp`. It opens two ways:

1. Hold BOOT for 2 s once the screen is lit (holding it through power-on enters ROM download mode instead).
2. Settings wheel, WiFi row, forward tap or swipe right (`CZ_REQ_PORTAL`).

Flow:

1. The badge starts an open access point named `CITIZEN-XXXX` (the last two bytes of its MAC) and shows `JOIN WIFI CITIZEN-XXXX` on the Talk screen; the same line goes to serial.
2. A DNS server answers every hostname with 192.168.4.1, and unknown paths redirect there, so phones open the captive portal page on join.
3. The form takes WiFi name (required, up to 32), password (up to 64), server base URL (optional, up to 128, placeholder is `CZ_DEFAULT_BASE`) and slug (optional, up to 32, placeholder `tenuk`). Out-of-range input gets a 400.
4. Save writes `ssid`, `pass` and, if given, `base` and `slug` to Preferences, answers "Saved. Citizen is restarting." and restarts after 500 ms.

The portal blocks the main loop but keeps ticking the core and pushing frames, and the serial console still works inside it. With no stored SSID the badge never starts the radio and counts its first sync as done so it can still sleep.

On boot with an SSID stored, `wifi_begin` connects in the background (station mode) and the pet keeps running. The Settings WiFi row shows CONNECTING for 8 s, then OK or FAIL; WiFi keeps retrying after FAIL. On connect, SNTP starts against `pool.ntp.org` with the stored timezone.

## Sync

All HTTP goes to `<base>/api/c/<slug>/...` with a 3 s timeout, where `<base>` is prefs `base` (default `CZ_DEFAULT_BASE`) and `<slug>` is prefs `slug` (default the creature's lowercase name: `tenuk`, `kenyalang`, `harimau`). The badge parses responses with a tiny flat-object string scanner (`json_str`), so values must be plain strings without escapes.

### State poll

`GET <base>/api/c/<slug>/state`, once right after connecting (or after the 8 s connect timeout), then every 60 s while awake. The server rate-limits to one poll per 10 s per slug and answers 429 with an empty state otherwise.

```json
{"msg":"<last assistant reply, at most 64 chars>","checkin":"<city or empty>","name":"<badge name>","updatedAt":"<ISO 8601 or null>","now":"2026-10-31T09:41:00.000Z"}
```

What the badge does with it:

- `msg` opens Talk only when `updatedAt` differs from the last one seen. The last value is kept in RTC memory across deep sleep and in prefs `last_upd` across power loss, so an old message does not re-open Talk on every poll and wake.
- `checkin` opens Check-in when the city differs from the last one seen (RTC and prefs `last_city`).
- `name` is stored and applied when it differs from prefs `name`.
- `now` (UTC) sets the core clock, converted to local with the stored timezone, until SNTP has synced. After that the badge re-anchors to system time once a minute.

### Telemetry

The badge prints one JSON line per second on serial, and once a minute while awake (never on the first poll of a wake) POSTs the latest line as `Content-Type: application/json` to `<base>/api/c/<slug>/telemetry`. The server stores anything that parses as a JSON object up to 2048 bytes; `GET ...?n=100` reads the latest rows back.

```json
{"t":123456,"fps":30,"frame_ms":1.52,"push_ms":1.20,"rows":130,"heap":198000,"screen":1,"hearts":3,"wifi":"ok","rssi":-61}
```

`t` is `millis()`, `fps` counts loop passes that pushed pixels, `frame_ms` is tick plus render plus push averaged over the second, `push_ms` is convert plus SPI, `rows` is the largest dirty span pushed, `screen` is `enum cz_screen`, `wifi` is `off`, `connecting`, `ok` or `fail`. The numbers in the example are illustrative; no real device line has been captured yet.

### OTA

`GET <base>/api/firmware/latest` returns the published `latest.json` plus an absolute download URL:

```json
{"version":"1.1.0","file":"firmware-1.1.0.bin","sha256":"<hex>","size":1219040,"publishedAt":"2026-10-16T05:32:44.904Z","url":"https://<host>/firmware/firmware-1.1.0.bin"}
```

The badge reads `version` and `url` only. It checks after the first state poll of every wake and then at most hourly, compares `version` to `CZ_VERSION` as three-part numeric semver, and tells the core (`cz_set_update`) only when the published build is newer. Install happens three ways: automatically, once per wake, when a newer build is known, prefs `autoupd` is 1 (default) and the badge has sat untouched on Home or Sulk for 10 s; from the Settings Update row; or the serial `update` command. `install_update` saves the pet, shows `UPDATING...`, then streams the image into the idle app slot with `HTTPUpdate` over a `WiFiClientSecure` that skips certificate checks, following redirects, and reboots on success. On failure it shows `UPDATE FAILED`. `sha256` from the manifest is not verified by the badge; the ESP-IDF image header and appended hash are still checked before the slot switch.

## Power behaviour

The main loop runs one pass every 16 ms: serial, touch, `cz_tick`, WiFi housekeeping, host requests from Settings, persist changed settings, push dirty rows, backlight, telemetry, periodic save. If a pass overruns it does not try to catch up.

Saves: `save_now` writes the core's 64-byte blob to prefs `save` when hearts change, every 5 minutes, before sleep, and on creature or brightness changes. Identical blobs are not rewritten, to spare the flash.

Sleep: the core asks to sleep (`cz_wants_sleep`) after 30 s idle on Home or Sulk. The firmware honours it only after this wake's first sync attempt has finished, so an idle badge still syncs once per wake. `go_to_sleep` then saves, stamps the RTC clock in `RTC_DATA_ATTR` memory, turns the backlight off, puts the panel to sleep and enters deep sleep (about 5 uA) with wake on the touch INT line (GPIO 0) going low. The ESP32-C3 can only wake deep sleep on GPIO 0..5, so BOOT (GPIO 9) cannot wake it; RESET does.

Wake: deep sleep reboots the chip. `setup` diffs the RTC clock against the stamp to get slept seconds (the RTC timer keeps running through deep sleep but not through power loss, where it passes 0), loads the blob with `cz_init`, re-applies the host-owned content (creature, name, prompt, timezone, version, clock from SNTP if still valid), pushes the first full frame before turning the backlight on, and only then starts WiFi. USB CDC drops while asleep and comes back on wake.

Fallback: build with `-DCZ_LIGHT_SLEEP_FALLBACK` if level wake from deep sleep proves unreliable. Light sleep (about 0.13 mA, RAM kept) wakes on touch INT low, BOOT low, or a 2 s timer; on a timer wake it polls the touch controller and goes back to sleep if nothing is pressed, then reloads the pet without a reboot.

Backlight: `cz_brightness` (100, 40 or 10) maps to PWM at 44.1 kHz on GPIO 3; it is applied only when it changes.
