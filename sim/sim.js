import { frameCost, currentMa, syncMas, tempC, batteryHours } from './model.js';

const SCREENS = ['HATCH','HOME','FEED','PLAY','TALK','QR','CHECKIN','SULK','SLEEP','SETTINGS','EGG','WIFI','PORTAL'];
const REQS = ['NONE','PORTAL','CHECK_UPDATE','INSTALL_UPDATE','FACTORY_RESET','SYNC','WIFI_FORGET'];
const POSES = ['IDLE','BLINK','EAT','PLAY','SLEEP','SULK','TALK','CHEER'];
const W = 240, H = 240, NCOL = 15 /* CZ_C_COUNT */, SAVE_KEY = 'citizen-save';
const STEP_MS = 1000 / 30; // the firmware loop runs at most 30 fps, so the core is ticked at that rate in sim time
const $ = id => document.getElementById(id);
const fail = e => { $('err').textContent = String(e); console.error(e); };

const hw = await fetch('hardware.json').then(r => r.json()).catch(fail);
if (!hw) throw new Error('hardware.json not loaded');
const mod = await WebAssembly.compileStreaming(fetch('core.wasm')).catch(fail);
if (!mod) throw new Error('core.wasm not loaded');
// Stub any imports the core might declare (freestanding build should have none).
const env = {};
for (const i of WebAssembly.Module.imports(mod)) if (i.kind === 'function') (env[i.module] ??= {})[i.name] = () => 0;
const { exports: cz } = await WebAssembly.instantiate(mod, env);
const mem = () => new Uint8Array(cz.memory.buffer);

// Strings go through the core's 256-byte scratch: max 255 bytes + NUL.
const enc = new TextEncoder();
function withStr(fn, s) {
  const b = enc.encode(s).slice(0, 255), p = cz.cz_scratch();
  mem().set(b, p); mem()[p + b.length] = 0;
  fn(p);
}

// ---- simulated host state
const sim = {
  ms: 1000, speed: 1, paused: false, fingerDown: false, swallow: false,
  power: 'awake', sleptMs: 0, blob: null,      // power: awake | light | deep
  wifi: false, nextSync: Infinity, syncs: 0, wakes: 0, pending: { msg: null, checkin: null },
  rate: 0, nextTap: Infinity, inGesture: false,
  scenario: null, end: Infinity,
  version: '1.1.0', updateAt: Infinity, nextClock: Infinity, note: '',
  nets: ['PromptKL', 'Home'],                 // the firmware's saved networks (NVS ssid0..4), fed to the WiFi screen
};
const now = () => sim.ms >>> 0;
const seed = () => (Math.random() * 2 ** 32) >>> 0;

function init(saved, slept) {
  const p = cz.cz_scratch() + 128; // blob staged away from string scratch use
  if (saved) mem().set(saved, p);
  cz.cz_init(seed(), saved ? p : 0, saved ? saved.length : 0, slept);
}
function applyContent() {
  cz.cz_set_creature(+$('creature').value);
  withStr(cz.cz_set_name, $('name').value);
  withStr(cz.cz_set_prompt, $('prompt').value);
  withStr(cz.cz_set_qr_url, $('qr').value);
  applyHost();
}
// What the real firmware tells the core after every init: running version and WiFi state (cz_set_wifi/cz_set_version).
function applyHost() {
  withStr(cz.cz_set_version, sim.version);
  withStr(p => cz.cz_set_wifi(p, sim.wifi ? 2 : 0), 'PromptKL');
  withStr(cz.cz_set_wifi_list, sim.nets.join('\n'));
}
const creaturesMask = () => [...document.querySelectorAll('[data-creature]')].reduce((m, c) => m | (c.checked ? 1 << +c.dataset.creature : 0), 0);
// ---- badge clock: the core advances it from cz_tick; the host only sets it
const clockMin = () => { const [h, m] = $('clock').value.split(':').map(Number); return (h * 60 + m) || 0; };
function setClock(min) { $('clock').value = `${String(Math.floor(min / 60)).padStart(2, '0')}:${String(min % 60).padStart(2, '0')}`; cz.cz_set_clock(min); }
function syncComputerClock() { const d = new Date(); setClock(d.getHours() * 60 + d.getMinutes()); }
function updateClockMode() { sim.nextClock = $('useclock').checked ? sim.ms : Infinity; }
const b64 = u8 => btoa(String.fromCharCode(...u8));
function saveBlob() {
  const n = cz.cz_save(cz.cz_scratch(), 64);
  if (!n) { fail('cz_save returned 0'); return null; }
  return mem().slice(cz.cz_scratch(), cz.cz_scratch() + n);
}
function doSave() {
  const blob = saveBlob();
  if (blob) try { localStorage.setItem(SAVE_KEY, b64(blob)); } catch (e) { fail(e); }
  return blob;
}

