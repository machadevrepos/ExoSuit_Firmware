# Twelve-Node Dual-WB55 Master Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scale the suit from four sensor nodes to twelve while preserving the existing BLE protocol, recording format, and four-node workflows. Use U9 as the system owner and phone-facing master, and U11 as a six-node lower-hub bridge.

**Architecture:** U9 owns nodes 1–6, the phone link, SD card, local sensors, actuators, commissioning, topology, and recording coordination. U11 owns nodes 7–12 and relays BLEPipe traffic over a framed USART1 link. The existing master central logic is first extracted into a shared six-slot core and then used by thin U9 and U11 adapters. Live traffic is best-effort and freshness-oriented; control, commissioning, synchronization, and recording traffic is reliable and backpressured.

**Tech stack:** STM32WB55, STM32_WPAN on CPU2, HAL V1.14.7, STM32Cube FW_WB V1.24.0, C++ superloops with `UTIL_Sequencer`, BLEPipe, COBS-framed USART1, browser JavaScript, Python converter/tests.

**Source specification:** [`docs/MD_Files/ExoSuit_TwoSTM_Master_Bridge_Implementation_Guide.md`](../../MD_Files/ExoSuit_TwoSTM_Master_Bridge_Implementation_Guide.md)

## Status and Decisions

This plan incorporates the repository review and the V1.1 hardware review. The AI-generated source guide is directionally feasible, but must not be implemented unchanged.

Confirmed decisions:

- The production target is one phone-facing master plus twelve leaf nodes.
- U9 owns node IDs 1–6; U11 owns node IDs 7–12.
- U9 remains operational in a degraded six-node mode if U11 is unavailable.
- U11 gains an independent hardware watchdog.
- Existing four-node behavior and packet parsing remain backward compatible.
- The required live profile is 25 Hz with both BNO and ICM data.
- A 50 Hz live profile is a stretch experiment, not a baseline requirement.
- Local sensor recording rates and the ESOX v4 on-disk format remain unchanged.
- U11 recording relay may follow live integration, but it is required before production acceptance.
- Body-segment mapping stays in the application layer. Model retraining, AI-selected haptics, exercise-specific sensor identity, and a polished full-body avatar are out of scope.

Known repository constraints that invalidate parts of the original guide:

- Central link storage is currently fixed at four in `link_tune_state.h`, `hub_leaf_ble_manager.h`, and `exo_hub_central_client.cpp`.
- Node runtime configuration currently accepts IDs 1–4 only.
- The live tool displays source IDs 0–4 and its model path expects arm sensors 2, 3, and 4.
- The desktop tool and converter reject IDs above 4; the converter filename parser only accepts a single digit from 1–4.
- Recording selection masks are currently eight-bit values.
- BLE V2 already carries a 16-bit node ID and does not require a breaking envelope change.
- Current live bundles use marker `0x03`; current live scheduling is 40 ms, or 25 Hz.
- U9 and U11 can both hear the same advertisements, so ownership filtering and commissioning isolation are mandatory.

## Global Constraints

- Do not build or flash firmware in the agent environment. The user performs sequential STM32CubeIDE builds and all hardware flashing.
- Do not casually regenerate `.ioc` files or edit generated/vendor regions. Any generated-file deviation must be minimal, documented, and repeatable.
- Preserve MTU 247, DLE 251, and 2M PHY tuning per link.
- Preserve the framing-derived payload ceilings: 192-byte reliable Node-to-Master payload and 180-byte Master local-transfer chunk.
- Preserve GATT event masks: PipeDataTx `0x0F`; Node other characteristics `0x07`. Keep variable-length characteristic types.
- Never call `hci_le_set_event_mask` on the Master.
- Preserve ESOX v4: magic `ESOX`, 88-byte header, sample format version 4.
- Preserve consumer-side ICM scaling exactly in browser and Python code.
- Keep the production live interval clamp at 40–80 ms. A 50 Hz experiment requires a separate reviewed change that re-derives capacity and updates the invariant and tests; it must not silently change the production default.
- Treat the physical STM32WB55 as G-grade silicon while retaining the repository's conservative CPU1 linker budgets: 768 KiB flash and approximately 192 KiB RAM plus the existing shared-memory reservation.
- Do not merge the twelve-node design merely because it compiles. The production gates at the end of this plan decide whether the architecture is accepted.

