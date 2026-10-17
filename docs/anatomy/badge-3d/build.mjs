// Fig 3D: the Citizen badge (DIYMORE ESP32-2424S012C in its case) on a turntable, taking itself apart.
// node build.mjs [--light] [--verify] [--audit [--quick] [--only=a,b]]  →  badge-3d.html / badge-3d-light.html
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import * as k from "../kit/iso-kit.mjs";
import * as G from "../kit/lathe.mjs";
import * as A from "../kit/audit.mjs";
import { turning } from "../kit/turn-build.mjs";
import * as TA from "../kit/turn-audit.mjs";
import { turnScript } from "../scripts/inline-kit.mjs";
import { M, follow, PIECES, ORDER, DETAIL } from "./model.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const theme = argv.includes("--light") ? "light" : "dark";
const verify = argv.includes("--verify");

const WIDTH = 680;
const HEIGHT = 560;
const AZ = M.azimuth;
const D2R = Math.PI / 180;

// ---- the badge frame: s along the face normal (0 = back face), x up, y across --------------------------------------
const { N, UP, V, B0 } = M;
const at3 = (s, x, y) => G.add3(G.add3(G.add3(B0, G.mul3(N, s)), G.mul3(UP, x)), G.mul3(V, y));
const FB = { o: B0, a: N, u: UP, v: V };
const frameOf = (o, a, u) => {
  const axis = G.unit3(a);
  const uu = G.unit3(G.sub3(u, G.mul3(axis, G.dot3(u, axis))));
  return { o, a: axis, u: uu, v: G.cross3(axis, uu) };
};
const ALONG = (x, y) => ({ o: at3(0, x, y), a: N, u: UP, v: V });
const VERT = (x, y) => G.frameOf([x, y, 0], [0, 0, 1], [1, 0, 0], [0, 1, 0]);

// ---- swept envelope for the camera fit: the stack apart at every angle, the turntable base ----------------------------
const sweep = [];
for (let a = 0; a < 360; a += 15) {
  const c = Math.cos(a * D2R);
  const s = Math.sin(a * D2R);
  for (const [r, z] of [[M.reach, M.cz + 92], [M.reach, M.cz - 92], [M.baseR + 2, -M.baseH - M.footH]]) sweep.push([r * c, r * s, z]);
}
export const P = k.fitProjection(sweep, WIDTH, HEIGHT, { pad: 18, azimuth: AZ, elevation: M.elevation });
export const R = A.recorder(P);
const put = (item) => R.put({ ...item, shapes: item.shapes.map((shape, index) => R.solid(Object.assign(shape, { name: shape.name ?? (index ? `${item.name}#${index + 1}` : item.name) }))) });
const S = (paths, style) => k.solidSvg(paths, style);
const L = (d, style) => k.lineSvg(d, style);
const Dt = (points, style) => k.dotsSvg(points, style);
const iso = (p) => k.iso(p, P);