// ---- telemetry: one bucket per simulated second, as the firmware's F3 line
const COLS = ['t','fps','frame_ms','push_ms','rows','heap','screen','hearts','wifi','rssi','core_ms','spi_pct','cpu_pct','ma','mah','battery_h','temp_c','power'];
let bucket, tele, hourMa, last, lastFrame, recent, record, recording = false, rawCore = { sum: 0, n: 0 };
let calibrated = false, imported = [], needPresent = true;
function resetTelemetry() {
  bucket = { end: Math.floor(sim.ms / 1000) * 1000 + 1000, frames: 0, core: 0, push: 0, frame: 0, rows: 0, mAs: 0 };
  tele = { mAs: 0, ema: null };
  hourMa = { v: new Float64Array(3600), i: 0, n: 0, sum: 0 };
  last = null; lastFrame = null; recent = []; record = []; sim.syncs = 0; sim.wakes = 0;
}
function closeBucket() {
  const b = bucket, n = b.frames, mA = b.mAs; // bucket is 1 s, so mA*s == average mA
  tele.mAs += mA;
  const h = hourMa; h.sum += mA - h.v[h.i]; h.v[h.i] = mA; h.i = (h.i + 1) % 3600; h.n = Math.min(3600, h.n + 1);
  tele.ema = tele.ema == null ? mA : tele.ema + (mA - tele.ema) / 60; // shortcut: 60 s first-order thermal lag, replace with a measured curve
  const avgMa = h.sum / h.n, frameMs = n ? b.frame / n : 0, on = sim.wifi && sim.power === 'awake';
  last = {
    t: Math.round(b.end - 1000), fps: n ? Math.min(hw.fps_cap, 1000 / frameMs) : 0, frame_ms: frameMs,
    push_ms: n ? b.push / n : 0, rows: b.rows, heap: hw.heap_free_bytes, screen: cz.cz_screen(), hearts: cz.cz_hearts(),
    wifi: on ? 1 : 0, rssi: on ? hw.rssi_dbm : 0, core_ms: n ? b.core / n : 0, spi_pct: b.push / 10, cpu_pct: (b.core + b.push) / 10,
    ma: mA, mah: tele.mAs / 3600, avg_ma: avgMa, battery_h: batteryHours(hw, avgMa), temp_c: tempC(hw, tele.ema), power: sim.power,
  };
  recent.push(last); if (recent.length > 300) recent.shift();
  if (recording) record.push(last);
  bucket = { end: b.end + 1000, frames: 0, core: 0, push: 0, frame: 0, rows: 0, mAs: 0 };
}

// ---- one firmware loop iteration: tick, render, model the push of the dirty rows
const P0 = () => cz.cz_scratch() + 248, P1 = () => cz.cz_scratch() + 252;
function coreFrame() {
  const t0 = performance.now();
  cz.cz_tick(now());
  const d = cz.cz_dirty_rows(P0(), P1());
  const raw = performance.now() - t0;
  if (d) {
    const dv = new DataView(cz.memory.buffer), rows = dv.getInt32(P1(), true) - dv.getInt32(P0(), true) + 1;
    const f = frameCost(hw, raw, rows);
    rawCore.sum += raw; rawCore.n++;
    lastFrame = { ...f, raw };
    if (rows === H) sim.lastFull = lastFrame;
    bucket.frames++; bucket.core += f.core; bucket.push += f.push; bucket.frame += f.frame; bucket.rows += rows;
    needPresent = true;
  }
  for (let q; (q = cz.cz_host_request()); ) {
    sim.note = REQS[q] ?? String(q);
    if (q === 1) { sim.note += ': portal open (simulated)'; withStr(cz.cz_portal, 'CITIZEN-0A1B2C'); $('leaveportal').hidden = false; }
    else if (q === 5) { sync(); if ($('live').checked) poll(); applyHost(); } // the poll ends with cz_set_wifi, which clears SYNCING
    else if (q === 6) { const i = cz.cz_wifi_forget(); if (i >= 0) sim.nets.splice(i, 1); applyHost(); }
    else if (q === 2) sim.updateAt = sim.ms + 2000;
    else if (q === 3) { sim.note += ': would reboot into 1.1.1'; sim.version = '1.1.1'; withStr(cz.cz_set_version, sim.version); withStr(cz.cz_set_update, ''); }
  }
  if (sim.ms >= sim.updateAt) { sim.updateAt = Infinity; withStr(cz.cz_set_update, '1.1.1'); }
  if (sim.power === 'awake' && cz.cz_wants_sleep()) {
    const mode = $('sleepmode').value;
    if (mode === 'deep') { sim.blob = saveBlob(); sim.sleptMs = 0; sim.power = 'deep'; sim.nextSync = Infinity; needPresent = true; }
    else if (mode === 'light') { sim.power = 'light'; sim.nextSync = Infinity; needPresent = true; } // radio off in both sleeps
  }
}

