// Wasm variant of tests/bench_core.c: forced full redraws of Home and Talk in sim/core.wasm (built -O2).
// Run from the repo root after `make wasm`: node tests/bench_wasm.mjs
import { readFile } from 'node:fs/promises';

const N = 2000, RUNS = 5;
const mod = await WebAssembly.compile(await readFile(new URL('../sim/core.wasm', import.meta.url)));
const env = {};
for (const i of WebAssembly.Module.imports(mod)) if (i.kind === 'function') (env[i.module] ??= {})[i.name] = () => 0;
const { exports: cz } = await WebAssembly.instantiate(mod, env);

const str = s => { // strings go through the core's scratch buffer, as in sim/sim.js
  const p = cz.cz_scratch(), b = new TextEncoder().encode(s);
  new Uint8Array(cz.memory.buffer).set([...b, 0], p);
  return p;
};
const P0 = () => cz.cz_scratch() + 248, P1 = () => cz.cz_scratch() + 252;
let now = 1000;
const run = ms => { for (const end = now + ms; now < end;) cz.cz_tick(now += 50); };
const boot = () => { now = 1000; cz.cz_init(0, 0, 0, 0); cz.cz_tick(now); run(6000); };

function bench(name, screen) {
  if (cz.cz_screen() !== screen) throw new Error(`${name}: screen ${cz.cz_screen()}`);
  const t = [];
  for (let r = 0; r < RUNS; r++) {
    const t0 = process.hrtime.bigint();
    for (let i = 0; i < N; i++) { cz.cz_set_name(str('TENUK')); cz.cz_dirty_rows(P0(), P1()); }
    t.push(Number(process.hrtime.bigint() - t0) / 1e3 / N);
  }
  t.sort((a, b) => a - b);
  console.log(`${name.padEnd(8)} ${t[RUNS >> 1].toFixed(3).padStart(9)}`);
}

console.log(`Citizen core wasm bench (node ${process.version}): ${N} full frames x ${RUNS} runs, median us/frame`);
console.log(`${'case'.padEnd(8)} ${'wasm_us'.padStart(9)}`);
boot(); bench('home', 1);
boot(); cz.cz_message(str('What did you ship')); bench('talk', 4);
