# Citizen simulator

Browser simulator for the badge (PRD section 5). Runs the real C core compiled to wasm plus a hardware model
with editable constants. No bundler, no dependencies.

## Build and run

```
make wasm                      # at repo root, produces sim/core.wasm
cd sim && python3 -m http.server 8787
```

Open http://localhost:8787 (HTTP, not file://, so the wasm and hardware.json load).
http://localhost:8787/?selftest runs the acceptance checks once on load and prints a `SELFTEST {...}` line to the console,
then a `SELFTEST_400 {...}` line: the page loaded in a 400 px iframe, `ok` when `document.body.scrollWidth <= 400` and no gesture
button label is clipped. (Use the iframe, not `--window-size=400`: desktop Chrome clamps windows to 500 px wide.)

## Files

- `index.html`, `sim.js`: UI, wasm host, event-driven simulated clock.
- `badge3d.js`: the 3D badge view (three.js, loaded lazily).
- `model.js`: hardware model (frame cost, current, temperature, battery). Pure functions.
- `hardware.json`: the ESP32-C3 profile. Every value is editable live under Nerd > Hardware constants.

## Controls

- Badge: 240x240 at 3x, round mask and bezel. Mouse or finger: click = tap, drag = swipe, press 1 s = hold, two quick clicks = double tap.
  While a finger is down the clock runs at 1x, so gestures always use real time (S3).
- 3D button (above the badge): swaps the round screen for a three.js model of the physical badge (ESP32-2424S012C in its stock
  case) with the live framebuffer on its window. Drag or swipe to spin, double-click to reset; it auto-rotates when left alone.
  Look only: touches on it never reach the core, use 2D (or the gesture buttons) for that. The choice is remembered in localStorage.
  three.js 0.160.0 loads from cdnjs (OrbitControls from jsDelivr) on first use; the 2D sim does not need it.
- URL parameters: `?scenario=desk|event|forgotten` starts that preset, `?speed=N` sets the speed (after the preset), `?view=3d`
  opens the 3D view, e.g. http://localhost:8787/?view=3d&scenario=event&speed=60.
- Gesture buttons inject raw touch sequences: Tap, Swipe, Hold, Double tap (opens Settings: tap = next item, swipe right = change,
  swipe left = back, hold = exit).
- Clock: time of day (HH:MM, calls `cz_set_clock`), timezone (`cz_set_timezone`), and "use my computer's clock", which re-sets the
  badge clock from `Date` every simulated minute. Between sets the core advances its own clock from `cz_tick`. Scenarios set it:
  Desk day 09:00, Event day 10:00, Forgotten 20:00.
- Host requests: `cz_host_request()` is polled every simulated frame and the request name shows in the status line.
  PORTAL = "portal would start (simulated)"; CHECK_UPDATE = after 2 simulated s `cz_set_update("1.1.1")`; INSTALL_UPDATE =
  "would reboot into 1.1.1", then version 1.1.1 and no update pending. Version 1.1.0 and the WiFi state (`PromptKL`, 2 when
  connected else 0) are fed to the core on boot, after every re-init, and when the WiFi toggle changes.
- Time: Pause, Step 1 s, Jump 1 h, speed 1x to 3600x. The core is ticked at 30 Hz of simulated time, like the firmware loop.
- Content: creature, name, prompt, QR url, brightness, send message now, check in, save, reset, wake from deep sleep with slept seconds.
- Host sleep: Deep (F4: save, core off, a touch re-inits from the blob with the slept seconds; the waking touch is consumed),
  Light (F4 fallback: core keeps running, 0.13 mA), None (Sleep screen stays lit).
- WiFi: connected toggle, sync interval (60 s per F6), a message box delivered on the next sync, a queued check-in.
  Sync policy matches F6: one sync (connect burst + state poll) at boot and at every wake, then one every 60 s while
  awake. None while asleep (deep or light): sleeping cancels the timer, and the next wake restarts it.
  Live mode fetches `<url>/state` every 10 s real time and shows `msg`/`checkin` when they change.
- Scenarios (reset, then run at 3600x with recording on, pause at the end):
  - Desk day: 8 h, USB, WiFi off, 10 taps/h.
  - Event day: 10 h, WiFi on, 60 taps/h, syncs as F6 dictates (one per wake plus every 60 s awake). Taps that land while
    the badge is already awake do not wake it, so wakes/h is below 60; `?selftest` prints `syncs_per_h` and `wakes_per_h`.
  - Forgotten: 72 h, no touch.
  Taps arrive at random (Poisson) at the set interactions/h in simulated time.

## Hardware model (Nerd panel)

Per drawn frame:
- core ms = measured wasm `cz_tick` + render wall time x `core_calibration`
- rows from `cz_dirty_rows`; bytes = rows x 240 x 2
- push ms = bytes x 8 / `spi_hz` x 1000 + ceil(rows / 8) x `chunk_overhead_us`
- frame ms = core + push; fps = min(30, 1000 / frame)

Power: awake = MCU + panel + backlight x brightness (+ WiFi idle); each sync adds a 300 mA, 200 ms burst; light and deep sleep
use their own currents. mAh is integrated over simulated time. Battery hours = `battery_mah` / average mA over the last
simulated hour. Temperature = ambient + 5 V x mA x 30 C/W, with a 60 s lag. Heap is the static figure from the verified build.

Every number reads "est." until a firmware log has been imported.

## Record, export, import (S10)

- Record collects one row per simulated second; Export CSV writes it (or the last 300 s if nothing is recorded).
  Columns start with the F3 keys: `t,fps,frame_ms,push_ms,rows,heap,screen,hearts,wifi,rssi`, then the model extras.
- Import a firmware serial log: any lines that parse as F3 JSON are kept, drawn over the model in the chart, and
  used to suggest `core_calibration` = firmware core ms (frame_ms - push_ms) / wasm core ms, per drawn frame. Apply sets it.

Limits: browsers coarsen `performance.now()` to about 0.1 ms, and a wasm frame takes less than that, so single-frame core times read
0 or 0.1 ms raw; averages over many frames are usable. Real numbers come from the import.

`window.cz` exposes the wasm exports, `window.sim` the simulator internals.