// ---- event-driven stepping in simulated time
function advanceTo(target) {
  while (sim.ms < target) {
    const end = Math.max(sim.ms, Math.min(target, bucket.end, sim.ms + STEP_MS, sim.nextTap, sim.nextSync, sim.nextClock, sim.updateAt));
    bucket.mAs += currentMa(hw, sim.power, sim.wifi, cz.cz_brightness()) * (end - sim.ms) / 1000;
    if (sim.power === 'deep') sim.sleptMs += end - sim.ms;
    sim.ms = end;
    if (sim.power !== 'deep') coreFrame(); // deep sleep: the core is off; light sleep keeps timers running
    if (sim.ms >= sim.nextTap) {
      if (!sim.inGesture) { if (sim.power !== 'awake') wake(); gesture('tap'); }
      scheduleTap();
    }
    if (sim.ms >= sim.nextClock) { syncComputerClock(); sim.nextClock += 60000; }
    while (sim.ms >= sim.nextSync) { sync(); sim.nextSync += Math.max(1, hw.sync_interval_s) * 1000; }
    if (sim.ms >= bucket.end) closeBucket();
  }
}
function scheduleTap() { // Poisson arrivals at the scenario's interaction rate
  sim.nextTap = sim.rate > 0 ? sim.ms + -Math.log(1 - Math.random()) * 3600000 / sim.rate : Infinity;
}

// ---- gestures: raw touch sequences with real-time spacing (S2, S3)
const GESTURES = {
  tap: [[0, 120, 150, 0], [2, 120, 150, 80]],
  swipe: [[0, 70, 150, 0], [1, 110, 150, 60], [1, 170, 150, 120], [2, 170, 150, 160]],
  hold: [[0, 120, 150, 0], [2, 120, 150, 1100]],
  double: [[0, 120, 150, 0], [2, 120, 150, 80], [0, 120, 150, 150], [2, 120, 150, 80]],
};
function gesture(name) {
  if (sim.power !== 'awake') return wake(); // the waking touch is consumed, as on hardware
  sim.inGesture = true;
  for (const [k, x, y, dt] of GESTURES[name]) { advanceTo(sim.ms + dt); cz.cz_touch(k, x, y, now()); }
  sim.inGesture = false;
}
function wake() {
  if (sim.power === 'deep') { init(sim.blob, Math.round(sim.sleptMs / 1000)); applyContent(); }
  else if (sim.power === 'light') { cz.cz_touch(0, 120, 150, now()); cz.cz_touch(2, 120, 150, now()); } // Sleep -> Home
  sim.power = 'awake'; sim.wakes++; needPresent = true;
  connect();
}

// ---- WiFi (S8 = F6): one sync at boot and at every wake, then every sync_interval_s (60) while awake, none asleep
function connect() {
  sim.nextSync = sim.wifi && sim.power === 'awake' ? sim.ms + hw.sync_interval_s * 1000 : Infinity;
  sync();
}
function setWifi(on) {
  sim.wifi = on; $('wifi').checked = on; applyHost();
  connect();
}
function sync() {
  if (!sim.wifi || sim.power !== 'awake') return;
  bucket.mAs += syncMas(hw); sim.syncs++;
  if (cz.cz_screen() === 0) return; // the core ignores content during Hatch; keep it queued
  if (sim.pending.msg != null) { withStr(cz.cz_message, sim.pending.msg); sim.pending.msg = null; }
  if (sim.pending.checkin != null) { withStr(cz.cz_checkin, sim.pending.checkin); sim.pending.checkin = null; }
}
let live = {}, liveTimer = 0;
async function poll() {
  try {
    const r = await fetch($('liveurl').value.replace(/\/+$/, '') + '/state', { cache: 'no-store' });
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const j = await r.json();
    const deliver = (key, fn, v) => {
      if (typeof v !== 'string' || !v || v === live[key]) return;
      live[key] = v; v = v.slice(0, 64);
      if (sim.power === 'awake' && cz.cz_screen() !== 0) withStr(fn, v); else sim.pending[key] = v;
    };
    deliver('msg', cz.cz_message, j.msg);
    deliver('checkin', cz.cz_checkin, j.checkin);
    $('wifistatus').textContent = 'live: ok ' + new Date().toLocaleTimeString();
  } catch (e) { $('wifistatus').textContent = 'live: ' + e.message; }
}

