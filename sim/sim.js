const SCREENS = ['HATCH','HOME','FEED','PLAY','TALK','QR','CHECKIN','SULK','SLEEP'];
const POSES = ['IDLE','BLINK','EAT','PLAY','SLEEP','SULK','TALK','CHEER'];
const W = 240, H = 240, NCOL = 11, SAVE_KEY = 'citizen-save';
const $ = id => document.getElementById(id);
const fail = e => { $('err').textContent = String(e); console.error(e); };

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

// Simulated clock (uint32 ms).
let simMs = 1000, last = performance.now(), speed = 1;
const now = () => simMs >>> 0;
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
}
const b64 = u8 => btoa(String.fromCharCode(...u8));
const unb64 = s => Uint8Array.from(atob(s), c => c.charCodeAt(0));
function doSave() {
  const n = cz.cz_save(cz.cz_scratch(), 64);
  if (!n) return fail('cz_save returned 0');
  const blob = mem().slice(cz.cz_scratch(), cz.cz_scratch() + n);
  try { localStorage.setItem(SAVE_KEY, b64(blob)); } catch (e) { fail(e); }
  return blob;
}

init(null, 0);
applyContent();

// Screen
const view = $('screen'), vctx = view.getContext('2d');
const off = document.createElement('canvas'); off.width = W; off.height = H;
const octx = off.getContext('2d'), img = octx.createImageData(W, H);
function present() {
  const fb = new Uint8Array(cz.memory.buffer, cz.cz_framebuffer(), W * H);
  const pal = new Uint16Array(cz.memory.buffer, cz.cz_palette(), NCOL);
  const rgb = Array.from(pal, v => [((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31]);
  const d = img.data;
  for (let i = 0, o = 0; i < fb.length; i++, o += 4) { const c = rgb[fb[i]] || rgb[0]; d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; d[o + 3] = 255; }
  octx.putImageData(img, 0, 0);
  vctx.imageSmoothingEnabled = false;
  vctx.drawImage(off, 0, 0, 720, 720);
}

// Pointer input -> 240x240
let down = false;
const pos = e => { const r = view.getBoundingClientRect(); return [Math.round((e.clientX - r.left) / r.width * W), Math.round((e.clientY - r.top) / r.height * H)]; };
view.addEventListener('pointerdown', e => { down = true; view.setPointerCapture(e.pointerId); cz.cz_touch(0, ...pos(e), now()); });
view.addEventListener('pointermove', e => { if (down) cz.cz_touch(1, ...pos(e), now()); });
const up = e => { if (down) { down = false; cz.cz_touch(2, ...pos(e), now()); } };
view.addEventListener('pointerup', up); view.addEventListener('pointercancel', up);

// Controls
$('speed').onchange = e => speed = +e.target.value;
$('creature').onchange = () => cz.cz_set_creature(+$('creature').value);
$('apply').onclick = applyContent;
$('send').onclick = () => withStr(cz.cz_message, $('msg').value);
$('checkin').onclick = () => withStr(cz.cz_checkin, 'KL');
$('save').onclick = doSave;
$('reset').onclick = () => { init(null, 0); applyContent(); };
$('wake').onclick = () => {
  const blob = doSave(); // saving first stands in for what the firmware did before sleeping
  if (blob) { init(blob, Math.max(0, +$('slept').value | 0)); applyContent(); }
};

const mmss = ms => { const s = Math.floor(ms / 1000); return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`; };
const hms = ms => { const s = Math.floor(ms / 1000); return `${Math.floor(s / 3600)}h ${String(Math.floor(s / 60) % 60).padStart(2, '0')}m ${String(s % 60).padStart(2, '0')}s`; };
let dirtyCount = 0, dirtyWindow = performance.now();
function frame(t) {
  simMs += (t - last) * speed; last = t;
  cz.cz_tick(now());
  if (cz.cz_dirty()) { present(); dirtyCount++; }
  if (t - dirtyWindow >= 1000) { $('r-fps').textContent = (dirtyCount * 1000 / (t - dirtyWindow)).toFixed(0); dirtyCount = 0; dirtyWindow = t; }
  $('r-time').textContent = hms(simMs - 1000);
  $('r-screen').textContent = SCREENS[cz.cz_screen()] ?? cz.cz_screen();
  $('r-hearts').textContent = cz.cz_hearts() + ' / 4';
  $('r-pose').textContent = POSES[cz.cz_pose()] ?? cz.cz_pose();
  $('r-hunger').textContent = mmss(cz.cz_hunger_ms());
  $('r-sleep').textContent = cz.cz_wants_sleep() ? 'yes' : 'no';
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
window.cz = cz; // for poking from the console
