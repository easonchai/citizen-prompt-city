const DATA = __DATA__;
const follow = null;
const stage = document.querySelector(".cz-stage");
const readout = document.querySelector("[data-readout]");
const hint = document.querySelector("[data-hint]") ?? document.querySelector(".iso-hint");
const buttons = [...document.querySelectorAll(".cz-part")];
const still = matchMedia("(prefers-reduced-motion: reduce)");
const narrow = matchMedia("(hover: none), (max-width: 480px)");
const TOUR = 14; // degrees per second while nobody touches it
let spin = 0;
let target = 0;
let apart = 0;
let apartTo = 0;
let selected = null;
let detail = null;
let frame = 0;
let last = 0;
let visible = false;
let touring = !still.matches;
let idle = 0;
let leave = 0;
let inside = false;
let drawn = null;
let own = false;

const controller = TURN.mount(stage, DATA.turn, {
  follow,
  onHold: () => {
    touring = false;
    clearTimeout(idle);
  },
  // a tool (or a test) set the pose through window.__isoTurn: adopt it so the loop does not pull it back
  onFrame: (turned) => {
    if (own) return;
    spin = target = drawn = turned.values.spin ?? spin;
    apart = apartTo = turned.values.apart ?? apart;
    show();
  },
});
const parts = () => [...stage.querySelectorAll(".iso-live [data-name]")];
const pieceOf = (name) => {
  const id = name.split(".")[0];
  return id in DATA.pieces ? id : null;
};
const hold = () => {
  touring = false;
  clearTimeout(idle);
  idle = setTimeout(() => {
    touring = !still.matches;
    run();
  }, 4000);
};
function describe() {
  const deg = ((Math.round(target) % 360) + 360) % 360;
  const what = selected ? DATA.pieces[selected].name : apartTo ? "apart" : "assembled";
  stage.setAttribute("aria-valuenow", String(selected ? DATA.order.indexOf(selected) + 1 : 0));
  stage.setAttribute("aria-valuetext", `${what}, turned ${deg}°`);
}
function show() {
  const deg = ((Math.round(spin) % 360) + 360) % 360;
  const text = selected ? (detail && DATA.detail[detail]) || DATA.pieces[selected].readout : `${apart > 0.5 ? "apart" : "assembled"} · ${deg}°`;
  if (readout && readout.textContent !== text) readout.textContent = text;
}
function select(id, part = null) {
  if (id === "spin") id = null;
  detail = part;
  if (selected !== id) {
    selected = id;
    for (const el of parts()) {
      if (id && pieceOf(el.dataset.name) === id) el.setAttribute("data-on", "");
      else el.removeAttribute("data-on");
    }
    if (id) stage.setAttribute("data-focus", "");
    else stage.removeAttribute("data-focus");
    buttons.forEach((b) => (b.dataset.part === id ? b.setAttribute("data-on", "") : b.removeAttribute("data-on")));
  }
  describe();
  show();
}
const setApart = (on) => {
  apartTo = on ? 1 : 0;
  describe();
};
const draw = () => {
  own = true;
  controller.set({ spin, apart });
  own = false;
  drawn = spin;
};
function tick(now) {
  const dt = last ? Math.min((now - last) / 1000, 1 / 30) : 1 / 60;
  last = now;
  const calm = still.matches;
  if (touring && !calm) target += TOUR * dt;
  spin = calm ? target : spin + (target - spin) * (1 - Math.exp(-dt / 0.12));
  if (Math.abs(target - spin) < 1e-3) spin = target;
  const before = apart;
  apart = calm ? apartTo : apart + (apartTo - apart) * (1 - Math.exp(-dt / 0.28));
  if (Math.abs(apartTo - apart) < 1e-4) apart = apartTo;
  if (apart !== before || drawn === null || Math.abs(spin - drawn) > 1e-4) draw();
  show();
  const settled = !touring && spin === target && apart === apartTo;
  frame = visible && !settled ? requestAnimationFrame(tick) : 0;
}
const run = () => {
  if (!frame && visible) {
    last = 0;
    frame = requestAnimationFrame(tick);
  }
};
new IntersectionObserver(
  (entries) => {
    visible = entries[entries.length - 1].isIntersecting;
    if (visible) run();
    else if (frame) {
      cancelAnimationFrame(frame);
      frame = 0;
    }
  },
  { rootMargin: "120px 0px" },
).observe(stage);