// ---- static: the turntable base under the platter ---------------------------------------------------------------------
function base() {
  const { baseR, baseH, footH } = M;
  const feet = [45, 135, 225, 315].map((a) => [(baseR - 22) * Math.cos(a * D2R), (baseR - 22) * Math.sin(a * D2R)]);
  for (const [x, y] of feet) {
    const svg = S(k.cylinder(x, y, 9, -baseH - footH, footH, P, 28), { tone: "lo" });
    put({ name: `stand.foot.${Math.round(x)}.${Math.round(y)}`, layer: "back", at: [x, y, -baseH - footH], svg, shapes: [A.cylinder(x, y, 9, -baseH - footH, footH, { ground: true })] });
  }
  const body = k.cylinder(0, 0, baseR, -baseH, baseH, P, 96, 1.6);
  const ticks = k.radialTicks(0, 0, baseR - 1.5, 72, 6, 0, [2.4, 5.2], P);
  // a degree scale: ticks run from the outer ring to the ring their length ends on
  const scale = [baseR - 1.5, baseR - 3.9, baseR - 6.7].map((r, i) => L(k.ring(0, 0, r, 0, P, 128), { tone: i ? "faint" : "lo" })).join("");
  const inner = [scale, L(ticks.minor ?? ticks, { tone: "lo" }), ticks.major ? L(ticks.major, { tone: "mid" }) : ""].join("");
  // the index: a brass wedge inlaid flush in the top of the base, facing the reader
  const ia = AZ * D2R;
  const wedge = [[M.platterR + 4, -0.8], [M.platterR + 4, 0.8], [baseR - 3, 3.4], [baseR - 3, -3.4]].map(([r, t]) => [Math.cos(ia) * r - Math.sin(ia) * t, Math.sin(ia) * r + Math.cos(ia) * t]);
  const index = `<g data-mat="brass"><path class="iso-top" d="${k.onTop(wedge, 0, P, true)}"/><path class="iso-line iso-edge" data-tone="mid" d="${k.onTop(wedge, 0, P, true)}"/></g>`;
  const svg = S(body, { tone: "mid", inner: inner + index });
  put({ name: "stand.base", layer: "back", at: [0, 0, -baseH], bias: 4, svg, shapes: [A.cylinder(0, 0, baseR, -baseH, baseH, { ground: true })] });
}

// ---- live: the turntable and everything on it --------------------------------------------------------------------------
export const T = turning(P, { recorder: R });
const spin = T.group("spin", { turn: { pivot: [0, 0] } });
const G_ = { spin };
for (const id of ORDER) if (PIECES[id].travel) G_[id] = T.group(id, { parent: spin, slide: { direction: PIECES[id].travel > 0 ? N : G.mul3(N, -1), travel: Math.abs(PIECES[id].travel) } });
const layer = T.layer("badge");
T.stack(["back", "badge", "front"]);
const groupOf = (id) => G_[id] ?? spin;

// A ring of convex sectors about the badge axis. The radial faces between sectors are cuts: never stroked.
function ring(id, name, { s0, s1, rIn, rOut, sectors, facets = 5, material, tone = "hi", spanS, arcs }) {
  const step = (2 * Math.PI) / (sectors * facets);
  const rv = rOut / Math.cos(step / 2); // outer polygon circumscribes rOut: every facet's apothem is rOut
  const parts = [];
  for (let j = 0; j < sectors; j++) {
    // odd facet count: sector edges fall on vertices and a sector centres on a facet (down, ±135° face a flat)
    const a0 = (j * facets + facets / 2) * step;
    const a1 = ((j + 1) * facets + facets / 2) * step;
    const pts = [];
    for (let f = 0; f <= facets; f++) {
      const a = a0 + f * step;
      pts.push([rv * Math.cos(a), rv * Math.sin(a)]);
    }
    const ri = rIn / Math.cos((a1 - a0) / 2);
    pts.push([ri * Math.cos(a1), ri * Math.sin(a1)], [ri * Math.cos(a0), ri * Math.sin(a0)]);
    // spans along the axis; a span edge listed in `joins` meets the next span flush and is not stroked
    const { spans, joins = [] } = spanS ? spanS(j, a0, a1) : { spans: [[s0, s1]] };
    for (const [i, [t0, t1]] of spans.entries()) {
      const part = T.prism({ name: `${name}.${String(j).padStart(2, "0")}${spans.length > 1 ? "abcdef"[i] : ""}`, group: groupOf(id), layer, F: FB, polygon: pts, s0: t0, s1: t1, tone, crease: "none", material });
      const cut = (a) => {
        const n = G.add3(G.mul3(UP, -Math.sin(a)), G.mul3(V, Math.cos(a)));
        return { n, d: G.dot3(n, B0) };
      };
      part.spec.cuts = [cut(a0), cut(a1), ...joins.filter((t) => t === t0 || t === t1).map((t) => ({ n: N, d: G.dot3(N, B0) + t }))];
      // the shared faces are the planes that separate flush neighbours: try them first
      part.hints.unshift(cut(a0).n, cut(a1).n, N);
      // chamfer lines on the face: each sector carries its own arc, so the paint order stays the sector's
      for (const { s, r, tone: t = "faint" } of arcs ?? []) {
        if (s !== t0 && s !== t1) continue;
        const arc = Array.from({ length: facets * 2 + 1 }, (_, q) => {
          const a = a0 + ((a1 - a0) * q) / (facets * 2);
          return `${q ? "L" : "M"}${(r * Math.sin(a)).toFixed(3)} ${(-r * Math.cos(a)).toFixed(3)}`;
        }).join("");
        const out = s === t1 ? N : G.mul3(N, -1);
        T.plane({ part, o: at3(s, 0, 0), u: V, v: G.mul3(UP, -1), normal: out, svg: `<path class="iso-line" data-tone="${t}" d="${arc}"/>` });
      }
      parts.push(part);
    }
  }
  return parts;
}

