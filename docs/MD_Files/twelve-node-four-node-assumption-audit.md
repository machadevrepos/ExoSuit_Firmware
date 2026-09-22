# Twelve-node migration audit: four-node assumptions

Audit baseline: 2026-09-22, after protocol-foundation commits through
`cf7594d`. This is an inventory only. The entries below must be converted in
their owning plan task; this file prevents a broad, mixed-purpose replacement.

## Shared firmware limits to replace

- [ ] `firmware/common/inc/exo/storage/node_runtime_config.h:10,106` —
  persistent Node ID range is currently 1–4. Owner: Task 3. Preserve ID 0 as
  commissioning/uncommissioned and move the limit to the shared topology
  helpers.
- [ ] `firmware/common/inc/exo/ble/link_tune_state.h:43,930` — four link slots
  and Node ID validation derived from the link count. Owner: Task 4. Extract
  the six-slot hub core before increasing U9 ownership.
- [ ] `firmware/common/inc/exo/ble/hub_leaf_ble_manager.h:399-528` —
  `kMaxLeaves=4`, four node slots, four source queues, and four per-node live
  counters. Owner: Task 4. Keep this state machine behavior-compatible before
  changing the capacity.
- [ ] `firmware/common/inc/exo/protocol/master_training_csv_coordinator.h` —
  Node ID checks, `uint8_t` source masks, raw `1U << node_id` operations, and
  four-node completion/failure cleanup paths. Owner: Task 10. Replace with
  `SourceMask` helpers without changing ESOX v4.
- [ ] `firmware/common/inc/exo/protocol/master_node_transfer_window.h:30` and
  `master_node_reliable_control.h:65,182` — reliable recording transfer
  validation accepts only Node 1–4. Owner: Task 10.
- [ ] `firmware/common/inc/exo/protocol/node_transfer_chunk_counters.h:20` —
  source validation accepts only Node 1–4. Owner: Task 10.
- [ ] `firmware/common/inc/exo/storage/master_node_session_stager.h:104` —
  staged recording completion accepts only Node 1–4. Owner: Task 10.
- [ ] `firmware/common/inc/exo/sensors/master_training_csv_formatter.h:54-58`
  — source validation and labels contain only Master plus Node 1–4. Owner:
  Task 9/10. Preserve the existing CSV columns and extend source enumeration
  deliberately.
- [ ] `firmware/common/inc/exo/sensors/master_training_csv_logger.h:315`
  — `kAllSourceBits=0x1F`, five source counters, and raw source shifts. Owner:
  Task 10.

## Master runtime limits to replace after shared-core extraction

- [ ] `firmware/master/Core/Src/main.cpp:423-438` — recording completion and
  verification arrays sized for four nodes. Owner: Task 10.
- [ ] `firmware/master/Core/Src/main.cpp:1379-1556,2705-2792,4502-4525`
  — Node 1–4 validation around record-done, reliable frames, verification,
  and staged-source indexing. Owner: Task 10.
- [ ] `firmware/master/Core/Src/main.cpp:2857,3616-3690,4954` — four-entry
  loops and 8-bit source-mask operations in recording coordination. Owner:
  Task 10.
- [ ] `firmware/master/Core/Src/ble/exo_hub_central_client.cpp:600-650,1247,
  1932-1963,2610` — raw 8-bit node masks and Node 1–4 discovery/ownership
  checks. Owner: Task 4/7. U9 must ultimately accept only Node 1–6.
- [ ] `firmware/master/Core/Src/main.cpp:1723-1733` — compact source-mask
  helper currently only represents source IDs below 8. Owner: Task 7/10.

## Host and conversion limits

- [ ] `host/tests/python/validate_training_csv.py:48,113` — CSV validation
  rejects sources above 4. Owner: Task 9.
- [ ] `host/desktop_tool/vantage_bin_to_csv.py:126` — ESOX source validation
  rejects Node IDs above 4. Owner: Task 9. Keep ESOX v4 parsing unchanged.
- [ ] `host/live_tool/js/ble-protocol.js:121-132` — `NODE_IDS=[2,3,4]` is the
  current model-input contract, while `DISPLAY_SOURCE_IDS=[0,1,2,3,4]` is the
  display filter. Owner: Task 8. Do not feed new lower-body IDs into the
  existing arm model; generalize transport/display enumeration separately.

## Intentional fixed-size code excluded from this audit

- Four-byte arrays and loops in UART, CRC, UUID, quaternion, sensor, and GPIO
  code are byte/protocol/component sizes, not node counts.
- `master_binary_session_index.h:75` parses a four-digit session index; it is
  not a four-node limit. Filename parsing for two-digit Node IDs is a separate
  Task 9 change.
- Existing BLE V2 model streams for Nodes 2–4 remain a compatibility contract;
  they must be preserved while the twelve-source transport is added.

## Replacement rules

- [ ] Every replacement uses `NodeId`, `SourceMask`, `is_valid_node_id()`,
  `hub_owns_node()`, and `source_bit()` rather than new raw shifts or magic
  limits.
- [ ] Each owner task adds boundary tests for Node 0, Node 1, Node 6, Node 7,
  Node 12, and Node 13 before changing the corresponding runtime code.
- [ ] No legacy four-node packet is reinterpreted by length; all new 16-bit-mask
  messages remain explicitly versioned.