## Frozen Interface Contracts

### Topology types

Add a shared topology header under `firmware/common/inc/exo/types/` with these semantics:

```cpp
using NodeId = uint8_t;
using SourceMask = uint16_t;

constexpr NodeId kUncommissionedNodeId = 0U;
constexpr NodeId kFirstNodeId = 1U;
constexpr NodeId kLastNodeId = 12U;
constexpr uint8_t kNodesPerHub = 6U;

enum class HubId : uint8_t { Main = 1U, Lower = 2U };

constexpr bool is_valid_node_id(NodeId id);
constexpr HubId hub_for_node(NodeId id);       // 1–6 Main, 7–12 Lower
constexpr SourceMask source_bit(uint8_t source_id); // bit 0 Master, bits 1–12 Nodes
```

All mask manipulation must use helpers. Do not leave raw `1U << node_id` expressions scattered through firmware or host code.

### BLE identifiers and compatibility

- Retain the current BLE V2 envelope marker `0xB1` byte-for-byte.
- Define `BLEPIPE_ID_HUB2 = 0x0002`.
- Derive leaf addresses from the node ID through one checked helper; reject IDs outside 1–12.
- Continue accepting legacy four-node control/session messages.
- Introduce explicitly versioned 16-bit-mask control/session/topology messages. Do not reinterpret the length of a legacy message.

Recommended new recording command layout, encoded field-by-field in little-endian order:

```text
START_SESSION_V2:
  command                 u8
  protocol_version (=2)   u8
  session_id              u32
  start_timestamp_us      u64
  safety_duration_ms      u32
  selected_source_mask    u16
  stream_interval_ms      u8
```

Assign message IDs only after checking the complete protocol enum for collisions. Add compile-time size assertions and golden-byte tests for every new wire message.

### Node live bundle

- Continue accepting legacy marker `0x03`.
- Add marker `0x04` for a timestamped bundle.
- Encode and decode fields explicitly; do not transmit a compiler-packed C++ struct.

```text
LIVE_BUNDLE_V2:
  marker (=0x04)          u8
  flags                   u8
  bno_payload_length      u8
  icm_payload_length      u8
  bundle_sequence         u32
  bno_acquired_ms         u32
  icm_acquired_ms         u32
  bno_payload             bytes
  icm_payload             bytes
```

With the current 56-byte BNO and 20-byte ICM payloads this is 92 bytes, below the existing BLE payload ceiling. Acquisition timestamps come from the node clock and must not be replaced with hub receipt times.

### U9–U11 bridge

USART1 wiring from the V1.1 schematic:

- U9 `PB6/USART1_TX` to U11 `PB7/USART1_RX`.
- U11 `PB6/USART1_TX` to U9 `PB7/USART1_RX`.
- U9 `PA8/USART1_CK` to U11 `PB5/USART1_CK` is optional and unused by the baseline asynchronous link.

Baseline configuration: asynchronous USART1 at 921600 baud, DMA RX/TX, explicit error recovery, and a hardware-independent codec testable on the host.

Each wire frame is:

```text
COBS(
  version                 u8
  lane                    u8       // live or reliable
  flags                   u16      // reliable, relayed, diagnostic bits
  boot_epoch              u32
  sequence                u32
  payload_length          u16
  BLEPipe frame           bytes
  CRC32                   u32      // covers header and BLEPipe frame
) 0x00
```

The decoder must validate version, lane, declared length, maximum length, CRC, and sequence before dispatch. A malformed frame is dropped and counted; the next delimiter must restore decoding without a reboot.

Traffic policy:

- Live lane: one latest-value slot per source, overwrite stale unsent data, count overwrites and age, and never block the reliable lane.
- Reliable lane: bounded FIFO with acknowledgements/retries at the owning protocol layer, explicit backpressure, and no silent drops.
- Reliable traffic always has scheduling priority, but rate limiting must prevent it from permanently starving live status.
- Every bridge status report includes boot epoch, last RX/TX sequence, CRC/framing/sequence errors, queue high-water marks, live overwrites, reliable retries, and clock-sync quality.

### Time model

- U9 is the suit time authority.
- Synchronize U11 to U9 with periodic request/response samples and use a filtered offset estimate.
- Preserve node acquisition timestamps across both hops.
- Handle 32-bit millisecond wrap explicitly.
- Report raw acquisition time, normalized suit time, arrival time, age, and current offset uncertainty in diagnostics.
- A U11 boot-epoch change invalidates outstanding reliable operations and triggers topology/session recovery.

