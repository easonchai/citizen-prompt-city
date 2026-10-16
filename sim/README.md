# Citizen simulator

Browser simulator for the Citizen badge (PRD section 5). It runs the real C core from `core/` compiled to
WebAssembly, draws the 240x240 framebuffer on a round canvas, takes the same raw touch events the firmware
feeds the core, and wraps it in a hardware model of the ESP32-C3 board so you can read frame time, current,
battery and temperature estimates while you fast-forward days of badge life. No bundler, no dependencies.

![Hatch screen on first boot, with the readouts and content panel on the right](../docs/sim/sim-home.png)

*First boot: the prompt types out on the Hatch screen. The right column shows the live readouts and the content panel.*

## Build and run

```
make wasm                        # at the repo root, builds sim/core.wasm with Homebrew LLVM
cd sim && python3 -m http.server 8791
```

Open http://localhost:8791. It must be served over HTTP, not opened as a file, so `core.wasm` and `hardware.json`
can be fetched. `make wasm` needs `/opt/homebrew/opt/llvm/bin/clang` and `wasm-ld` from the `lld` formula
(`brew install llvm lld`). The build is `--target=wasm32 -ffreestanding -nostdlib -O2` and exports exactly the
`cz_*` functions declared in `core/citizen.h`. The resulting `sim/core.wasm` is 139,684 bytes.

`scripts/check.sh` builds the wasm and runs the simulator selftest headlessly on port 8790 as part of the
repo-wide check.

## Files

- `index.html`: the page. Round bezel and screen canvas on the left, control panels on the right.
- `sim.js`: wasm host, simulated clock, gestures, scenarios, WiFi and sleep model, telemetry, CSV, selftest.
- `model.js`: the hardware model. Pure functions over a `hardware.json` profile: frame cost, current, sync charge,
  temperature, battery hours.
- `hardware.json`: the ESP32-C3 / ESP32-2424S012C profile. Every number is editable live under Nerd > Hardware constants.
- `badge3d.js`: the 3D badge view, loaded only when you switch to it.
- `core.wasm`: built by `make wasm`, not committed.

`window.cz` exposes the wasm exports and `window.sim` the simulator internals (`sim`, `hw`, `gesture`, `advanceTo`,
`runScenario`, `toCsv`, `importLog`) for poking from the console.

## The screen and gestures

The badge is drawn at 3x (a 720x720 canvas) with a round mask and bezel, nearest-neighbour scaled so every pixel
stays a crisp 3x3 block. Backlight brightness is approximated with a CSS brightness filter floored at 35% so the
10% setting stays readable on a monitor.

Mouse or finger on the screen sends raw touch events straight to the core (`cz_touch` down, move, up) and the core
detects the gesture, exactly as it does on the badge:

- Tap: click, or touch and release.
- Swipe: drag.
- Hold: press and keep still for 1 s.
- Double tap: two quick taps (within 400 ms).

While a finger is down the simulated clock runs at 1x whatever the speed setting, so gesture timing always uses
real time. If the badge is asleep, the first touch only wakes it and is swallowed, as on hardware.

The four gesture buttons under the screen inject fixed touch sequences for people without a mouse: Tap (down, up
after 80 ms), Swipe (a left-to-right drag over 160 ms), Hold (down, up after 1.1 s) and Double tap (two taps 150 ms
apart). Hold opens the QR screen. Double tap on Home or Sulk opens Settings.

### Settings wheel

Settings is a wheel of rows with the selected row in gold. To navigate:

- Move: tap the row above or below the selected one, or swipe up or down.
- Change a value: tap the left or right arrow on the selected row, or swipe left or right. The Swipe button is a
  horizontal swipe, so in Settings it changes the selected value.
- Leave: double tap again. Hold or 20 s without touching also exits.

Everything on the wheel is core behaviour (`core/citizen.c`), not simulator code.

## URL parameters

| Parameter | Effect |
|---|---|
| `?scenario=desk`, `event` or `forgotten` | Resets and starts that scenario on load (see Scenarios). |
| `?speed=N` | Sets the speed multiplier after the scenario has started. Any positive number works; values not in the dropdown are added to it. |
| `?view=3d` | Opens the 3D badge view instead of the flat screen. Without it the view last chosen with the 3D button is restored from localStorage. |
| `?selftest` | Runs the acceptance checks once on load and prints the result (see Selftest). |

They combine, for example http://localhost:8791/?view=3d&scenario=event&speed=60.