// ---- scenarios (S11)
const SCENARIOS = {
  desk: { label: 'Desk day', clock: '09:00', wifi: false, rate: 10, hours: 8, usb: true },
  event: { label: 'Event day', clock: '10:00', wifi: true, rate: 60, hours: 10 }, // syncs follow F6: one per wake plus every 60 s awake
  forgotten: { label: 'Forgotten', clock: '20:00', wifi: false, rate: 0, hours: 72 },
};
function reset() {
  sim.version = '1.1.0'; sim.updateAt = Infinity; sim.note = '';
  init(null, 0); applyContent();
  if ($('useclock').checked) syncComputerClock(); else cz.cz_set_clock(clockMin());
  updateClockMode();
  sim.power = 'awake'; sim.scenario = null; sim.end = Infinity; sim.pending = { msg: null, checkin: null };
  resetTelemetry(); needPresent = true;
  connect(); // F6 boot sync
}
function runScenario(key) {
  const s = SCENARIOS[key];
  hw.sync_interval_s = 60; $('sync').value = 60; // F6
  sim.wifi = $('wifi').checked = s.wifi;
  $('clock').value = s.clock; $('useclock').checked = false;
  reset();
  $('sleepmode').value = 'deep';
  sim.rate = s.rate; $('rate').value = s.rate; scheduleTap();
  sim.scenario = s; sim.end = sim.ms + s.hours * 3600e3;
  setSpeed(3600); sim.paused = false;
  recording = true; record = [];
}
function setSpeed(v) { sim.speed = v; $('speed').value = v; }