// angle in the badge plane, measured from UP toward V
const ang = (deg) => deg * D2R;
const polar = (r, deg) => [r * Math.cos(ang(deg)), r * Math.sin(ang(deg))];

function turntable() {
  T.round({ name: "spin.platter", group: spin, layer, F: VERT(0, 0), s0: 0, s1: M.platterH, r: M.platterR, tone: "mid", crease: "lo", material: "steel", details: { ribs: { s0: 0, s1: M.platterH, count: 220, tone: "lo", fade: [0.08, 0.5] }, rings: [M.platterR - 6, 104, 60].map((r, i) => ({ at: [M.platterH, 0, 0], normal: [0, 0, 1], r, tone: i ? "faint" : "lo" })) } });
  // the stand: a block under the case (flush with its bottom facet, clear of the USB-C window) and a back rest
  // flush with the back face below the pin. Both touch only the back shell, which never moves.
  const xPl = M.platterH - M.cz; // platter top in badge x
  const block = (name, x1, t0, t1, w) => T.prism({ name, group: spin, layer, F: FB, polygon: [[xPl, -w / 2], [x1, -w / 2], [x1, w / 2], [xPl, w / 2]], s0: t0, s1: t1, tone: "mid", crease: "lo", bevel: 0.6, material: "gunmetal",
    details: { dots: [-1, 1].map((side) => ({ at: [t1, xPl + 5, (side * w) / 3], normal: N, size: 0.5, tone: "hi" })), rings: [-1, 1].map((side) => ({ at: [t1, xPl + 5, (side * w) / 3], normal: N, r: 1.6, tone: "lo" })) } });
  block("spin.stand.seat", -M.caseR, M.cradleS[0], M.cradleS[1], 40);
  block("spin.stand.rest", -58, -12, 0, 56);
}


