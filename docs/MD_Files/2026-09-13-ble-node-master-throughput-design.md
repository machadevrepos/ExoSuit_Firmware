# BLE Node→Master throughput breakthrough — design

**Date:** 2026-09-13 · **Branch:** `feature/BLE_speed_Optimization` · **Status:** implemented in code, awaiting CubeIDE build + hardware measurement (agents never flash).

## 1. Problem and measured baseline

The Node→Master reliable record upload (W25Q256 flash → Master SD) measured **~7–9 KB/s per
node** on the current tree (commit `0f4863e`; 2026-09-08 counters: NODE2 `574 accepted /
637 INSUFFICIENT_RESOURCES / 699 TX-pool events` — a ~1-notification-deep effective
pipeline). The radio-time ceiling for this framing at 2M PHY + DLE 251 + MTU 247 is far
higher:

| Item | Value | Source |
|---|---|---|
| ST-measured WB55 notification throughput @2M | 164 kB/s ≈ 1.3 Mbps | stm32-hotspot DataThroughput (Ellisys) |
| Sanity math | 251-byte PDU ≈ 1.4 ms/exchange @2M → 244 B payload / 1.4 ms ≈ 1.4 Mbps | radio timing |
| This wire | 192 B chunk per 239 B notification (192+25+22 ≤ 244) | `ble_record_protocol.h:26-35` |
| 20 ms granted interval, multi-packet CE (~13 pkt/CE) | ≈ 650 notifications/s ⇒ ≈ 125 KB/s chunk data | derived |
| 20 ms interval, 1 packet/CE (today) | 50/s ⇒ 9.6 KB/s wire ceiling ≈ measured | derived |

So the gap is not framing and not radio — it is **scheduler utilization**: the controller
transmits only ~1 queued notification per connection event, and the sender's wake path
reinforces the serialization.

## 2. Root causes (file:line, current tree)

1. **Node PipeDataTx lacks `GATT_NOTIFY_NOTIFICATION_COMPLETION` (0x08)** and is
   `CHAR_VALUE_LEN_VARIABLE` — `firmware/node/Core/Src/ble/custom_stm.cpp:524-532`. The
   Master's PipeDataTx has both (`Master/.../custom_stm.cpp:563-574`, mask 0x0F). This is
   the long-documented P1 (`dataset-acquisition-branch-context.md §5`): without 0x08 the
   Node gets no per-flush completion wake; the upload pump and live gate depend on
   TX-pool events (which only arm *after* a real `INSUFFICIENT_RESOURCES`) or watchdogs
   (`NodeUploadPump::kWatchdogMs` 750 ms, live gate 10 ms).
2. **Bulk CE hint collapses on the fallback ladder** — `exo/ble/link_tune_state.h:780-800`:
   `max_ce_length()` returns the generous `kBulkMaxCeLength` (30 ms) only when
   `fast_interval == kBulkInterval && fallback_level == 0`. Every rejected exact request
   (0x84/0x85: 30 ms exact is ungrantable at the observed 40 ms anchor) demotes the CE
   hint to `kLiveMaxCeLength` (5 ms). The connection-event length is an *informative hint*
   to the CPU2 scheduler (AN5270); without a generous hint the scheduler uses short CEs
   and drains ~1 packet/event — exactly the observed regime.
3. **TX-pool wake discards the reported buffer count** — `exo/ble/node_upload_pump.h:94-102`:
   `on_tx_pool_available(available_buffers)` stores the count in metrics only; the wake
   fully unblocks and the burst loop re-discovers capacity by hitting
   `INSUFFICIENT_RESOURCES` again. With 1 buffer freed per event this is 1 send + 1
   INSUFFICIENT + 1 block/wake round trip per notification — doubling event traffic and
   adding a superloop pass of latency per chunk.
4. **Bulk exact interval is ungrantable at the fleet's anchor** — the firmware-owned bulk
   interval is 30 ms (`record_transfer_tuning.h:24`); at a 40 ms anchor that request is
   rejected 0x84, walks 3 ladder levels (~150 ms of rejections), and lands on the same
   20 ms grant the wide window would give directly.