### Commissioning

- Factory-blank nodes use ID 0, advertise as uncommissioned, and do not enter normal live streaming.
- U9 commissions exactly one selected candidate by BLE address: connect, write ID, read back, verify, disconnect, then allow normal reconnection.
- U9 accepts/initiates leaf ownership only for IDs 1–6; U11 only for IDs 7–12.
- Duplicate IDs are a hard topology fault surfaced to the host. Neither hub may silently choose one duplicate.
- Body-segment assignments are stored by the application against stable node IDs; they are not firmware exercise roles.

## Review Focus Before Each Phase

Reviewers must explicitly check these failure classes:

1. Duplicate/uncommissioned nodes and the race caused by both hubs hearing every advertisement.
2. UART corruption, partial frames, queue saturation, sequence gaps, and U11 resets.
3. Legacy packet-length ambiguity and accidental reinterpretation of old messages.
4. Recording data blocking live/control traffic or exhausting RAM.
5. Timestamp wrap, U11 offset drift, stale packets, and incorrect age calculation.
6. CPU1 RAM, shared mailbox, CPU2 BLE pool sizing, and actual concurrent-link behavior.

## Task 1: Correct the Design Guide and Freeze the Hardware Preconditions

**Files:**

- Modify: `docs/MD_Files/ExoSuit_TwoSTM_Master_Bridge_Implementation_Guide.md`
- Reference: `docs/ExoSkeleton Master PCB V1.1.pdf`
- Reference: `firmware/common/build/linker/STM32WB55_FLASH.ld`
- Test: documentation review against this plan

- [ ] Replace four-node-only claims with the repository findings listed above.
- [ ] Correct the USART wiring and asynchronous baseline.
- [ ] Correct memory acceptance criteria to the actual linker budgets and BLE pool configuration.
- [ ] Add backward compatibility, commissioning ownership, time synchronization, queue policy, diagnostics, watchdog, and recording-relay requirements.
- [ ] Mark 25 Hz BNO+ICM as required and 50 Hz as a separately gated experiment.
- [ ] Record the pre-flash hardware checks: U9/U11 rail stability, reset pins, HSE/LSE, RF matching/antenna continuity, USART continuity and idle level, optional clock-line continuity, SWD access, and current draw.
- [ ] Stop implementation if USART routing, SWD recovery, power integrity, or both radios cannot be independently verified.

**Done when:** The guide and this plan agree, and the user has recorded hardware preflight results.

## Task 2: Add Topology Types and Versioned Protocol Foundations

**Files:**

- Create: `firmware/common/inc/exo/types/topology.h`
- Modify: `firmware/common/inc/exo/protocol/blepipe_proto.h`
- Modify: `firmware/common/inc/exo/protocol/ble_record_protocol.h`
- Modify: relevant shared protocol codecs and tests under `host/tests/cpp/` or firmware module tests

- [ ] Write failing tests for valid IDs 1–12, invalid IDs 0 and 13, hub ownership, and source bits 0–12.
- [ ] Add `NodeId`, `SourceMask`, ownership helpers, checked BLEPipe address helpers, and `BLEPIPE_ID_HUB2`.
- [ ] Write golden-byte compatibility tests for all legacy four-node messages before changing a codec.
- [ ] Add versioned 16-bit-mask session, control, and topology messages with collision-free IDs.
- [ ] Add round-trip and malformed-length tests for every new message.
- [ ] Audit all fixed arrays, loops, switch statements, bit shifts, filename validators, and display filters that encode a four-node assumption; capture them in a checklist before replacement.

**Done when:** Protocol tests prove old messages are unchanged and new messages safely represent all twelve nodes plus the master.

## Task 3: Extend Node Identity and Timestamped Live Bundles

**Files:**

- Modify: `firmware/node/` runtime configuration and application code
- Modify: `firmware/common/inc/exo/` live-bundle codec
- Modify: node and host-side protocol tests