function backShell() {
  const id = "back";
  const usb = M.usb;
  ring(id, "back.wall", {
    s0: 0, s1: M.wallTop, rIn: M.wallIn, rOut: M.caseR, sectors: 32, material: "rubber",
    // the two bottom sectors carry the USB-C window: wall behind it and in front of it, open in between
    arcs: [{ s: 0, r: M.caseR - 2.2 }],
    // two shallow rings: deep sectors seen obliquely overlap their non-neighbours and the painter's order cycles.
    // The three sectors under the board's USB-C carry its window instead of the first join.
    spanS: (j, a0, a1) =>
      Math.abs((a0 + a1) / 2 - Math.PI) < 17 * D2R
        ? { spans: [[0, usb.s0 - 0.6], [usb.s1 + 0.6, 22], [22, M.wallTop]], joins: [22] }
        : { spans: [[0, 22], [22, M.wallTop]], joins: [22] },
  });
  const plate = T.round({ name: "back.plate", group: spin, layer, F: FB, s0: 0, s1: M.plateT, r: M.wallIn, tone: "hi", material: "rubber", details: { rings: M.buttons.map((b) => ({ at: [0, b.x, b.y], normal: G.mul3(N, -1), r: 5.6, tone: "lo" })) } });
  // the battery cutout over the JST header: an opening in the back, drawn on the outside face
  const j = M.jst;
  T.plane({ part: plate, o: at3(0, j.x, j.y), u: V, v: G.mul3(UP, -1), normal: G.mul3(N, -1), svg: `<rect class="iso-line" data-tone="mid" x="${(-j.d / 2 - 3).toFixed(2)}" y="${(-j.w / 2 - 3).toFixed(2)}" width="${(j.d + 6).toFixed(2)}" height="${(j.w + 6).toFixed(2)}" rx="2.4"/><rect class="iso-line" data-tone="faint" x="${(-j.d / 2 - 1.6).toFixed(2)}" y="${(-j.w / 2 - 1.6).toFixed(2)}" width="${(j.d + 3.2).toFixed(2)}" height="${(j.w + 3.2).toFixed(2)}" rx="1.4"/>` });
  // button plungers: a cap outside, a stem inside, at the two holes the case leaves for BOOT and RESET
  for (const b of M.buttons) {
    T.round({ name: `back.${b.id}.cap`, group: spin, layer, F: ALONG(b.x, b.y), s0: -1.6, s1: 0, r: 4.2, ends: ["dome", "flat"], tone: "hi", material: "rubber" });
    T.round({ name: `back.${b.id}.stem`, group: spin, layer, F: ALONG(b.x, b.y), s0: M.plateT, s1: M.switchTop, r: 2.4, tone: "mid", material: "rubber" });
  }
  void plate;
}