// ---- screen (S1)
const view = $('screen'), vctx = view.getContext('2d');
const off = document.createElement('canvas'); off.width = W; off.height = H;
const octx = off.getContext('2d'), img = octx.createImageData(W, H);
function present() {
  needPresent = false;
  vctx.imageSmoothingEnabled = false;
  // Real badge: backlight off and panel asleep in host sleep. At 1x we show that. When time is accelerated the
  // sleep/wake cycle would strobe the screen, so we keep the dimmed Sleep frame visible at 10% instead.
  const asleep = sim.power !== 'awake';
  if (asleep && sim.speed <= 1) { vctx.fillStyle = '#000'; vctx.fillRect(0, 0, 720, 720); badge3d?.update(0); return; }
  const level = asleep ? 10 : cz.cz_brightness();
  const fb = new Uint8Array(cz.memory.buffer, cz.cz_framebuffer(), W * H);
  const pal = new Uint16Array(cz.memory.buffer, cz.cz_palette(), NCOL);
  const rgb = Array.from(pal, v => [((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31]);
  const d = img.data;
  for (let i = 0, o = 0; i < fb.length; i++, o += 4) { const c = rgb[fb[i]] || rgb[0]; d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; d[o + 3] = 255; }
  octx.putImageData(img, 0, 0);
  badge3d?.update(level); // after putImageData so the texture upload sees this frame
  vctx.drawImage(off, 0, 0, 720, 720);
  // shortcut: CSS approximation of backlight PWM, floored so 10% stays readable on a monitor
  view.style.filter = `brightness(${0.35 + 0.65 * level / 100})`;
}

// ---- 3D view: loaded on first use so the 2D sim never depends on the CDN
const VIEW_KEY = 'citizen-view';
let badge3d = null;
async function setView3d(on) {
  if (on && !badge3d) {
    try { badge3d = (await import('./badge3d.js')).create($('badge3d'), off); needPresent = true; }
    catch (e) { fail(e); on = false; }
  }
  badge3d?.setActive(on);
  $('bezel').hidden = on; $('badge3d').hidden = !on; $('note3d').hidden = !on;
  $('view3d').textContent = on ? '2D' : '3D'; $('view3d').setAttribute('aria-pressed', on);
}
$('view3d').onclick = () => {
  const on = $('badge3d').hidden;
  try { localStorage.setItem(VIEW_KEY, on ? '3d' : '2d'); } catch {}
  setView3d(on);
};

// Pointer input -> 240x240. Gesture timing runs in real time: the clock is held at 1x while a finger is down.
const pos = e => { const r = view.getBoundingClientRect(); return [Math.round((e.clientX - r.left) / r.width * W), Math.round((e.clientY - r.top) / r.height * H)]; };
view.addEventListener('pointerdown', e => {
  view.setPointerCapture(e.pointerId); sim.fingerDown = true;
  if (sim.power !== 'awake') { wake(); sim.swallow = true; return; }
  cz.cz_touch(0, ...pos(e), now());
});
view.addEventListener('pointermove', e => { if (sim.fingerDown && !sim.swallow) cz.cz_touch(1, ...pos(e), now()); });
const up = e => {
  if (!sim.fingerDown) return;
  sim.fingerDown = false;
  if (!sim.swallow) cz.cz_touch(2, ...pos(e), now());
  sim.swallow = false;
};
view.addEventListener('pointerup', up); view.addEventListener('pointercancel', up);

// ---- controls
document.querySelectorAll('[data-g]').forEach(b => b.onclick = () => gesture(b.dataset.g));
document.querySelectorAll('[data-sc]').forEach(b => b.onclick = () => runScenario(b.dataset.sc));
$('pause').onclick = () => { sim.paused = !sim.paused; };
$('step').onclick = () => advanceTo(sim.ms + 1000);
$('jump').onclick = () => advanceTo(sim.ms + 3600e3);
$('speed').onchange = e => sim.speed = +e.target.value;
$('rate').onchange = e => { sim.rate = Math.max(0, +e.target.value || 0); scheduleTap(); };
$('creature').onchange = () => cz.cz_set_creature(+$('creature').value);
document.querySelectorAll('[data-creature]').forEach(c => c.onchange = () => { const m = creaturesMask(); if (m) cz.cz_set_creatures(m); });
$('leaveportal').onclick = () => { withStr(cz.cz_portal, ''); $('leaveportal').hidden = true; };
$('bright').onchange = e => cz.cz_set_brightness(+e.target.value);
$('apply').onclick = applyContent;
$('send').onclick = () => withStr(cz.cz_message, $('msg').value);
$('checkin').onclick = () => withStr(cz.cz_checkin, 'KL');
$('save').onclick = doSave;
$('reset').onclick = reset;
$('wake').onclick = () => {
  const blob = doSave(); // saving first stands in for what the firmware did before sleeping
  if (blob) { init(blob, Math.max(0, +$('slept').value | 0)); applyContent(); sim.power = 'awake'; sim.wakes++; needPresent = true; connect(); }
};
$('clock').onchange = () => { $('useclock').checked = false; updateClockMode(); cz.cz_set_clock(clockMin()); };
$('useclock').onchange = () => { updateClockMode(); if ($('useclock').checked) syncComputerClock(); };
$('tz').onchange = () => cz.cz_set_timezone(+$('tz').value);
$('wifi').onchange = e => setWifi(e.target.checked);
$('sync').onchange = e => { hw.sync_interval_s = Math.max(1, +e.target.value || 60); if (sim.wifi) setWifi(true); };
$('queue').onclick = () => { sim.pending.msg = $('webmsg').value.slice(0, 64); };
$('queuecheckin').onclick = () => { sim.pending.checkin = 'KL'; };
$('live').onchange = e => { clearInterval(liveTimer); live = {}; if (e.target.checked) { poll(); liveTimer = setInterval(poll, 10000); } };
$('nerd').onclick = () => { const p = $('nerdpanel'); p.hidden = !p.hidden; $('nerd').classList.toggle('on', !p.hidden); };
$('record').onclick = () => { recording = !recording; if (recording) record = []; };

// ---- record / export / import (S10)
const fmtCell = v => typeof v === 'number' ? (Number.isFinite(v) ? +v.toFixed(3) : '') : v;
const toCsv = rows => [COLS.join(','), ...rows.map(r => COLS.map(c => fmtCell(r[c])).join(','))].join('\n');
$('export').onclick = () => {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([toCsv(record.length ? record : recent)], { type: 'text/csv' }));
  a.download = 'citizen-telemetry.csv'; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
};
const avg = a => a.reduce((s, v) => s + v, 0) / a.length;
function importLog(text) {
  const lines = text.split(/\r?\n/).map(l => { try { const j = JSON.parse(l); return j && typeof j.frame_ms === 'number' ? j : null; } catch { return null; } }).filter(Boolean);
  if (!lines.length) { $('calib').textContent = 'No F3 JSON lines found.'; return; }
  imported = lines; calibrated = true;
  const drawn = lines.filter(l => l.rows > 0 && l.frame_ms > 0 && typeof l.push_ms === 'number');
  const fwCore = drawn.length ? avg(drawn.map(l => l.frame_ms - l.push_ms)) : NaN, wasmCore = rawCore.sum / rawCore.n;
  const factor = fwCore / wasmCore;
  $('calib').innerHTML = '';
  $('calib').append(`${lines.length} lines imported. Firmware core ${fwCore.toFixed(2)} ms vs wasm ${wasmCore.toFixed(3)} ms per drawn frame: suggested factor ${Number.isFinite(factor) ? factor.toFixed(2) : 'n/a'}. `);
  if (Number.isFinite(factor) && factor > 0) {
    const b = document.createElement('button'); b.textContent = 'Apply factor';
    b.onclick = () => { hw.core_calibration = +factor.toFixed(2); buildFields(); };
    $('calib').append(b);
  }
}
$('import').onchange = async e => { const f = e.target.files[0]; if (f) importLog(await f.text()); };

// ---- nerd panel (S9)
function buildFields() {
  $('hwfields').innerHTML = '';
  for (const [k, v] of Object.entries(hw)) {
    if (typeof v !== 'number' || k === 'sync_interval_s') continue; // sync interval lives in the WiFi panel
    const l = document.createElement('label'), i = document.createElement('input');
    l.textContent = k; i.type = 'number'; i.step = 'any'; i.value = v;
    i.onchange = () => { if (Number.isFinite(+i.value)) hw[k] = +i.value; };
    l.append(i); $('hwfields').append(l);
  }
}
buildFields();
const est = (v, unit, dp = 1) => v == null || Number.isNaN(v) ? '-' : `${Number.isFinite(v) ? v.toFixed(dp) : 'inf'} ${unit}${calibrated ? '' : ' est.'}`;
function teleRows() {
  const f = lastFrame || {}, s = last || {};
  return [
    ['fps', est(f.fps, '', 0)], ['frame', est(f.frame, 'ms', 2)], ['core', est(f.core, 'ms', 2)], ['push', est(f.push, 'ms', 2)],
    ['dirty rows', f.rows == null ? '-' : `${f.rows} (${f.bytes} B)`], ['SPI util', est(s.spi_pct, '%')], ['CPU', est(s.cpu_pct, '%')],
    ['current', est(s.ma, 'mA', 2)], ['avg last hour', est(s.avg_ma, 'mA', 3)], ['used', est(s.mah, 'mAh', 2)],
    ['battery left', est(s.battery_h, `h on ${hw.battery_mah} mAh`)], ['temperature', est(s.temp_c, '°C')],
    ['heap free', `${hw.heap_free_bytes} B (static)`], ['syncs', String(sim.syncs)], ['wasm raw', f.raw == null ? '-' : f.raw.toFixed(3) + ' ms'],
    ['full-frame push', est(frameCost(hw, 0, H).push, 'ms', 2)],
  ];
}
function drawChart() {
  const c = $('chart'), g = c.getContext('2d'), w = c.width, h = c.height;
  const a = recent.map(r => r.frame_ms), b = imported.slice(-300).map(r => r.frame_ms);
  const max = Math.max(40, ...a, ...b);
  g.clearRect(0, 0, w, h);
  g.strokeStyle = '#2a313c'; g.beginPath(); const y33 = h - 33 / max * h; g.moveTo(0, y33); g.lineTo(w, y33); g.stroke();
  for (const [s, col] of [[a, '#e8b04a'], [b, '#3fb6a8']]) {
    g.strokeStyle = col; g.beginPath();
    s.forEach((v, i) => g[i ? 'lineTo' : 'moveTo'](i / 299 * w, h - v / max * h));
    g.stroke();
  }
}

// ---- readouts and main loop
const hms = ms => { const s = Math.floor(ms / 1000); return `${Math.floor(s / 3600)}h ${String(Math.floor(s / 60) % 60).padStart(2, '0')}m ${String(s % 60).padStart(2, '0')}s`; };
const mmss = ms => { const s = Math.floor(ms / 1000); return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`; };
let uiAt = 0;
function ui(t) {
  $('r-time').textContent = hms(sim.ms - 1000);
  $('r-screen').textContent = sim.power === 'deep' ? 'off (deep sleep)' : SCREENS[cz.cz_screen()] ?? cz.cz_screen();
  $('r-hearts').textContent = cz.cz_hearts() + ' / 4';
  $('r-pose').textContent = POSES[cz.cz_pose()] ?? cz.cz_pose();
  $('r-hunger').textContent = mmss(cz.cz_hunger_ms());
  $('r-power').textContent = sim.power + (sim.wifi ? ', WiFi' : '') + (sim.scenario?.usb ? ', USB powered' : '');
  const bc = cz.cz_clock(); $('r-clock').textContent = bc < 0 ? 'unknown' : `${String(Math.floor(bc / 60)).padStart(2, '0')}:${String(bc % 60).padStart(2, '0')}`;
  $('r-bright').textContent = cz.cz_brightness() + '%';
  const sc = sim.scenario;
  $('status').textContent = `${sim.paused ? 'paused' : (sim.fingerDown ? 1 : sim.speed) + 'x'}` +
    (sc ? ` · ${sc.label} ${hms(sim.ms - (sim.end - sc.hours * 3600e3))} / ${sc.hours}h` : '') +
    (recording ? ` · recording ${record.length} s` : '') + (sim.pending.msg != null ? ' · message queued' : '') + (sim.note ? ` · request ${sim.note}` : '');
  if (t - uiAt < 200 || $('nerdpanel').hidden) return;
  uiAt = t;
  $('tele').innerHTML = teleRows().map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
  drawChart();
}
let lastT = performance.now();
function frame(t) {
  const dt = Math.min(t - lastT, 250); lastT = t; // clamp so a backgrounded tab does not jump hours
  if (!sim.paused || sim.fingerDown) advanceTo(Math.min(sim.ms + dt * (sim.fingerDown ? 1 : sim.speed), sim.end));
  if (sim.ms >= sim.end && !sim.paused) { sim.paused = true; recording = false; }
  if (needPresent) present();
  ui(t);
  requestAnimationFrame(frame);
}

reset();
// URL: ?scenario=desk|event|forgotten&speed=N&view=3d opens straight into a running demo
const qs = new URLSearchParams(location.search);
if (SCENARIOS[qs.get('scenario')]) runScenario(qs.get('scenario'));
const qSpeed = +qs.get('speed');
if (qs.has('speed') && qSpeed > 0 && Number.isFinite(qSpeed)) {
  if (![...$('speed').options].some(o => +o.value === qSpeed)) $('speed').add(new Option(qSpeed + 'x', qSpeed));
  setSpeed(qSpeed);
}
let storedView = null; try { storedView = localStorage.getItem(VIEW_KEY); } catch {}
if ((qs.get('view') ?? storedView) === '3d') setView3d(true);
if (qs.has('selftest')) selftest();
requestAnimationFrame(frame);
window.cz = cz; window.sim = { sim, hw, gesture, advanceTo, runScenario, toCsv: () => toCsv(record), importLog };

// ?selftest: drives the acceptance checks headlessly and prints the numbers.
function selftest() {
  const r = {}, scr = () => SCREENS[cz.cz_screen()];
  advanceTo(sim.ms + 20000); r.boot = scr();
  r.full_frame = sim.lastFull && { rows: sim.lastFull.rows, push_ms: +sim.lastFull.push.toFixed(2), core_ms: +sim.lastFull.core.toFixed(3) };
  gesture('tap'); r.tap = scr(); advanceTo(sim.ms + 3500);
  gesture('swipe'); r.swipe = scr(); advanceTo(sim.ms + 3500);
  gesture('hold'); r.hold = scr();
  gesture('tap'); r.tap_on_qr = scr(); advanceTo(sim.ms + 3500);
  gesture('double'); advanceTo(sim.ms + 500); r.double = scr(); r.settings_open = cz.cz_screen() === 9;
  const b0 = cz.cz_brightness(); gesture('swipe'); advanceTo(sim.ms + 500); r.swipe_brightness = `${b0}->${cz.cz_brightness()}`;
  gesture('hold'); advanceTo(sim.ms + 500); r.settings_exit = scr();
  const fbh = () => { const f = new Uint8Array(cz.memory.buffer, cz.cz_framebuffer(), W * H); let h = 0; for (let i = 0; i < f.length; i++) h = (Math.imul(h, 31) + f[i]) | 0; return h; };
  const px = () => new Uint8Array(cz.memory.buffer, cz.cz_framebuffer(), W * H)[20 * W + 20];
  cz.cz_set_clock(720); advanceTo(sim.ms + 500); const noon = px(); cz.cz_set_clock(180); advanceTo(sim.ms + 500); const night = px();
  r.day_night_px20 = { noon, night, differ: noon !== night };
  // Home breath steps every 600 ms (PRD 2.6), so 250 ms samples repeat; assert 4 consecutive animation steps by waiting for each change.
  const hs = [fbh()]; for (let i = 0; i < 4; i++) { for (let k = 0; k < 60 && fbh() === hs[i]; k++) advanceTo(sim.ms + 50); hs.push(fbh()); }
  const raw = []; for (let i = 0; i < 4; i++) { advanceTo(sim.ms + 250); raw.push(fbh()); }
  r.home_frames = { screen: scr(), steps_differ: hs.every((h, i) => !i || h !== hs[i - 1]), sampled_250ms: raw, sampled_250ms_all_distinct: new Set(raw).size === 4 };
  const summary = () => ({ fps: +lastFrame.fps.toFixed(1), frame_ms: +lastFrame.frame.toFixed(2), push_ms: +lastFrame.push.toFixed(2),
    rows: lastFrame.rows, avg_ma_last_hour: +last.avg_ma.toFixed(3), mah: +last.mah.toFixed(2), battery_h: +last.battery_h.toFixed(1),
    temp_c: +last.temp_c.toFixed(1), syncs: sim.syncs, power: sim.power });
  let w = performance.now(); runScenario('forgotten'); advanceTo(sim.end);
  r.forgotten_72h = { ...summary(), battery_weeks: +(last.battery_h / 168).toFixed(1), wall_ms: Math.round(performance.now() - w) };
  w = performance.now(); runScenario('event'); advanceTo(sim.ms + 3600e3);
  r.event_day_1h = { ...summary(), syncs_per_h: sim.syncs, wakes_per_h: sim.wakes, wall_ms: Math.round(performance.now() - w) };
  const csv = toCsv(record).split('\n'); r.csv = { header: csv[0], rows: csv.length - 1 };
  if (sim.power !== 'awake') wake();
  // unlocking a creature hatches it: EGG, then Home with that creature
  const owned = [...document.querySelectorAll('[data-creature]')], before = cz.cz_creature(), gain = (before + 1) % 3;
  owned.forEach(c => c.checked = +c.dataset.creature === before || +c.dataset.creature === gain); owned[gain].onchange();
  const egg = scr(); advanceTo(sim.ms + 4500);
  r.unlock = { egg, after: scr(), creature: cz.cz_creature(), ok: egg === 'EGG' && cz.cz_creature() === gain };
  if (sim.power !== 'awake') wake();
  $('sleepmode').value = 'none'; // keep the radio up so the next periodic sync happens
  sim.pending.msg = 'hello from web'; const t0 = sim.ms; advanceTo(sim.nextSync + 1);
  r.queued_msg = { screen: scr(), after_s: Math.round((sim.ms - t0) / 1000) };
  importLog(record.filter(x => x.rows).slice(0, 50).map(x => JSON.stringify({ ...x, frame_ms: x.push_ms + 2 })).join('\n'));
  r.import = { lines: imported.length, calibrated, text: $('calib').firstChild?.textContent };
  if (sim.power !== 'awake') wake(); sim.paused = true; // leave the badge visible for the screenshot
  $('nerdpanel').hidden = false; $('nerd').classList.add('on');
  $('selftest').hidden = false; $('selftest').textContent = 'SELFTEST ' + JSON.stringify(r, null, 1);
  console.log('SELFTEST ' + JSON.stringify(r));
  layout400();
}
// S12: load the page in a 400 px iframe and check nothing overflows and every gesture button shows its full label.
function layout400() {
  const f = document.createElement('iframe');
  f.style.cssText = 'width:400px;height:900px;border:0;position:absolute;left:-9999px';
  f.onload = () => setTimeout(() => {
    const d = f.contentDocument, sw = d.body.scrollWidth;
    const clipped = [...d.querySelectorAll('[data-g]')].filter(b => b.scrollWidth > b.clientWidth || b.getBoundingClientRect().right > d.documentElement.clientWidth).map(b => b.textContent);
    const rows = new Set([...d.querySelectorAll('[data-g]')].map(b => Math.round(b.getBoundingClientRect().top))).size;
    const r = { scrollWidth: sw, ok: sw <= 400 && !clipped.length, gesture_rows: rows, clipped };
    f.remove();
    $('selftest').textContent += '\nSELFTEST_400 ' + JSON.stringify(r);
    console.log('SELFTEST_400 ' + JSON.stringify(r));
  }, 500);
  f.src = location.pathname; // no ?selftest, so the iframe only lays out
  document.body.append(f);
}
