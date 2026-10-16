# Citizen simulator

Browser simulator for the Citizen badge (PRD section 5). It runs the real C core from `core/` compiled to
WebAssembly, draws the 240x240 framebuffer on a round canvas, takes the same raw touch events the firmware
feeds the core, and wraps it in a hardware model of the ESP32-C3 board so you can read frame time, current,
battery and temperature estimates while you fast-forward days of badge life. No bundler, no dependencies.

![Hatch screen on first boot, with the readouts and content panel on the right](../docs/sim/sim-home.png)

*First boot: the prompt types out on the Hatch screen. The right column shows the live readouts and the content panel.*

## Build and run

```
make wasm                        # at the repo root, builds sim/core.wasm with Homebrew LLVM
cd sim && python3 -m http.server 8791
```

Open http://localhost:8791. It must be served over HTTP, not opened as a file, so `core.wasm` and `hardware.json`
can be fetched. `make wasm` needs `/opt/homebrew/opt/llvm/bin/clang` and `wasm-ld` from the `lld` formula
(`brew install llvm lld`). The build is `--target=wasm32 -ffreestanding -nostdlib -O2` and exports exactly the
`cz_*` functions declared in `core/citizen.h`. The resulting `sim/core.wasm` is 139,684 bytes.

`scripts/check.sh` builds the wasm and runs the simulator selftest headlessly on port 8790 as part of the
repo-wide check.