export const PCB_FACE = M.pcb.s0; // components hang off the back face toward the back shell
function pcb() {
  const g = groupOf("pcb");
  const { s0, s1, r } = M.pcb;
  const holes = [45, 135, 225, 315].map((deg) => polar(r - 9, deg));
  const back = [];
  for (const [x, y] of holes) back.push({ at: [s0, x, y], normal: G.mul3(N, -1), size: 0.55, tone: "mid" });
  const board = T.round({ name: "pcb.board", group: g, layer, F: FB, s0, s1, r, tone: "mid", crease: "lo", details: { rings: holes.map(([x, y]) => ({ at: [s0, x, y], normal: G.mul3(N, -1), r: 3.2, tone: "lo" })).concat(holes.map(([x, y]) => ({ at: [s1, x, y], normal: N, r: 3.2, tone: "lo" }))), dots: back } });
  T.plane({ part: board, o: at3(s0, 0, 0), u: V, v: G.mul3(UP, -1), normal: G.mul3(N, -1), svg: silkscreen() });
  // ESP32-C3-MINI-1U: module board, then the shield can
  const esp = M.esp;
  const box = (name, x, y, w, d, t0, t1, extra) => T.prism({ name, group: g, layer, F: FB, polygon: [[x - w / 2, y - d / 2], [x + w / 2, y - d / 2], [x + w / 2, y + d / 2], [x - w / 2, y + d / 2]], s0: t0, s1: t1, ...extra });
  box("pcb.esp.board", esp.x, esp.y, esp.w, esp.d, s0 - 2.4, s0, { tone: "lo", crease: "lo" });
  // the shield leaves a strip at the top of the module for the U.FL antenna socket
  const shield = box("pcb.esp.shield", esp.x - 5.25, esp.y, esp.w - 13.5, esp.d - 3, s0 - esp.h, s0 - 2.4, { tone: "hi", crease: "lo", bevel: 0.6, material: "steel" });
  T.plane({ part: shield, o: at3(s0 - esp.h, esp.x - 5.25, esp.y), u: G.mul3(V, 1), v: G.mul3(UP, -1), normal: G.mul3(N, -1), svg: shieldMarkup(esp) });
  T.round({ name: "pcb.esp.ufl", group: g, layer, F: frameOf(at3(s0 - 2.4, esp.x + esp.w / 2 - 6, esp.y - esp.d / 2 + 6), G.mul3(N, -1), UP), s0: 0, s1: 3.2, r: 3.4, tone: "hi", material: "brass", details: { bolts: [{ s: 3.2, r: 0, count: 1, size: 0.5 }] } });
  // USB-C receptacle on the bottom edge, mouth facing down through the case window
  const u = M.usb;
  const usbPart = T.prism({ name: "pcb.usb", group: g, layer, F: frameOf(at3((u.s0 + u.s1) / 2, -M.usbR, 0), G.mul3(UP, -1), N), polygon: roundRect(u.s1 - u.s0, u.w, 3.2), s0: -u.deep, s1: 0, tone: "hi", crease: "lo", material: "chrome", details: {} });
  T.plane({ part: usbPart, o: at3((u.s0 + u.s1) / 2, -M.usbR, 0), u: V, v: N, normal: G.mul3(UP, -1), svg: usbMouth(u) });
  // JST 1.25 battery header: beige housing, two contacts
  const j = M.jst;
  const jst = box("pcb.jst", j.x, j.y, j.w, j.d, s0 - j.h, s0, { tone: "hi", crease: "lo", bevel: 0.4, material: "brass" });
  T.plane({ part: jst, o: at3(s0 - j.h, j.x, j.y), u: V, v: G.mul3(UP, -1), normal: G.mul3(N, -1), svg: `<path class="iso-line" data-tone="lo" d="M-6 -2.2H6V2.2H-6Z"/><circle class="iso-line" data-tone="mid" cx="-2.6" cy="0" r="1.1"/><circle class="iso-line" data-tone="mid" cx="2.6" cy="0" r="1.1"/>` });
  // tactile switches under the plungers
  for (const b of M.buttons) {
    box(`pcb.${b.id}.body`, b.x, b.y, 14, 14, M.switchTop + 2, s0, { tone: "mid", crease: "lo", bevel: 0.4, material: "steel" });
    T.round({ name: `pcb.${b.id}.knob`, group: g, layer, F: ALONG(b.x, b.y), s0: M.switchTop, s1: M.switchTop + 2, r: 3.2, tone: "lo", material: "rubber" });
  }
  // the two small chips: LDO and the battery charger
  for (const c of M.chips) {
    const chip = box(`pcb.${c.id}`, c.x, c.y, c.w, c.d, s0 - 4, s0, { tone: "lo", crease: "lo", bevel: 0.3, material: "gunmetal", details: { dots: [{ at: [s0 - 4, c.x + c.w / 2 - 2, c.y - c.d / 2 + 2], normal: G.mul3(N, -1), size: 0.45, tone: "hi" }] } });
    void chip;
  }
  // the display connector on the front face; the panel's flex lands on it
  const f = M.fpc;
  box("pcb.fpc.socket", f.x, 0, f.w, f.d, s1, s1 + f.h, { tone: "lo", crease: "lo", bevel: 0.3, material: "gunmetal" });
}
// the back of the board: the model number, part outlines and a few traces that end on pads
function silkscreen() {
  const box = (x, y, w, d) => `M${(y - d / 2 - 2).toFixed(2)} ${(-x - w / 2 - 2).toFixed(2)}h${(d + 4).toFixed(2)}v${(w + 4).toFixed(2)}h${(-d - 4).toFixed(2)}Z`;
  const outlines = [box(M.jst.x, M.jst.y, M.jst.w, M.jst.d), ...M.buttons.map((b) => box(b.x, b.y, 14, 14)), ...M.chips.map((c) => box(c.x, c.y, c.w, c.d))].join("");
  const pads = [[-30, 30], [-30, 44], [-52, 30], [6, -46], [6, -60], [-62, -8], [-62, 8]];
  const traces = "M-30 30L-30 44M-52 30L-30 30M6 -46L6 -60M-62 -8L-62 8";
  return `<path class="iso-line" data-tone="faint" d="${outlines}"/><path class="iso-line" data-tone="lo" d="${traces}"/>${pads.map(([x, y]) => `<circle class="iso-line" data-tone="mid" cx="${x}" cy="${y}" r="1.3"/>`).join("")}<text class="iso-face-text" data-tone="lo" font-size="4.6" text-anchor="middle" x="-2" y="58">ESP32-2424S012C</text>`;
}
const roundRect = (w, d, r) => k.roundedPlan({ x: -w / 2, y: -d / 2, w, d, r }, 3);
const usbMouth = (u) => {
  const w = u.w - 4;
  const h = u.s1 - u.s0 - 4.4;
  return `<rect class="iso-line" data-tone="lo" x="${(-w / 2).toFixed(2)}" y="${(-h / 2).toFixed(2)}" width="${w.toFixed(2)}" height="${h.toFixed(2)}" rx="${(h / 2).toFixed(2)}"/><path class="iso-line" data-tone="mid" d="M${(-w / 2 + 6).toFixed(2)} 0H${(w / 2 - 6).toFixed(2)}"/>`;
};
function shieldMarkup(esp) {
  const w = esp.w - 13.5;
  const d = esp.d - 3;
  return `<path class="iso-line" data-tone="faint" d="M${(-w / 2 + 3).toFixed(2)} ${(-d / 2 + 3).toFixed(2)}H${(w / 2 - 3).toFixed(2)}V${(d / 2 - 3).toFixed(2)}H${(-w / 2 + 3).toFixed(2)}Z"/><text class="iso-face-text" data-tone="mid" font-size="4.2" text-anchor="middle" x="0" y="-3">ESP32-C3</text><text class="iso-face-text" data-tone="lo" font-size="3.2" text-anchor="middle" x="0" y="3.4">MINI-1U</text><circle class="iso-line" data-tone="lo" cx="${(-w / 2 + 6).toFixed(2)}" cy="${(d / 2 - 6).toFixed(2)}" r="1.4"/>`;
}

