# Citizen simulator

Browser simulator for the badge. Runs the real C core compiled to wasm; no bundler, no dependencies.

## Build and run

```
make wasm                      # at repo root, produces sim/core.wasm
cd sim && python3 -m http.server 8787
```

Open http://localhost:8787. (Must be served over HTTP, not file://, so the wasm can load.)

## Controls

- Screen: 240x240 drawn at 3x. Click/tap = tap, drag = swipe, press and hold 1 s = hold. Touch uses the simulated clock, so at high speed a "hold" happens in less real time; use 1x for gestures.
- Speed: 1x, 10x, 60x, 600x, 3600x multiplier on real time. Sim time shows elapsed simulated time.
- Creature / Name / Prompt / QR url: set on the core. Creature applies on change; the others on "Apply content".
- Send message: `cz_message` (shows Talk). Check in (KL): `cz_checkin("KL")`.
- Save: `cz_save` (64 bytes) stored base64 in localStorage (`citizen-save`).
- Reset: `cz_init` with a fresh seed, no save, then reapplies content.
- Wake from sleep: saves, then re-inits from that blob with "Slept seconds", as the firmware does after deep sleep. The sim clock is not advanced; the core derives the lost time from slept seconds.
- Readouts: screen, hearts, pose, hunger (mm:ss since last feed), wants_sleep, dirty frames per second.

`window.cz` exposes the wasm exports in the browser console.
