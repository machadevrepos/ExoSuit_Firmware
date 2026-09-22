# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` at the repo root is the authoritative source for project truth, hard
invariants, current direction, and verification constraints on this PC — read it
first. This file adds commands and architecture notes that complement it.

## Commands

Firmware is built/flashed by the user in STM32CubeIDE only — never attempt to
build or flash firmware from here (see AGENTS.md "Verification").

```bash
# Python host tests (works on this PC; pytest itself is not installed, so use plain python3)
python3 -m pytest host/tests/python -v
python3 -m pytest host/tests/python/test_motion_engine.py -v      # single file
python3 -m pytest host/tests/python/test_motion_engine.py -k name -v   # single test
python3 host/tests/python/test_script_guards.py       # plain-assert scripts also run directly

# C++ module tests (host/tests/cpp, firmware/{master,node}/tests) — header-only exo/
# modules compiled against host g++/clang++ with stub fixtures under host/tests/cpp/stubs.
# NOT buildable on this PC (no working host C++ compiler) — must run inside STM32CubeIDE's
# toolchain or a machine with a real g++/clang++. Do not attempt via `cmake`/`ctest` here.
cmake -S host/tests/cpp -B host/tests/cpp/build && cmake --build host/tests/cpp/build
ctest --test-dir host/tests/cpp/build --output-on-failure

# Format / lint (first-party C/C++ only; vendor/Cube-generated trees excluded)
./scripts/format.sh     # clang-format, per .clang-format
./scripts/lint.sh       # cppcheck, warning+performance+portability, C++17

# Live tool (Coach Assist motion engine) — must be served, not opened as a file:// page
python host/live_tool/serve.py     # caching disabled; red "STALE BUILD" banner = browser has stale modules

# Desktop tool (recording/commissioning)
# open host/desktop_tool/Exoskeleton.html directly in a browser
./scripts/convert_bin_to_csv.sh    # or host/desktop_tool/vantage_bin_to_csv.py — ESOX bin -> CSV

# Dataset QA (see dataset/README.md for the full workflow)
python3 scripts/qa_session.py <session_dir>
python3 scripts/build_dataset_config.py
```

Firmware TU syntax checks (Windows/CubeIDE toolchain only, not runnable on this
PC) live at `host/tests/scripts/run_firmware_syntax_check.ps1` /
`run_host_syntax_suite.ps1`.

## Architecture

### Two-tier BLE mesh
One **Master** (STM32WB55, BLE central + GATT server to the browser, SD card
recording) talks to up to 4 **Node** peripherals (STM32WB55, W25Q256 flash) over
a hub-leaf BLE mesh, and relays live/recorded IMU data to the browser over a
separate GATT link. There is no RTOS on either side — `UTIL_Sequencer` pumps
BLE/HCI events in a single-context superloop; "watchdog" logic is software
timers only (no IWDG). Both firmware projects (`firmware/master/`,
`firmware/node/`) are CubeIDE-managed (`.ioc` at their roots) and share code
only through the header-only library below — there is no shared build target,
so changes must stay source-compatible with both toolchains.

### Shared header-only library (`firmware/common/inc/exo/`)
This is the core of the firmware logic and the only part directly host-testable
(compiled by both `arm-none-eabi-g++` in CubeIDE and host g++/clang++ in
`host/tests/cpp`, via stub fixtures for FatFS/BLE). Layout:

- `protocol/` — wire framing and session-transfer state machines (reliable
  control, transfer windows, training-CSV coordination, `live_bundle_v2`).
- `ble/` — hub-leaf link management (`hub_leaf_ble_manager`), PHY/MTU/DLE
  tuning state (`link_tune_state`), node upload pump, notification gating.
- `bridge/` — framing/sequencing/time-sync/stream-decoding for the emerging
  dual-hub UART bridge transport (12-node master study; see AGENTS.md "Current
  direction" before extending this — it's a separate, deprioritized timeline).
- `recording/`, `storage/` — Node-side live sample queue and W25Q256 flash
  recorder; Master-side SD session recorder, session stager, binary session
  index, timestamp ledger (ESOX v4 format).
- `sensors/` — ICM-45686/BNO085 drivers, Master IMU/training CSV formatters
  and loggers, SWO telemetry.
- `actuator/` — Node haptic pulse control.
- `types/` — shared structs/enums/topology used across both sides.
- `utils/` — logging, JSON, string/UART helpers shared by both firmware images.

Vendored drivers (`firmware/third_party/`: w25qxx, icm45686-driver, sh2) are
treated as external — edit only per documented, owned deviations.

### Host tooling
- `host/live_tool/` — browser Web Bluetooth + ONNX Runtime Web tool. Pipeline:
  `ble-protocol.js` (wire decode) → `ml-preprocessing.js` → **`motion-engine.js`**
  (deterministic kinematics: sensor→body-segment calibration, arm angles —
  independent of the ML model by design) → **`rep-analyzer.js`** (per-rep verdict
  against a coach target) → `arm-avatar.js` / `charts.js` / `ui.js`. The ONNX
  model path (`live-inference.js`) is a parallel, currently deprioritized track.
  Contract, scope limits and field results: `docs/MD_Files/motion-engine-contract.md`.
- `host/desktop_tool/` — standalone recording/commissioning page
  (`Exoskeleton.html`) + `vantage_bin_to_csv.py`; ICM scaling here must stay
  byte-identical to the browser JS decoder (see AGENTS.md invariants).
- `host/tests/python/` — protocol/dataset/preprocessing invariant tests, several
  named `test_*_invariants.py` guarding exact behaviors called out in AGENTS.md
  (BLE-only cleanup, binary-first desktop recording, live-preprocessing parity
  with the Python reference in `reference_preprocessing.py`).
- `host/tests/scripts/*.mjs` — Node-run fixture replays for motion engine /
  calibration / avatar behavior, driven by the `.ps1` runners.

### Dataset workflow
`dataset/` is local-only (gitignored) until a snapshot is deliberately
committed. Every session must be converted and pass `scripts/qa_session.py`
before it's trusted; `scripts/build_dataset_config.py` assembles the training
config. Full workflow: `dataset/README.md`.

### Where to look for current state
`docs/MD_Files/dataset-acquisition-branch-context.md` is the running,
timestamped log of what's solved vs. open across BLE transport, the live
model, and firmware risk — check its dated "CURRENT STATE" header before
assuming an older section still applies.