function displayStack() {
  const p = M.panel;
  const panel = T.round({ name: "panel.body", group: groupOf("panel"), layer, F: FB, s0: p.s0, s1: p.s1, r: p.r, tone: "lo", crease: "lo", material: "gunmetal", details: { seams: [p.s0 + 1.6] } });
  T.plane({ part: panel, o: at3(p.s1, 0, 0), u: V, v: G.mul3(UP, -1), normal: N, fade: [0.02, 0.25], svg: screenMarkup(p.active) });
  const f = M.fpc;
  T.prism({ name: "panel.flex", group: groupOf("panel"), layer, F: FB, polygon: [[f.x - f.w / 2 + 1, -f.d / 2 + 1], [f.x + f.w / 2 - 1, -f.d / 2 + 1], [f.x + f.w / 2 - 1, f.d / 2 - 1], [f.x - f.w / 2 + 1, f.d / 2 - 1]], s0: M.pcb.s1 + f.h, s1: p.s0, tone: "hi", crease: "lo", material: "copper" });
  const t = M.touch;
  const film = T.round({ name: "touch.film", group: groupOf("touch"), layer, F: FB, s0: t.s0, s1: t.s1, r: t.r, tone: "hi", crease: "lo", material: "chrome" });
  T.plane({ part: film, o: at3(t.s1, 0, 0), u: G.mul3(V, -1), v: G.mul3(UP, -1), normal: N, fade: [0.04, 0.3], svg: itoMarkup(t.r - 3) });
  const gl = M.glass;
  T.round({ name: "glass.pane", group: groupOf("glass"), layer, F: FB, s0: gl.s0, s1: gl.s1 - 0.8, r: gl.r, tone: "hi", crease: "lo", material: "chrome" });
  T.round({ name: "glass.chamfer", group: groupOf("glass"), layer, F: FB, s0: gl.s1 - 0.8, s1: gl.s1, r: [gl.r, gl.r - 0.8], tone: "hi", crease: "lo", material: "chrome" });
}
const PNG = readFileSync(join(HERE, "../../screens/home.png")).toString("base64");
function screenMarkup(r) {
  return `<defs><clipPath id="cz-screen"><circle r="${r.toFixed(2)}"/></clipPath></defs><g clip-path="url(#cz-screen)"><image class="cz-pixels" href="data:image/png;base64,${PNG}" x="${(-r).toFixed(2)}" y="${(-r).toFixed(2)}" width="${(2 * r).toFixed(2)}" height="${(2 * r).toFixed(2)}" preserveAspectRatio="none"/></g><circle class="iso-line" data-tone="lo" r="${r.toFixed(2)}"/>`;
}
function itoMarkup(r) {
  // the CST816S sense grid: diamonds of ITO, drawn faint, clipped to the film
  const pitch = 12;
  const lines = [];
  for (let c = -r * 2; c <= r * 2; c += pitch) {
    for (const sgn of [-1, 1]) {
      // chord of the line y = sgn*x + c with the circle of radius r
      const a = 2;
      const b = 2 * sgn * c;
      const cc = c * c - r * r;
      const disc = b * b - 4 * a * cc;
      if (disc <= 0) continue;
      const x0 = (-b - Math.sqrt(disc)) / (2 * a);
      const x1 = (-b + Math.sqrt(disc)) / (2 * a);
      lines.push(`M${x0.toFixed(2)} ${(sgn * x0 + c).toFixed(2)}L${x1.toFixed(2)} ${(sgn * x1 + c).toFixed(2)}`);
    }
  }
  return `<path class="iso-line" data-tone="faint" d="${lines.join("")}"/><circle class="iso-line" data-tone="lo" r="${r.toFixed(2)}"/>`;
}

