// The badge's numbers. World units: 4 per millimetre, z up. Board facts come from docs/HARDWARE.md (the verified table);
// sizes are drawn to look right on the bench, not measured off the part.
import * as G from "../kit/lathe.mjs";

const D2R = Math.PI / 180;
const azimuth = 40;
const elevation = 26;
const face = (azimuth - 38) * D2R; // the badge faces 38° left of the reader at rest: a three-quarter view
const N = [Math.cos(face), Math.sin(face), 0];
const UP = [0, 0, 1];
const V = G.cross3(N, UP);
const platterH = 8;
const caseR = 88;
const cz = platterH + caseR + 14;
const pivotS = 40; // the turntable's axis passes through the stack this far from the back face
const B0 = G.sub3([0, 0, cz], G.mul3(N, pivotS));

export const M = {
  azimuth, elevation, N, UP, V, B0, cz,
  baseR: 140, baseH: 14, footH: 5, platterR: 128, platterH,
  caseR, wallIn: 80, wallTop: 44, lipT: 8, window: 64, plateT: 4,
  cradleS: [6, 38],
  reach: 166,
  pcb: { s0: 20, s1: 26, r: 78 },
  esp: { x: 26, y: 4, w: 50, d: 53, h: 10 }, // ESP32-C3-MINI-1U: 13.2 x 12.5 mm module under a shield
  usb: { s0: 7, s1: 20, w: 36, deep: 29 }, // USB-C receptacle, 9 x 3.2 mm mouth
  usbR: 76, // its mouth stays inside the wall's inner chords, so the board slides out past the window
  jst: { x: -22, y: -50, w: 14, d: 22, h: 12 },
  buttons: [{ id: "boot", x: -40, y: -18 }, { id: "reset", x: -40, y: 18 }],
  switchTop: 12,
  chips: [{ id: "ldo", x: -10, y: 42, w: 12, d: 12 }, { id: "chg", x: -8, y: -24, w: 12, d: 16 }],
  fpc: { x: 60, w: 8, d: 30, h: 3 },
  panel: { s0: 30, s1: 38, r: 68, active: 64 },
  touch: { s0: 38, s1: 40, r: 68 },
  glass: { s0: 40, s1: 44, r: 70 },
  pin: { x: 28, w: 96, d: 22, tape: 3, bar: 2 },
};

// Outer pieces leave first; every travel is along the face normal (the pin goes back).
export const PIECES = {
  bezel: { name: "Front case", travel: 126, readout: "Case · black plastic · 1.28 in window" },
  glass: { name: "Cover glass", travel: 108, readout: "Cover glass · over the 1.28 in panel" },
  touch: { name: "Touch film", travel: 94, readout: "CST816S touch · I2C 0x15" },
  panel: { name: "Display panel", travel: 80, readout: "GC9A01 panel · 240 x 240 · 1.28 in" },
  pcb: { name: "Board", travel: 60, readout: "ESP32-C3-MINI-1U · 160 MHz · 4 MB flash" },
  back: { name: "Back shell", travel: 0, readout: "Back shell · BOOT and RESET reachable" },
  pin: { name: "Bar pin", travel: -40, readout: "Bar pin on foam tape" },
};
export const ORDER = ["bezel", "glass", "touch", "panel", "pcb", "back", "pin"];
// part-level readouts that are more exact than their piece's
export const DETAIL = {
  "pcb.usb": "USB-C · 5 V",
  "pcb.jst": "JST 1.25 · battery, not included",
  "pcb.boot": "BOOT · GPIO 9",
  "back.boot": "BOOT · GPIO 9",
  "pcb.reset": "RESET",
  "back.reset": "RESET",
  "pcb.esp": "ESP32-C3-MINI-1U · 160 MHz · 4 MB flash",
};

// values { spin: degrees, apart: 0…1 } → group values. Self-contained: it is stringified into the page.
export const follow = (values) => {
  const a = values.apart ?? 0;
  const out = { ...values, spin: values.spin ?? 0 };
  const plan = [["bezel", 126, 0], ["glass", 108, 1], ["touch", 94, 2], ["panel", 80, 3], ["pcb", 60, 4], ["pin", 40, 1]];
  for (const [id, travel, i] of plan) {
    const x = Math.min(1, Math.max(0, a * 1.6 - i * 0.15));
    out[id] = travel * x * x * (3 - 2 * x);
  }
  return out;
};