- [ ] Write tests for ID 0 commissioning mode and IDs 1–12 normal mode.
- [ ] Permit persistent IDs 1–12 while rejecting every other commissioned value.
- [ ] Ensure ID 0 never emits normal live or recording traffic.
- [ ] Implement `LIVE_BUNDLE_V2` marker `0x04`, sequence, and per-sensor acquisition timestamps.
- [ ] Retain decoding/handling of legacy marker `0x03` during migration.
- [ ] Preserve the existing 25 Hz live schedule and all local recording rates.
- [ ] Verify encoded bundle length remains inside current characteristic and BLEPipe ceilings.

**Done when:** One node can be commissioned anywhere in 1–12, emits timestamped live bundles, and remains compatible with a legacy four-node master build where applicable.

## Task 4: Extract a Shared Six-Link Hub Core

**Files:**

- Modify: `firmware/common/inc/exo/ble/link_tune_state.h`
- Modify: `firmware/common/inc/exo/ble/hub_leaf_ble_manager.h`
- Modify: `firmware/master/Core/Src/exo_hub_central_client.cpp`
- Create/modify: shared hub-core headers under `firmware/common/inc/exo/ble/`
- Modify: associated module tests

- [ ] Characterize the current four-link state machine with tests before refactoring.
- [ ] Extract discovery, connection ownership, characteristic discovery, GATT subscription, MTU/DLE/PHY/interval tuning, reconnect policy, control routing, and diagnostics into one six-slot core.
- [ ] Keep platform callbacks and generated WPAN glue in thin adapters.
- [ ] Preserve the current event masks, variable characteristic lengths, chunk ceilings, and Master advertising behavior.
- [ ] Restore the existing four-node behavior using the shared core before increasing any configured count.
- [ ] Increase U9 ownership to six and reject IDs 7–12 at the U9 adapter boundary.
- [ ] Recalculate CPU1 static/dynamic RAM, CPU2 BLE pool sizing, shared mailbox use, and per-link buffers for six central links plus the phone peripheral.
- [ ] Add tests for six simultaneous slots, reconnect storms, duplicate IDs, out-of-range ownership, and one slow/failing leaf.

**Done when:** U9 passes the existing four-node regression and a six-slot host/module simulation without duplicated central logic.

## Task 5: Implement the Bridge Codec, Queues, and Clock Synchronization

**Files:**

- Create: shared bridge codec/queue/time-sync headers under `firmware/common/inc/exo/bridge/`
- Create: host-portable tests for COBS, CRC, sequence, queues, and time synchronization

- [ ] Write failing golden-vector tests for empty, minimum, maximum, escaped-zero, corrupt-CRC, truncated, oversized, duplicate-sequence, and sequence-gap frames.
- [ ] Implement the COBS record format and CRC32 without heap allocation.
- [ ] Implement incremental decoding across arbitrary DMA-buffer boundaries.
- [ ] Implement per-source latest-value live slots and a bounded reliable FIFO.
- [ ] Add priority scheduling, backpressure, counters, and queue high-water marks.
- [ ] Add boot epochs and sequence tracking in both directions.
- [ ] Implement U9-authoritative request/response clock synchronization with wrap-safe arithmetic and uncertainty reporting.
- [ ] Add a synthetic traffic generator capable of twelve 25 Hz timestamped streams without physical nodes.

**Done when:** Portable tests prove recovery from arbitrary corruption/boundaries and show reliable traffic cannot be silently dropped or blocked by live traffic.

## Task 6: Create and Integrate the U11 Lower-Hub Firmware

**Files:**

- Create: `firmware/lower_hub/` CubeIDE project from the compatible master hardware configuration
- Reuse: shared six-link hub core and bridge modules
- Document: generated-code deviations and project creation steps

- [ ] Create the project with the correct STM32WB55 target, clocks, BLE stack, memory layout, USART1 pins, DMA channels, interrupts, and SWD configuration.
- [ ] Keep phone GATT, SD, local-sensor, and U9-only application features out of U11.
- [ ] Configure exactly six central links and accept only IDs 7–12.
- [ ] Integrate DMA bridge RX/TX, shared hub core, bridge diagnostics, and boot epoch.
- [ ] Enable and service the hardware IWDG only after startup has reached a recoverable state.
- [ ] Ensure watchdog service depends on forward progress of BLE event pumping and bridge processing rather than an unconditional loop kick.
- [ ] Add startup and fault status messages so U9 can distinguish absent, booting, healthy, congested, and restarted states.

**Done when:** CubeIDE syntax/build checks pass on the user's machine and U11 independently connects to owned nodes and exchanges framed traffic with a bridge test peer.