function bezel() {
  const t = M.wallTop + M.lipT;
  ring("bezel", "bezel.lip", { s0: M.wallTop, s1: t, rIn: M.window, rOut: M.caseR, sectors: 32, material: "rubber", arcs: [{ s: t, r: M.caseR - 2.2 }, { s: t, r: M.window + 2.4 }, { s: t, r: M.window + 0.8, tone: "lo" }] });
}

function barPin() {
  const g = groupOf("pin");
  const p = M.pin;
  const box = (name, w, d, t0, t1, extra) => T.prism({ name, group: g, layer, F: FB, polygon: roundRect(d, w, Math.min(3, d / 2 - 0.1)).map(([x, y]) => [p.x + x, y]), s0: t0, s1: t1, ...extra });
  box("pin.tape", p.w, p.d, -p.tape, 0, { tone: "lo", crease: "lo" });
  box("pin.bar", p.w - 6, p.d - 8, -p.tape - p.bar, -p.tape, { tone: "hi", crease: "lo", bevel: 0.4, material: "steel" });
  const s = -p.tape - p.bar;
  const half = (p.w - 6) / 2;
  // hinge barrel at one end, catch at the other, the needle between
  T.round({ name: "pin.hinge", group: g, layer, F: frameOf(at3(s - 3, p.x, -half + 2), V, UP), s0: -2, s1: 5, r: 3, tone: "mid", material: "steel" });
  T.prism({ name: "pin.catch", group: g, layer, F: FB, polygon: [[p.x - 3.4, half - 9], [p.x + 3.4, half - 9], [p.x + 3.4, half - 2], [p.x - 3.4, half - 2]], s0: s - 6, s1: s, tone: "mid", crease: "lo", bevel: 0.3, material: "steel" });
  T.round({ name: "pin.needle", group: g, layer, F: frameOf(at3(s - 3, p.x, 0), V, UP), s0: -half + 7, s1: half - 9.9, r: 0.9, ends: ["flat", "dome"], tone: "hi", material: "chrome" });
}

base();
turntable();
backShell();
pcb();
displayStack();
bezel();
barPin();

