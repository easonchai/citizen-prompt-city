# Simulator hardware model

How `sim/model.js` turns a drawn frame and a power state into the numbers in the Nerd panel, and how to replace
the guessed constants with measured ones. Every constant lives in `sim/hardware.json` and can be edited live
under Nerd > Hardware constants. The profile ships as "est., uncalibrated" and every readout carries an `est.`
suffix until a firmware log has been imported.

## Constants (`sim/hardware.json`)

| Key | Default | Meaning |
|---|---|---|
| `spi_hz` | 40,000,000 | SPI clock to the GC9A01 panel. |
| `chunk_rows` | 8 | Rows per SPI transfer. The firmware pushes dirty rows in chunks. |
| `chunk_overhead_us` | 40 | Fixed cost per chunk (setup, address window, RGB565 conversion). |
| `core_calibration` | 2.5 | Multiplier from wasm core time on your machine to core time on the ESP32-C3. |
| `fps_cap` | 30 | Upper bound for the fps readout; the firmware loop paces itself. |
| `ma_mcu_active` | 20 | MCU current while awake. |
| `ma_wifi_idle` | 30 | Extra current while the radio is associated and idle. |
| `ma_wifi_tx_burst` | 300 | Current during a sync burst. |
| `wifi_tx_burst_ms` | 200 | Length of one sync burst. |
| `sync_interval_s` | 60 | Seconds between syncs while awake (edited in the WiFi panel, not the constants list). |
| `ma_light_sleep` | 0.13 | Whole-board current in light sleep. |
| `ma_deep_sleep` | 0.005 | Whole-board current in deep sleep. |
| `ma_panel` | 8 | Panel logic current with the backlight off. |
| `ma_backlight_full` | 25 | Backlight current at 100% brightness. |
| `supply_v` | 5 | Supply voltage, used for the thermal estimate. |
| `thermal_c_per_w` | 30 | Temperature rise per watt dissipated. |
| `ambient_c` | 30 | Room temperature. |
| `battery_mah` | 300 | Pack size used for the "battery left" figure. The stock board has no battery; this is a what-if. |
| `heap_free_bytes` | 219,980 | Static free-heap figure reported as `heap`. Not modelled, copied from a verified build. |
| `rssi_dbm` | -60 | RSSI reported while WiFi is up. |

## Frame push time

`frameCost(hw, rawCoreMs, rows)` runs once per drawn frame. The simulator measures how long `cz_tick` plus
`cz_dirty_rows` took in wasm (`rawCoreMs`, wall time from `performance.now()`) and reads the inclusive row span
the core touched.

```
core  = rawCoreMs * core_calibration
bytes = rows * 240 * 2                              (RGB565, full width per row)
push  = bytes * 8 / spi_hz * 1000                   (ms on the SPI bus)
      + ceil(rows / chunk_rows) * chunk_overhead_us / 1000
frame = core + push
fps   = min(fps_cap, 1000 / frame)                  (0 when nothing was drawn)
```

With the defaults a full 240-row frame is 115,200 bytes: 23.04 ms on the bus plus 30 chunks x 40 us = 1.2 ms,
so `push` = 24.24 ms. The Nerd panel shows this as "full-frame push" whatever the last frame was. An 8-row blink
box is 3,840 bytes: 0.768 ms plus one chunk, 0.81 ms. This is why the core only redraws the rows that changed.

`fps` is a capacity figure (how many frames of that size would fit in a second), not frames actually drawn. Most
seconds the core draws far fewer because nothing changed.

Browsers coarsen `performance.now()` to about 0.1 ms and a wasm frame usually takes less than that, so single
`wasm raw` readings sit at 0 or 0.1 ms. Averages over many frames are usable, and the calibration below works on
the average.

## Per-second telemetry

`sim.js` keeps one bucket per simulated second and closes it into a row with the same leading keys as the
firmware's F3 line:

| Key | Meaning |
|---|---|
| `t` | Simulated ms at the start of the second. |
| `fps` | `min(fps_cap, 1000 / mean frame ms)` over the frames drawn that second, 0 if none. |
| `frame_ms`, `push_ms`, `core_ms` | Means over the frames drawn that second. |
| `rows` | Sum of the rows pushed that second. (The firmware reports the largest single frame instead.) |
| `heap` | `heap_free_bytes`, static. |
| `screen`, `hearts` | Core state at the end of the second. |
| `wifi`, `rssi` | 1 and `rssi_dbm` while connected and awake, else 0. (The firmware prints a state string for `wifi`.) |
| `spi_pct` | Total push ms that second / 10: the share of the second the SPI bus was busy. |
| `cpu_pct` | (core + push ms that second) / 10. |
| `ma` | Charge drawn that second in mA s, which over one second equals the average mA. |
| `mah` | Cumulative charge since reset, in mAh. |
| `avg_ma` | Rolling mean of `ma` over the last 3,600 s. In the panel, not in the CSV. |
| `battery_h` | `battery_mah / avg_ma`, infinite when no current. |
| `temp_c` | See below. |
| `power` | `awake`, `light` or `deep`. |

## Power and battery

`currentMa(hw, power, wifi, brightness)` gives the steady current for the state the simulator is in, and the
stepping loop integrates it over every slice of simulated time:

```
deep   = ma_deep_sleep
light  = ma_light_sleep
awake  = ma_mcu_active + ma_panel + ma_backlight_full * brightness / 100 + (wifi ? ma_wifi_idle : 0)
```

Defaults: 53 mA awake at full brightness with WiFi off, 83 mA with WiFi on, 30.5 mA at 10% brightness with WiFi
off. The Sleep screen with Host sleep set to None counts as awake. Both sleeps have the radio off.

Each sync adds one burst on top of idle: `(ma_wifi_tx_burst - ma_wifi_idle) * wifi_tx_burst_ms / 1000`, which is
54 mA s with the defaults, or about 0.9 mA averaged over a 60 s sync interval.

Battery hours = `battery_mah / avg_ma` using the rolling one-hour average, so the figure settles only after an
hour of simulated time in a steady state. The 72 h Forgotten scenario is the one that shows what deep sleep buys.

## Temperature

`tempC(hw, mA) = ambient_c + supply_v * mA / 1000 * thermal_c_per_w`, applied to a smoothed current rather than
the instantaneous one. The smoothing is a first-order filter updated once per second with a 60 s time constant
(`ema += (ma - ema) / 60`), a stand-in for the board's real thermal mass until someone measures a curve. At a
steady 83 mA the estimate is 42.45 C with the default ambient.

## Recalibrating from a real badge

The firmware prints one F3 JSON line per second on the serial port (`pio device monitor`), with keys `t`, `fps`,
`frame_ms`, `push_ms`, `rows`, `heap`, `screen`, `hearts`, `wifi`, `rssi`. `frame_ms` is tick + render + push
and `push_ms` is convert + SPI, so `frame_ms - push_ms` is the core's own time on the C3. The serial command
`stats` prints one line on demand.

1. Save the serial output to a file. The importer accepts `.txt`, `.log`, `.jsonl` and `.json` and keeps any line
   that parses as JSON with a numeric `frame_ms`; everything else (boot messages, prompts) is ignored.
2. In the simulator, draw a lot of frames first: run a scenario or interact for a while. The suggested factor
   divides the firmware's core time by the mean wasm raw time per drawn frame since the page loaded, and that
   mean needs many frames to be meaningful.
3. Nerd > Import firmware serial log. The panel reports how many lines were kept, the firmware's mean core ms over
   frames with `rows > 0`, the wasm mean, and the suggested `core_calibration`. Apply factor writes it (two
   decimals) into the constants. The imported `frame_ms` series is drawn in teal over the model's gold line on
   the chart, and the `est.` suffix disappears.
4. Check the push side by hand: compare the firmware's `push_ms` on full-frame seconds (`rows` = 240) with the
   model's "full-frame push". If they differ, adjust `spi_hz` for the bus rate and `chunk_overhead_us` for the
   per-chunk and conversion cost until they agree.
5. Current, temperature and sleep figures have no import path. Measure them with a USB power meter or a
   thermocouple and type them into the constants.

Edits in the Nerd panel live in memory only and reset on reload. Once a number is measured, put it in
`sim/hardware.json` and change the `profile` string so the next person knows it is no longer a guess.