## 3. Changes

### 3.1 Node GATT characteristic (P1 — owned deviation of WPAN template code)

`firmware/node/Core/Src/ble/custom_stm.cpp`, PipeDataTx only:
- event mask `0x07 → 0x0F` (adds `GATT_NOTIFY_NOTIFICATION_COMPLETION`);
- length type **stays `CHAR_VALUE_LEN_VARIABLE`**.

The Master's PipeDataTx is created with the CONSTANT *token* but the Master project
redefines it to VARIABLE at compile time (`Master/.../custom_stm.cpp:63-64`,
`#undef CHAR_VALUE_LEN_CONSTANT / #define CHAR_VALUE_LEN_CONSTANT CHAR_VALUE_LEN_VARIABLE`)
— so the fleet-proven 0x08 configuration is VARIABLE + 0x08. Every node frame passes its
real length to `aci_gatt_update_char_value` (chunk 239 B, RecordDone/live bundles smaller),
and CPU2 behavior for shorter-than-declared updates on a fixed-length attribute is not
documented — a wrong guess here maps to `OtherFailure` and kills the whole pipe. The 0x08
mask is the P1 value-add; the length-type token is orthogonal to it.

Also at the dispatch site (`custom_stm.cpp` notification-complete VS event): the shared
completion counter is incremented only when `Attr_Handle` matches the PipeDataTx *value*
handle (declared handle + `CHARACTERISTIC_VALUE_ATTRIBUTE_OFFSET`; the event reports the
updated characteristic **value** handle per `ble_events.h:1541`). Hardening so a future
0x08 characteristic cannot mis-wake the upload pump or live gate.

The other four Node characteristics stay at 0x07. The Node's completion plumbing already
exists and is dead code today: `custom_stm.cpp:400-411` → `Custom_APP_NotificationComplete`
(`custom_app.cpp:325-328`) → counter shadows in `node/Core/Src/main.cpp:470-479, 802-812`
(separate `g_node_upload_seen_*` / `g_node_live_seen_*` — the documented dual-wake
regression shape is already structurally safe).

### 3.2 Upload pump: pool-budget top-up

`firmware/common/inc/exo/ble/node_upload_pump.h`:
- New private `uint16_t pending_pool_buffers_` with `kUnboundedPool = 0xFFFFU`.
- `on_tx_pool_available(n)`: `pending_pool_buffers_ = n` (the count free at event time;
  the newest event supersedes), then wake.
- `on_notification_complete()`: `pending_pool_buffers_ = kUnboundedPool` (a completed
  flush means the pool state is unknown-but-nonempty; burst until the stack refuses).
- `set_credit()` / `start()`: unbounded (current behavior preserved).
- `on_send_result(Success)`: consume one from the budget when bounded;
  `send_ready_ = (credit_ != 0) && (budget != 0)`; when the budget reaches 0, re-block
  (awaiting the next controller event) **without** an extra blind send that would return
  `INSUFFICIENT_RESOURCES`.
- `on_send_result(Busy/Insufficient)`: `pending_pool_buffers_ = 0` + block (unchanged
  blocking, fresh counts come from the next event).
- Watchdog wake path unchanged (750 ms recovery, never the clock).

Steady state after the change: each transmitted notification produces a completion event;
the pump tops the pool back up on every wake; the radio never sees an empty queue, and
the app no longer pays one `INSUFFICIENT_RESOURCES` round trip per accepted chunk.

### 3.3 Bulk connection-event hint at all ladder levels

`firmware/common/inc/exo/ble/link_tune_state.h`:
- `max_ce_length()`: `fast_interval == kBulkInterval` returns `kBulkMaxCeLength` at
  **every** fallback level, not only level 0. A CE hint larger than the eventually
  granted interval is legal — the controller clamps to what fits — and it is exactly the
  "tell the scheduler it may use the window" fix the live path already validated in
  reverse (small CE budgets per leaf, `kLiveMinCeLength/kLiveMaxCeLength`).