## Task 7: Integrate U11 into U9 and Preserve Graceful Degradation

**Files:**

- Modify: `firmware/master/` main application, routing, topology, diagnostics, and control paths
- Reuse: shared bridge and protocol modules

- [ ] Add U11 discovery/heartbeat and a topology table covering sources 0–12.
- [ ] Route U11 live frames into the existing `0xB1` phone envelope without altering legacy envelope bytes.
- [ ] Route control messages by ownership: local to U9 for 1–6, bridge reliable lane for 7–12.
- [ ] Surface U11 state, bridge counters, per-node age/loss, tuning state, and ownership faults to the host.
- [ ] On U11 loss, keep U9, the phone link, nodes 1–6, local sensors, and safe local actuation operational.
- [ ] On U11 return or boot-epoch change, rebuild topology and reject stale reliable replies.
- [ ] Prove synthetic twelve-source traffic cannot starve phone control or BLE event pumping.

**Done when:** U9 exposes one coherent twelve-node topology and continues safely in six-node degraded mode during U11 disconnect/reset.

## Task 8: Generalize the Live Tool Without Expanding the ML Scope

**Files:**

- Modify: `host/live_tool/js/ble-protocol.js`
- Modify: `host/live_tool/js/live-inference.js`
- Modify: `host/live_tool/js/motion-engine.js` only where transport/source enumeration requires it
- Modify/create: browser protocol and fixture tests

- [ ] Add fixtures for source IDs 0–12, legacy bundles, timestamped bundles, unknown versions, and malformed lengths.
- [ ] Generalize transport parsing, source discovery, connection/status UI, counters, and raw visualization to twelve nodes.
- [ ] Preserve the current model contract and arm-analysis path for sensor IDs 2, 3, and 4.
- [ ] Do not map new node IDs to exercises in firmware or silently feed them into the existing model.
- [ ] Display acquisition age, sequence gaps, hub ownership, U11 health, and bridge loss separately from BLE leaf loss.
- [ ] Preserve byte-identical ICM scaling.
- [ ] Increment `LIVE_TOOL_BUILD` and verify no stale-build banner appears when served through `host/live_tool/serve.py`.

**Done when:** The live tool displays and diagnoses twelve sources while producing the same arm-model inputs and results for the legacy configuration.

## Task 9: Generalize Desktop Commissioning, Recording Controls, and Conversion

**Files:**

- Modify: `host/desktop_tool/` JavaScript and UI
- Modify: `host/desktop_tool/vantage_bin_to_csv.py`
- Modify/create: `host/tests/python/` tests and fixtures

- [ ] Add tests for IDs 1–12, 16-bit masks, two-digit filenames, duplicate IDs, legacy sessions, and malformed metadata.
- [ ] Replace UI limits of four nodes with topology-driven source enumeration.
- [ ] Add the one-candidate commissioning workflow and explicit duplicate-ID errors.
- [ ] Encode/decode the versioned 16-bit session/control messages while accepting legacy messages.
- [ ] Update filename parsing from the single-digit 1–4 assumption to checked IDs 1–12.
- [ ] Preserve ESOX v4 parsing and all current scaling semantics.
- [ ] Show hub ownership and whether each requested recording source is local, relayed, absent, or faulted.

**Done when:** Python/browser tests accept valid twelve-node artifacts, reject invalid IDs, and still convert existing four-node ESOX v4 sessions identically.

## Task 10: Extend Recording Coordination Across U11

**Files:**

- Modify: shared recording coordinator and protocol modules
- Modify: `firmware/master/` recording/storage integration
- Modify: `firmware/lower_hub/` recording relay
- Modify/create: recording recovery tests

- [ ] Expand coordinator state from four node bits to `SourceMask` without altering ESOX v4 files.
- [ ] Route start/stop/status/list/read/delete operations for nodes 7–12 over the reliable lane.
- [ ] Define acknowledgement, timeout, retry, cancellation, and idempotency behavior for every relayed operation.
- [ ] Chunk relayed data without exceeding the existing 192-byte leaf reliable payload and 180-byte Master local-transfer chunk ceilings.
- [ ] Apply backpressure so recording upload cannot exhaust RAM or block BLE control/event processing.
- [ ] Resume or cleanly restart transfers after UART loss or U11 reboot; never append ambiguous data to a valid session.
- [ ] Record per-source completion/failure and make partial-session status explicit to the host.