const partAt = (event) => {
  const hit = document.elementFromPoint(event.clientX, event.clientY);
  const el = hit && hit.closest ? hit.closest(".iso-live [data-name]") : null;
  return el ? el.dataset.name : null;
};
const exact = (name) => (name ? Object.keys(DATA.detail).find((key) => name === key || name.startsWith(`${key}.`)) ?? null : null);
let drag = null;
stage.addEventListener("pointerdown", (event) => {
  hold();
  drag = { from: controller.angleAt(event.clientX, event.clientY, DATA.dragZ, "spin"), base: target, id: event.pointerId, captured: false, x: event.clientX, y: event.clientY };
});
stage.addEventListener("pointermove", (event) => {
  if (drag && event.pointerId === drag.id) {
    const delta = controller.angleAt(event.clientX, event.clientY, DATA.dragZ, "spin") - drag.from;
    if (!drag.captured && Math.hypot(event.clientX - drag.x, event.clientY - drag.y) > 6) {
      drag.captured = true;
      stage.setPointerCapture(event.pointerId);
      stage.setAttribute("data-drag", "");
    }
    if (drag.captured) {
      hold();
      target = drag.base + delta;
      describe();
      run();
      return;
    }
  }
  if (event.pointerType === "touch") return;
  inside = true;
  clearTimeout(leave);
  hold();
  const name = partAt(event);
  const id = name ? pieceOf(name) : null;
  setApart(true);
  select(id, exact(name));
  run();
});
const release = (event) => {
  if (!drag || event.pointerId !== drag.id) return;
  const tap = !drag.captured;
  drag = null;
  stage.removeAttribute("data-drag");
  try {
    stage.releasePointerCapture(event.pointerId);
  } catch {}
  if (tap && event.pointerType === "touch") {
    const name = partAt(event);
    const id = name ? pieceOf(name) : null;
    if (!id || (apartTo && id === selected)) {
      select(null);
      setApart(!apartTo);
    } else {
      setApart(true);
      select(id, exact(name));
    }
    run();
  }
};
stage.addEventListener("pointerup", release);
stage.addEventListener("pointercancel", release);
stage.addEventListener("pointerleave", () => {
  inside = false;
  clearTimeout(leave);
  leave = setTimeout(() => {
    if (inside || buttons.some((b) => b.matches(":hover, :focus-visible"))) return;
    select(null);
    setApart(false);
    run();
  }, 300);
});
buttons.forEach((button) => {
  const on = () => {
    hold();
    clearTimeout(leave);
    setApart(true);
    select(button.dataset.part);
    run();
  };
  const off = () => {
    clearTimeout(leave);
    leave = setTimeout(() => {
      if (inside || buttons.some((b) => b.matches(":hover, :focus-visible"))) return;
      select(null);
      setApart(false);
      run();
    }, 300);
  };
  button.addEventListener("pointerenter", on);
  button.addEventListener("focus", on);
  button.addEventListener("click", on);
  button.addEventListener("pointerleave", off);
  button.addEventListener("blur", off);
});
stage.addEventListener("keydown", (event) => {
  const index = selected ? DATA.order.indexOf(selected) : -1;
  let next = null;
  if (event.key === "ArrowDown") next = Math.min(DATA.order.length - 1, index + 1);
  else if (event.key === "ArrowUp") next = Math.max(0, index - 1);
  else if (event.key === "ArrowLeft" || event.key === "ArrowRight") {
    event.preventDefault();
    hold();
    target += (event.key === "ArrowLeft" ? -1 : 1) * (event.shiftKey ? 45 : 15);
    describe();
    run();
    return;
  } else if (event.key === " " || event.key === "Enter") {
    event.preventDefault();
    hold();
    setApart(!apartTo);
    if (!apartTo) select(null);
    run();
    return;
  } else if (event.key === "Escape") {
    event.preventDefault();
    select(null);
    setApart(false);
    run();
    return;
  }
  if (next === null) return;
  event.preventDefault();
  hold();
  setApart(true);
  select(DATA.order[next]);
  run();
});
const wording = () => hint && (hint.textContent = narrow.matches ? "Drag to turn · tap to take apart" : "Drag to turn · hover to take apart");
wording();
narrow.addEventListener?.("change", wording);
if (still.matches) touring = false;
draw();
describe();
show();