- `kBulkMaxCeLength 0x0030 (30 ms) → 0x0020 (20 ms)`: the CE hint is expressed in
  0.625 ms units while the interval is in 1.25 ms units, so the header's own
  `static_assert(kBulkMaxCeLength <= 2*kBulkInterval)` is the unit bridge "CE ≤ one
  interval". At the old 24-unit bulk interval it held **at equality** (48 ≤ 48); at 16
  units it must become `32 ≤ 32` — the hint now saturates the owned 20 ms interval,
  which is the same effective clamp the controller would apply to the 30 ms hint.
- Live behavior unchanged (`fast_interval != kBulkInterval` keeps `kLiveMaxCeLength`).

### 3.4 Bulk owned interval 30 ms → 20 ms

`firmware/common/inc/exo/protocol/record_transfer_tuning.h`:
- `kBulkFastInterval 24U → 16U` (20 ms in 1.25 ms units); `16U` added to the supported
  set via `kBulkFastInterval` membership. The 20 ms value is the fleet-proven grantable
  interval (40 ms anchor ÷ 2, observed confirmations) and packs ~13 notifications/CE at
  2M — past the throughput plateau — while making the level-0 exact request grantable
  instead of 0x84-rejected.
- Wire contract unchanged: `kDefaultFastInterval` stays 12 (15 ms), version-1 frames
  unaffected; the browser's 0xB5 byte remains advisory (bulk is firmware-owned).
- `firmware/node/Core/Src/main.cpp` static_assert at `:329-331` re-derived for the 20 ms
  profile: `192 B × 16 chunks × 1000 / 20 ms = 153.6 KB/s ≥ 100 KB/s` — updated constant
  and comment; the static_assert in `link_tune_state.h` (`kBulkMaxCeLength ≤ 2×interval`,
  the 0.625 ms↔1.25 ms unit bridge) holds at the new `32 ≤ 32` equality.

## 4. Invariants honored

- Chunk ceilings re-derived, unchanged: 244 − 22 − 25 = **197 B ceiling**, payload stays
  **192 B** (5 B MTU-negotiation margin preserved).
- MTU 247 / DLE 251 / 0x0848 µs / 2M PHY unchanged; node_id 1–4; live queue interval
  clamp [40, 80] ms untouched; ESOX v4 untouched; no `hci_le_set_event_mask` call added
  anywhere (Master advertising invariant).
- `kRecordReliableDefaultCredit` 24 and Master receiver credit cap 24 unchanged.
- Live path: suppression while pump active unchanged; separate seen-counters unchanged.
  Re-test live 25 Hz after flashing (documented caution in §6).

## 5. Expected results

| Scenario | Before | After (expected) |
|---|---|---|
| Bulk upload steady state | ~37–50 chunks/s (7–9 KB/s) | 300–650 chunks/s (60–125 KB/s), bounded by granted CE packing |
| Transfer start latency | +150 ms ladder rejections | immediate 20 ms grant at 40 ms anchor |
| `INSUFFICIENT_RESOURCES` per accepted chunk | ~1.1 | ~0–1 per CE burst |
| Live 25 Hz path | unaffected | unaffected (separate gate; re-verify) |

## 6. Verification plan (user-run; agents never flash)

1. CubeIDE build both projects (sequential, workspace lock); confirm zero new warnings.
2. Flash Node + Master; run a record session; upload one node.
3. Master console gates (already on the wire, no SWO needed):
   - `[BLE][NODE][UPLOAD] ... accepted=N busy=N res=N complete=N pool=N/M watchdog=N` —
     success criteria: `res` grows ~1 per burst instead of ~1 per chunk,
     `complete` climbs during upload (0x08 live), `watchdog` stays ~0,
     `accepted_bytes/s ≥ 30 KB/s` (target stretch: 60–120 KB/s).
   - `[BLE][HUB][LINK] issue ... proc=2 iv=16-16 ce=0-32` then `... confirmed interval=16` —
     the exact 20 ms request granted at level 0 (check the LINK line status `st=0x00`).
4. Node `TP1`/`TP0` flood mode (`node_throughput_test_process()`) isolates the radio path
   if the framing pipeline still limits: raw PipeDataTx writes, no reliable protocol.