**Done when:** A master-plus-twelve synchronized session can be captured, retrieved, converted, and audited, including recovery tests for U11 reset and UART interruption.

## Task 11: Execute the Staged Validation Matrix

### Automated and bench-independent validation

- [ ] Run all available Python tests and plain assertion scripts.
- [ ] Run CubeIDE firmware builds sequentially on the user's machine for Node, U9, and U11.
- [ ] Run firmware translation-unit syntax checks where useful.
- [ ] Inspect map files for flash, CPU1 RAM, shared RAM, stack/heap margins, and BLE pool allocation.
- [ ] Verify no generated/vendor code changed outside documented owned deviations.
- [ ] Run the UART synthetic soak at approximately 60 KiB/s for one hour: zero unexplained reliable loss, deterministic corruption recovery, and p99 bridge ping below 5 ms.

### Four-node hardware stage

- [ ] Use two nodes on U9 and two nodes on U11.
- [ ] Verify commissioning, static ownership, connect/reconnect, MTU/DLE/PHY/interval tuning, timestamped live data, controls, haptics, and U11 watchdog recovery.
- [ ] Verify both browser tools and the converter.
- [ ] Inject UART corruption/disconnect and reset U11 during live traffic and during a reliable transaction.
- [ ] Run twelve synthetic streams concurrently with the four physical nodes to exercise U9/phone scaling.

### Twelve-node required live stage

- [ ] Connect and tune all twelve nodes and keep the phone connected.
- [ ] Run at least 30 minutes at 25 Hz with both BNO and ICM data.
- [ ] Require no disconnect storm, no sustained queue growth, zero unexplained bridge reliable drops, and less than 0.1% end-to-end live bundle loss.
- [ ] Require end-to-end acquisition age p95 below 150 ms and p99 below 250 ms.
- [ ] Require normalized inter-node timestamp skew p95 at or below 20 ms and maximum at or below 60 ms.
- [ ] Attribute every loss/gap to leaf BLE, bridge live overwrite, bridge corruption, phone BLE, or host parsing; unexplained loss fails the gate.

### Recording stage

- [ ] Capture one synchronized session containing the Master and all twelve nodes.
- [ ] Validate every ESOX v4 file and convert it successfully.
- [ ] Verify explicit partial-session reporting.
- [ ] Interrupt UART and reset U11 during upload, then verify retry/recovery without corrupt or duplicated output.

### Stretch 50 Hz experiment

- [ ] Begin only after every 25 Hz gate passes.
- [ ] Use a separate reviewed experimental profile; do not change the production 40–80 ms clamp silently.
- [ ] Recalculate BLE airtime, phone notification capacity, UART load, queue sizes, and timestamp-age targets.
- [ ] Treat failure as a profile limitation, not as failure of the required twelve-node architecture.

### Production acceptance

- [ ] Run a 24-hour soak with all twelve nodes at the required 25 Hz profile and the application connected.
- [ ] Include movement, realistic body placement, RF interference, reconnects, and periodic recordings/uploads.
- [ ] Reject the architecture for any spontaneous reset, corrupt/missing session, memory drift, stuck link, unexplained resynchronization growth, unbounded queue, or violation of a required timing/loss gate.
- [ ] Update architecture, protocol, commissioning, diagnostics, recovery, and hardware-validation documentation with measured results.

## Completion Evidence

Before marking this plan complete, attach or record:

- CubeIDE build results and memory-map summaries for all three firmware images.
- Automated test command output and fixture versions.
- Four-node and twelve-node run logs.
- Bridge soak statistics and injected-fault results.
- Per-link BLE tuning state and disconnect/reconnect counts.
- Queue high-water marks, loss attribution, timestamp age/skew distributions, and clock-sync uncertainty.
- Recording inventory, validation output, and conversion results.
- A final pass/fail table for every production gate above.

## Handoff Notes

- Begin with Task 1 and do not modify firmware until the hardware preconditions and protocol contracts are reviewed.
- Keep commits phase-scoped so the four-node regression, shared-core extraction, bridge transport, U11 project, host tooling, and recording relay can be reviewed independently.
- If measured hardware or BLE limits contradict this design, stop at the failing gate and document the evidence. Do not hide the issue with larger unbounded buffers, silent drops, or relaxed success criteria.