const posesOf = () => {
  const out = [];
  for (let a = 0; a < 360; a += 2) for (const apart of [0, 0.2, 0.4, 0.6, 0.8, 1]) out.push(follow({ spin: a, apart }));
  return out;
};
const out = T.build({ statics: R.items, poses: posesOf, verify });
A.settle(R, P);
const layerOf = (name) => R.items.filter((item) => item.layer === name).sort((a, b) => a.key - b.key).map((item) => item.svg);
const BACK = [`<path class="iso-halo" d="${k.haloOf({ x: -M.baseR, y: -M.baseR, w: 2 * M.baseR, d: 2 * M.baseR, r: M.baseR }, -M.baseH - M.footH, M.baseH, P)}"/>`, ...layerOf("back")];
const FRONT = layerOf("front");
if (argv.includes("--audit")) {
  A.auditOrExit(R, P);
  TA.orbitOrExit(T, R, P, {
    sweep: { at: (x) => follow({ spin: x, apart: 0 }), from: 0, to: 360, frame: 0.3 },
    clips: [{ name: "apart", at: (t) => follow({ spin: 30 + 8 * t, apart: Math.min(1, t / 1.2) }), from: 0, to: 1.5, frame: 1 / 60 }],
    quick: argv.includes("--quick"),
    only: argv.find((a) => a.startsWith("--only="))?.slice(7).split(",") ?? null,
    log: argv.includes("--verbose") ? (text) => console.log(`  … ${text}`) : null,
  });
}

const LABEL = "The Citizen badge on a turntable: a round black case holding a 1.28 inch round screen, its glass, touch film and display panel, an ESP32-C3 board with USB-C and a battery header, and a bar pin on foam tape at the back.";
const backSvg = k.figureSvg({ width: WIDTH, height: HEIGHT, label: LABEL, body: BACK });
const staticSvg = (list) => `<svg xmlns="http://www.w3.org/2000/svg" class="iso-svg iso-layer" viewBox="0 0 ${WIDTH} ${HEIGHT}" aria-hidden="true">${list.join("")}</svg>`;
const stage = `<div class="iso-stage iso-turn cz-stage" tabindex="0" role="slider" aria-label="Citizen badge: drag to turn it, hover a part or press space to take it apart, arrow keys step through the parts" aria-valuemin="0" aria-valuemax="${ORDER.length}" aria-valuenow="0" aria-valuetext="assembled">${backSvg}${out.layerSvg("badge", { width: WIDTH, height: HEIGHT })}${staticSvg(FRONT)}</div>`;

const DATA = { turn: out.data, pieces: PIECES, order: ORDER, detail: DETAIL, dragZ: M.cz };
const CSS = out.css + "\n" + readFileSync(join(HERE, "badge.css"), "utf8");
const LIVE = readFileSync(join(HERE, "live.js"), "utf8").replace("__DATA__", JSON.stringify(DATA)).replace("const follow = null;", `const follow = ${follow.toString()};`);
const PARTS = ORDER.map((id, i) => `<button type="button" class="cz-part" data-part="${id}"><span class="cz-num">${String(i + 1).padStart(2, "0")}</span>${PIECES[id].name}</button>`).join("");
const body = k.plateHtml({
  fig: "Fig 3D",
  title: "Citizen badge",
  hint: "Drag to turn · hover to take apart",
  readout: "assembled · 0°",
  keys: [],
  caption: "The DIYMORE ESP32-2424S012C in its case, as each speaker wears it. Readouts are the verified board table; proportions are drawn, not measured.",
  body: `<style>${CSS}</style>${stage}`,
}).replace('<figcaption class="iso-legend">', `<figcaption class="iso-legend"><span class="cz-parts">${PARTS}</span>`);
const page = k.pageHtml({ title: "Citizen badge", theme, body, script: turnScript() + (verify ? `window.__isoVerify = ${JSON.stringify(out.verify)};\n` : "") + LIVE, width: 720 });
if (process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1]) {
  const name = `badge-3d${verify ? "-verify" : ""}${theme === "light" ? "-light" : ""}.html`;
  writeFileSync(join(HERE, name), page);
  console.log(`wrote ${name} · ${(page.length / 1024).toFixed(0)} KB · ${out.report.parts} parts · ${out.report.layers.map((l) => `${l.name}: ${l.planar} planes, ${l.framed} framed, ${l.dynamic} dynamic`).join(" · ")} · build ${out.report.seconds.toFixed(1)} s`);
}
export { out };