5. Live regression: 60 s Qualification after the change (both consumers now wake on the
   0x08 event; shadows are separate, but this is the documented re-test).
6. Host tests in CubeIDE/cmake: `test_node_upload_pump` (extended with budget cases and,
   since 2026-09-13 second pass, watchdog-recovery-from-spent-budget / start()-unbounded
   burst / zero-capacity-report-no-reopen cases), `test_link_tune_state` (extended with
   bulk-CE-at-fallback cases).

## 8. Independent verification record (second agent, 2026-09-13)

Cross-checks performed on the committed diff (`49955b8` + `561b77f`), all static
(agents never build/flash):

- **0x08 handle filter verified against the official stack**: STM32CubeWB v1.24.0
  `ble_events.h:1541` states `Attr_Handle` = "Handle of the updated characteristic
  value"; both projects define `CHARACTERISTIC_VALUE_ATTRIBUTE_OFFSET 1` and
  `CHARACTERISTIC_DESCRIPTOR_ATTRIBUTE_OFFSET 2` (fleet-proven via the write/CCCD
  handlers), so `CustomPipedatatxHdle + 1` is the value handle. The Master's
  completion forward is unconditional (its only 0x08 characteristic is PipeDataTx).
- **`CFG_TLBLE_EVT_QUEUE_LENGTH 5` needs no change**: ST's own `BLE_DataThroughput`
  reference app (the ~1.4 Mbps measurement source) ships the same value, so the
  per-notification 0x08 event load is within a config ST validated at full rate.
- **Full-pump state-machine review**: no underflow (sentinel + zero guards), no
  live-lock (every budget-exhaustion path implies a notification in flight, so a
  completion event re-opens; watchdog is the backstop), `stop()/start()` symmetric.
  Reviewer verdict: SAFE-TO-BUILD, no P0.
- **Master receive path assessed capable of the new rates**: cumulative ACK_WINDOW
  every 16 chunks with credit = min(24 − pending, 24) (`master_training_csv_coordinator.h`),
  ACK latency ≤ 1 superloop pass (`main.cpp:3490→3498`), SD writes buffered in a 4 KiB
  staging buffer (one `f_write` per ~21 chunks, no per-chunk `f_sync`,
  `master_node_session_stager.h`). Bottleneck watch: SPI1 polling-mode SD flush
  duration (`sd_flush_max_duration_ms` in the 0xB6 report) on slow cards — the 24-deep
  pending ring converts SD latency into credit backpressure (graceful, not corrupting).
- **Environment note re-confirmed**: `gcc` has no cc1plus and there is no zig/g++ on
  this PC — the C++ host tests can only be compiled in the STM32CubeIDE environment
  by the user (AGENTS.md already documents this).
- **Residual bench-checks (unchanged from §6/§7)**: grant behavior at the 40 ms anchor,
  CPU2 0x08 emission cadence, and the P2 "hint-induced 0x86 on non-40 ms anchors"
  (if the ladder shows 0x86 at fallback levels, clamp the CE hint to the requested
  window max per level).

## 7. Residual risks

- The CPU2 scheduler may still cap packets/CE below the CE hint; if measured rate
  stalls below ~30 KB/s with `complete` climbing and `res` ≈ 1/burst, the remaining
  lever is the capability-negotiated L2CAP CoC bulk lane (already documented as the
  next step; deliberately not implemented here — larger blast radius).
- The 0x08 event adds ~1 event per transmitted notification to the HCI event queue
  (`CFG_TLBLE_EVT_QUEUE_LENGTH 5` × 255 B): same order as the existing TX-pool events,
  but watch for `TL_BLE_HCI_ToNot` warnings in a long upload (would show as HCI timeout,
  not data corruption).
- The `Pipedatatx` handle filter uses the declared handle + 1 (value handle, per
  `ble_events.h:1541`); if a future WPAN template regeneration changes the attribute
  layout, re-check the comparison.
- Another agent may be working the same tree; changes are confined to five files +
  docs, committed in separate commits for clean review.
