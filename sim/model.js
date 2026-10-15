// Hardware model (PRD S6, S7). Pure functions over a hardware.json profile.
// Every result is an estimate with explicit constants, to be calibrated from firmware F3 telemetry.

// One drawn frame: wasm time scaled to the C3, plus SPI push of the dirty rows in chunk_rows-row chunks.
export function frameCost(hw, rawCoreMs, rows) {
  const core = rawCoreMs * hw.core_calibration;
  const bytes = rows * 240 * 2;
  const chunks = Math.ceil(rows / hw.chunk_rows);
  const push = bytes * 8 / hw.spi_hz * 1000 + chunks * hw.chunk_overhead_us / 1000;
  const frame = core + push;
  return { core, push, frame, bytes, rows, fps: rows ? Math.min(hw.fps_cap, 1000 / frame) : 0 };
}

// Steady current for a power state: 'awake' (screen on, incl. the Sleep screen when the host does not sleep),
// 'light' (F4 fallback) or 'deep'. Radio is off in both sleeps.
export function currentMa(hw, power, wifi, brightness) {
  if (power === 'deep') return hw.ma_deep_sleep;
  if (power === 'light') return hw.ma_light_sleep;
  return hw.ma_mcu_active + hw.ma_panel + hw.ma_backlight_full * brightness / 100 + (wifi ? hw.ma_wifi_idle : 0);
}

// Extra charge (mA*s) of one sync: the TX burst replaces wifi idle for wifi_tx_burst_ms.
export const syncMas = hw => (hw.ma_wifi_tx_burst - hw.ma_wifi_idle) * hw.wifi_tx_burst_ms / 1000;
export const tempC = (hw, mA) => hw.ambient_c + hw.supply_v * mA / 1000 * hw.thermal_c_per_w;
export const batteryHours = (hw, avgMa) => avgMa > 0 ? hw.battery_mah / avgMa : Infinity;
