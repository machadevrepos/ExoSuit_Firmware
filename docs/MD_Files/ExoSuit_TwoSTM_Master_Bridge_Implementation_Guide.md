# ExoSuit Two-STM Master Bridge — Implementation & Validation Handbook

**Repo:** `https://github.com/machadevrepos/ExoSuit_Firmware.git` (reviewed at commit `0b93253`, branch `main`)
**Scope of this document:** Scaling the master from "1 hub + ≤4 leaves" to a 12-node suit using the **two STM32WB55CCU6 MCUs already on the Master PCB V1.1**, with no master PCB redesign and no changes to node firmware or the app-facing BLE protocol.
**Audience:** A coding agent (or engineer) that will implement firmware changes and execute the hardware validation plan.
**Status of facts:** Everything in §2–§9 marked ✅ was verified against the repository (schematics PDF, `.ioc` files, header sources). Items marked ⚠️ UNVERIFIED (§10) must be confirmed on hardware before or during the phase where they matter.

---

## 0. How to use this document (implementing agent rules)

1. **Do not redesign the master PCB.** The master board exists as-is; firmware + solder-bridge population checks only.
2. **Do not modify node firmware** (`firmware/node`) for v0. Nodes already behave as connectable leaves; the split is implemented entirely hub-side.
3. **Do not break the app-facing BLE protocol.** The host tools (Web Bluetooth app, `host/`) and the BLE V2 live-stream envelope must keep working unchanged for upper-body nodes at all times.
4. **Keep changes additive.** New modules under `firmware/common/inc/exo/bridge/` (or equivalent), a new U11 project, config bumps on U9. Refactors of existing loop code are out of scope unless a gate forces it.
5. **Verification-first.** Every ⚠️ UNVERIFIED item in §10 is a blocking precondition for the phase that references it. Confirm with the cheapest possible test (continuity, map file, SWD probe) before writing dependent code.
6. **Tests live with the code.** Bench-test scripts and expected outputs should be committed under `firmware/master/tests/` or `scripts/` following existing repo conventions.
7. Repo layout (verified):
   - `firmware/master/` — U9 CubeIDE project (`Master.ioc`, `Core/Src/main.cpp` ≈ 5.4k lines)
   - `firmware/node/` — leaf node project (`Node.ioc`)
   - `firmware/common/inc/exo/` — shared modules: `actuator/ ble/ protocol/ recording/ sensors/ storage/ types/ utils/`
   - `host/` — Web Bluetooth recording/debug/live-inference tools
   - `docs/` — PCB schematics PDFs, requirements DOCXs

---

## 1. Executive summary

- The master PCB already carries **two STM32WB55CCU6** MCUs, each with a complete independent BLE RF chain (own antenna, filter, matching network) and own SWD debug header. Both radios are usable simultaneously.
- **Decision (§3):** U9 ("Main MCU" in schematic, runs today's firmware) stays the **main/app-facing MCU**: upper-body 6 nodes + phone/app link (7 BLE links). U11 ("Sensor MCU", currently idle) becomes the **lower-body hub**: 6 nodes → UART relay to U9. Swapping the roles was evaluated and rejected (§3.3).
- The inter-MCU link is almost certainly **USART1 on pins PB6/PB7** (configured synchronous with CK on PA8 in `Master.ioc` — a master-clocked USART only makes sense chip-to-chip). ⚠️ UNVERIFIED: continuity check U1.
- The UART bridge reuses the existing `blepipe` framing (20 B header + CRC16-CCITT, 222 B max payload) so U9's parser can consume lower-body packets with existing code (§5).
- Bandwidth is **not** the bottleneck for the required 25 Hz BNO+ICM live profile: 12 nodes produce roughly 7.4 KB/s of raw sensor data before envelopes, versus 92 KB/s UART capacity and ≈ 733 kbps measured BLE capacity (§4). A 50 Hz profile is a separately gated stretch experiment; link count, airtime scheduling, and dual-radio contention on the shared PCB remain the real risks (§8).
- Validation is phased with hard gates (§7). Phase 1 (UART bring-up, no nodes needed) answers "can this work?" within ~1–2 weeks. The PCB order decision is gated on a 24 h soak (Phase 4).

---

## 2. Verified hardware facts (Master PCB V1.1, `docs/ExoSkeleton Master PCB V1.1.pdf`)

### 2.1 The two MCUs

| Property | U9 — "Main MCU" | U11 — "Sensor MCU" |
|---|---|---|
| Part | STM32WB55CCU6 (QFN48) | STM32WB55CCU6 (QFN48) |
| Power rail | `3V3_MCU2` (TPS63020 buck-boost 3V3) | `3V3_MCU1` (TPS63020 buck-boost 3V3) |
| RF chain | ✅ RF1 → U10 MLPF-WB55-01E3 → ANT1 (2450AT18B100E) → `MCU2_ANT` | ✅ RF1 → U12 MLPF-WB55-01E3 → ANT2 (2450AT18B100E) → `MCU1_ANT` |
| SWD debug | ✅ header T9–T13: `MCU2_PA13_DIO/CLK`, `MCU2_PB3_SWO`, `MCU2_NRST`, `MCU2_BOOT0` | ✅ header T14–T18: `MCU1_*` equivalents |
| Crystals | ✅ own 32 MHz (Y2) + 32.768 kHz (Y1) | ✅ own 32 MHz (Y4) + 32.768 kHz (Y3) |
| Peripherals wired | BNO085 (I2C3, PA7/PB4, addr 0x4B), ICM-45686 (I2C1, PB8/PB9, addr 0x69), micro-SD slot (SPI), PT2041 touch IC, buzzer + ERM motor + RGB LED, RS485 transceiver (THVD1426) + 6× JST-GH-4P ports (legacy wired bus) | **None currently populated** (despite the "Sensor MCU" name; sensor nets reach it only via unpopulated 0R bridges) |

### 2.2 Sensor nets use 0R solder bridges (routing flexibility exists but is a physical change)

`BNO_SDA/SCL/RST/INT ↔ BNO_*_MCU` (R51–R55, 0R) and `ICM_SDA/SCL/INT1/INT2 ↔ ICM_*_MCU` (R62–R65, 0R). Today's firmware expects BNO+ICM on U9 (I2C3 / I2C1), so the assembled boards are presumed bridged to U9. ⚠️ UNVERIFIED: U3.

### 2.3 UART pin map (from `firmware/master/Master.ioc`, verified)

| UART | Pins (Master.ioc) | Mode / baud | Evidence of role |
|---|---|---|---|
| **USART1** | TX=**PB6**, RX=**PB7**, **CK=PA8** | Synchronous (clock output) | A synchronous USART is only useful chip-to-chip → designed U9↔U11 link. ⚠️ UNVERIFIED U1/U2 (continuity) |
| LPUART1 | TX=PB5, RX=PA3 | Async 115200, overrun disable, DMA1_CH1 | ST WPAN debug-console convention → reserved for logs, do **not** use for the bridge |
| — | Node.ioc uses the same PB6/PB7 async @ 921600 | — | 921600 async is a proven baud on this pin pair (board heritage) |

### 2.4 Firmware-side configuration (verified)

| Key | Value | File |
|---|---|---|
| `STM32_WPAN.CFG_BLE_NUM_LINK` | **6** (master), 2 (node) | `Master.ioc:442`, `Node.ioc:360` |
| `CFG_BLE_MAX_ATT_MTU` | 247 | `Master.ioc:439` |
| `CFG_BLE_DATA_LENGTH_EXTENSION` | 1 (DLE on) | `Master.ioc:438` |
| `CFG_TX_POWER` | 0x1F = 6 dBm | `Master.ioc:451` |
| GAP roles | central **+** peripheral (both) | `Master.ioc` IPParameters |
| Master CPU1 image size | ≈ 124 KiB (comment in `main.cpp:49-52`) — flash headroom is plentiful (1 MB device) | `firmware/master/Core/Src/main.cpp` |
| `EXO_MASTER_BINARY_ONLY_BUILD` | 1 — strips legacy MCU-side training-CSV path; **not** a source-availability limitation | `main.cpp:52` |

### 2.5 Memory (datasheet)

The physical STM32WB55 is the 1 MB G-grade device, but acceptance uses the
repository's conservative CPU1 linker budget: **768 KiB application flash**,
approximately **192 KiB CPU1 RAM**, and the existing **10 KiB shared RAM**
reservation at `0x20030000`. CPU2 owns its BLE memory, so the link map and BLE
pool configuration must both be checked; datasheet capacity alone is not an
acceptance criterion.

The current Master BLE pool is configured for six links, MTU 247, DLE enabled,
68 GATT attributes, 8 GATT services, a 1344-byte attribute-value array, and
`CFG_BLE_MBLOCK_COUNT` derived from the 247-byte prepare-write size plus the
documented 16-block throughput reserve. Increasing U9 to eight links costs
additional CPU2 pool memory and must be accepted only after the linker/map
check in U5.

---

## 3. Architecture decision

### 3.1 Topology (Option A — recommended and adopted)

```
                 chest board (existing, no redesign)
   ┌───────────────────────────────────────────────────────────┐
   │  U9 "Main MCU"                        U11 "Sensor MCU"     │
   │  ┌──────────────────────┐   USART1    ┌────────────────┐ │
   │  │ BLE central: 6 links │  PB6↔PB7    │ BLE central:   │ │
   │  │  = upper nodes 1–6   │←─921600────→│ 6 links =      │ │
   │  │ BLE peripheral:      │  (async)    │ lower nodes    │ │
   │  │  = phone/app (1 link)│             │ 7–12           │ │
   │  │ BNO085 + chest ICM   │             │ (dumb relay)   │ │
   │  │ SD recorder, touch,  │             └───────┬────────┘ │
   │  │ buzzer/ERM/RGB       │                ANT2 │          │
   │  └──────────┬───────────┘                     │          │
   │         ANT1│                                 │          │
   └─────────────┼─────────────────────────────────┼──────────┘
                 │ 2.4 GHz                         │ 2.4 GHz
        ┌────────┴───────┐                  ┌──────┴────────┐
        │ nodes 1–6      │                  │ nodes 7–12    │
        │ (L arm ×3,     │                  │ (L leg ×2,    │
        │  R arm ×3)     │                  │  R leg ×2,    │
        │                │                  │  lower back,  │
        │                │                  │  neck)        │
        └────────────────┘                  └───────────────┘
```

Link accounting: U9 = 6 (upper nodes) + 1 (app) = **7 links** → `CFG_BLE_NUM_LINK` must go 6 → 7 or 8. U11 = **6 links** (unchanged value, new project).

### 3.2 Node ID namespace (suit-wide)

| Range | Body region | Hub | blepipe IDs |
|---|---|---|---|
| 1–6 | upper body (arms) | U9 (direct) | `0x0101`–`0x0106` |
| 7–12 | lower body (legs, lower back, neck) | U11 → UART relay | `0x0107`–`0x010C` |
| Hub endpoints | U9 = `0x0001` (existing `BLEPIPE_ID_HUB`), U11 = **`0x0002` (new, `BLEPIPE_ID_HUB2`)** | | |

Existing `BLEPIPE_ID_LEAF_1..4` (`0x0101`–`0x0104`) extend naturally to `_5`, `_6`, and the new `0x0107`–`0x010C` range. All addressing fields are `uint16_t` — no format changes needed.

### 3.3 Why not swap the roles (Option B — evaluated, rejected)

The proposal "make the idle UART-only STM the main" was analyzed and rejected:

1. **Peripherals define the main, and they are all wired to U9**: SD card (session recorder FATFS pipeline), BNO085 + chest ICM, PT2041 touch IC + power/shutdown control, buzzer/ERM/RGB. Under Option B every one of these becomes a UART proxy → new failure domains, latency on the logging path, and a file-access protocol spanning two MCUs.
2. **Radio load is symmetric**: both MCUs have full RF chains, so the split 6+7 links exists either way. No airtime advantage.
3. **Cross-MCU power management is dangerous**: touch-hold shutdown and `PWR_EN` rail control live on U9. If the "main" were U11 and U9 hung holding the rail, U11 could not recover the system.
4. **Firmware gravity**: the validated 5.4k-line master app (BLEPipe service, record-transfer credit window, `link_tune_state` ladder, notification gate) runs on U9. Option A adds a UART RX module; Option B re-parents the whole app.
5. **The idle-resources intuition is inverted**: U11's free headroom is exactly why it should get the bounded, lightweight job (6 links → DMA UART relay), while the resource-hungry aggregation/orchestration job needs the peripherals, not spare cycles.

### 3.4 What does NOT change

- **Node firmware**: zero changes. Nodes advertise/connect exactly as today; a node does not know which hub it attached to.
- **App-facing protocol**: BLE V2 live envelope (`ble_stream_v2.h`, frame id `0xB1`, 14 B header, `node_id` is `uint16_t`) and `blepipe` service over the air stay byte-identical. Lower-body streams appear to the app as ordinary node streams with IDs 7–12. Existing four-node messages remain accepted byte-for-byte; new twelve-node masks and topology messages are explicitly versioned rather than inferred from legacy lengths.
- **Host tools**: unchanged in v0 (they already tolerate arbitrary `node_id` values; verify when integrating — ⚠️ U8 ledger covers the injection-point format check).

---

## 4. Bandwidth & latency budget (the numbers that justify the design)

### 4.1 Sample sizes (verified struct definitions)

| Struct | Size | Source |
|---|---|---|
| `Bno85SampleV3` | 56 B (`offset_us` + 12 × float) | `firmware/common/inc/exo/types/recording_types.h` |
| `Icm45686SampleV4` | 20 B (accel/gyro/temp packed) | same header — confirm exact `sizeof` with a `static_assert` during implementation (V2 in the header is 26 B; the V4 packed form was measured at 20 B in the earlier deep-dive) |
| BLE V2 live envelope | 14 B header + payload | `firmware/common/inc/exo/protocol/ble_stream_v2.h` |
| blepipe frame | 20 B header + ≤222 B payload + 2 B CRC16 | `firmware/common/inc/exo/protocol/blepipe_proto.h` |

### 4.2 Throughput budget

| Segment | Load | Capacity | Utilization |
|---|---|---|---|
| U11 → U9 UART | lower 6 nodes @ 25 Hz × 20 B = 3 KB/s + ~12 % bridge overhead ≈ **3.4 KB/s** | 92.16 KB/s (921600 8N1) | **~4 %** |
| U11 → U9 UART @ 50 Hz stretch | ≈ 6.7 KB/s | 92.16 KB/s | ~7 % (2 Mbaud remains an optional measured fallback) |
| Node → hub BLE (per hub) | 6 × 25 Hz ≈ 3 KB/s air data (+BLE overhead) | ≈ 733 kbps measured @ 1M PHY; ≈ 1.3 Mbps @ 2M PHY (ST AN5289-class figures from the prior research brief) | comfortable |
| U9 → app BLE | 12 ICM nodes (6 KB/s) + BNO @ 25 Hz (1.4 KB/s) ≈ **7.4 KB/s** raw, with envelope overhead | same as above | fits at the required profile |
| Note on envelopes | If the live path emits one 14 B envelope per sample, per-node overhead is significant; today's validated live cadence is 25 Hz (40 ms) and `exo_hub_central_client.h` comments reference a "40 ms sample cadence". Check how samples are batched per envelope at the injection point (⚠️ U8) before evaluating the separate 50 Hz stretch profile. | | |

### 4.3 Latency budget (design targets, not yet measured)

| Hop | Expected | Basis |
|---|---|---|
| UART hop U11→U9 | ≈ 2.7 ms per 244 B frame @ 921600 + DMA | arithmetic |
| Node → hub BLE | 15–50 ms (conn interval; `link_tune_state` ladder, 30–50 ms multi-link default, faster intervals for live) | existing code comments |
| Hub → app BLE | one conn interval (15–50 ms) | same |
| **End-to-end firmware target** | **p95 < 150 ms** node→browser at live cadence; haptics unaffected (actuation loop is node-local by design and never crosses the bridge) | gate G3.2 |

---

## 5. UART bridge protocol spec v0 ("EXO-BRIDGE")

### 5.1 Physical layer

- **USART1, PB6/PB7, async 8N1, 921600 baud** (default). Cross-wired: U9.PB6 → U11.PB7, U11.PB6 → U9.PB7. PA8/CK is **not used** in v0 (async full-duplex needs only TX/RX — this also removes dependency on unverified CK continuity).
- Stretch: 2 Mbaud if the BER ladder test passes on the real trace (same wiring).
- **DMA on both ends** (the WB55 pattern already exists: `DMA1_CH1` is configured for LPUART1; add channels for USART1). TX = circular ring buffer; RX = circular buffer + idle-line interrupt to delimit bursts.
- No RTS/CTS exists on the board → flow control is **software-only** (§5.4).

### 5.2 Frame format — COBS envelope carrying BLEPipe

The UART transport has an explicit envelope around each BLEPipe frame. This
keeps live freshness policy, reliable backpressure, bridge resets, and UART
corruption recovery separate from the app-facing BLEPipe payload.

```
COBS(
  version                 u8
  lane                    u8       // live or reliable
  flags                   u16      // reliable, relayed, diagnostic bits
  boot_epoch              u32      // changes after a hub reboot
  sequence                u32      // per-lane sender sequence
  payload_length          u16      // BLEPipe frame length
  BLEPipe frame            bytes
  CRC32                   u32      // covers the envelope header and BLEPipe frame
) 0x00
```

The inner BLEPipe frame continues to use the existing
`blepipe_encode()`/`blepipe_decode()`/`blepipe_crc16_ccitt()` helpers and keeps
the app-facing protocol byte-compatible. The outer decoder must validate the
version, lane, declared length, maximum length, CRC, and sequence. A malformed
frame is dropped and the next delimiter must restore decoding without a
reboot.

### 5.3 Message set (subset of `blepipe_msg_type_t` carried over the bridge, v0)

| Direction | msg_type | Use |
|---|---|---|
| U11 → U9 | `BLEPIPE_MSG_LEAF_SAMPLE` (0x01) | live sample batches from lower nodes (payload = whatever the node→hub BLE path carried) |
| U11 → U9 | `BLEPIPE_MSG_LINK_STATS` (0x22) | per-node BLE health of U11's links (interval, retries, PHY, offered/dropped/sent) |
| U11 → U9 | `BLEPIPE_MSG_STATUS` (0x20) | hub status: connected node mask, buffer drops, uptime |
| U9 → U11 | `BLEPIPE_MSG_COMMAND` (0x10) / `BLEPIPE_MSG_STREAM_CONTROL` (0x32) | start/stop live stream, ODR config, targeted reconnect, pass-through commands addressed to specific nodes (dst = node ID, U11 forwards over its BLE links) |
| U11 → U9 | `BLEPIPE_MSG_COMMAND_RESP` (0x11) / `ACK` (0x12) / `NACK` (0x13) | control responses, relayed node acks |
| both | `BLEPIPE_MSG_EVENT` (0x23) | connect/disconnect events for lower nodes (drives app topology view) |
| both | new `BLEPIPE_MSG_BRIDGE_HELLO = 0x50` (extend the enum in a bridge-scoped header, or reuse `DEVICE_INFO` 0x43) | 10 Hz heartbeat: uptime, TX ring high-water, RX resync count, firmware version |

The first bring-up slice is live streams + control + stats only. Session-
recording transfer across the bridge remains a later implementation slice, but
it is required before production acceptance: lower-body recordings must cross
U11 → UART → U9 and be archived without ambiguous or duplicated output.

### 5.4 Flow control, ordering, errors

- **Live lane:** one latest-value slot per source; overwrite stale unsent data, count overwrites and age, and never block the reliable lane.
- **Reliable lane:** bounded FIFO with acknowledgements/retries, explicit backpressure, and no silent drops. Reliable traffic has priority, but rate limiting must prevent it from permanently starving live status.
- **Seq gap detection:** per `(src_id, msg_type)` and outer sequence; expose gaps separately as BLE-side, UART framing, and bridge-queue loss.
- **Resync rule:** validate the outer envelope and inner BLEPipe frame; discard malformed data until the next COBS delimiter. Recovery must not require a reboot or accept a partial frame.
- **Time model:** U9 is the suit time authority. Synchronize U11 with periodic request/response samples and a filtered offset estimate; preserve node acquisition timestamps and report raw time, normalized suit time, arrival time, age, and offset uncertainty. Handle 32-bit millisecond wrap explicitly.
- **Diagnostics:** every bridge status report includes boot epoch, last RX/TX sequence, CRC/framing/sequence errors, queue high-water marks, live overwrites, reliable retries, and clock-sync quality.
- **Deadlock safety:** TX ring writes are non-blocking; RX uses DMA plus idle/half-buffer processing with no polling loops. The U11 hardware watchdog is serviced only after BLE event pumping and bridge processing have both made forward progress.

### 5.5 Why not synchronous mode

`Master.ioc` currently configures USART1 as synchronous (CK=PA8). v0 switches to **async**: (a) works with only TX/RX routed — PA8 continuity is unverified; (b) node firmware proves async 921600 on this pin pair; (c) clocked mode adds a wiring dependency for marginal benefit at these data rates. Only revisit sync if the BER ladder fails at every async baud.

---

## 6. Firmware work plan

### 6.1 U11 — new "lower hub" firmware (relay only, scope frozen)

**Reuse (verified to exist in `firmware/common/inc/exo/`):**

| Module | Reused for |
|---|---|
| `exo/ble/exo_hub_central_client.h` | the entire 6-link BLE central role: scan/connect, `send_blepipe_to_node`, per-leaf telemetry (`leaf_link_interval_raw/state/retries/tx_phy`, `leaf_live_diag`), reconnect callbacks |
| `exo/ble/link_tune_state.h` | DLE/PHY/conn-interval ladder — copy as-is |
| `exo/ble/notification_gate.h` | 10 ms notification watchdog per link |
| `exo/protocol/blepipe_proto.h` | frame encode/decode both to nodes and to the UART |
| node superloop skeleton | `firmware/node/Core/Src/main.cpp` is a closer template for U11 than the master loop (no SD/FATFS/recorder to strip) |

**Strip from the master build:** FATFS + SD recorder, BNO/ICM drivers, `master_sd_session_recorder`, `master_node_session_stager`, training-CSV coordinator, upload pump, haptic sequencing (stays node-local), SWO telemetry (U11 gets UART debug instead).

**New modules (suggest `firmware/common/inc/exo/bridge/`):**

1. `bridge_uart_transport.h` — DMA ring TX + idle-line RX, `bridge_uart_send(const blepipe_hdr_t*, const uint8_t*, uint16_t)`, non-blocking; parser with slide-resync; stats (resyncs, ring high-water).
2. `bridge_relay.h` — node↔UART pump: BLE notifications in → `LEAF_SAMPLE` frames out (src_id preserved, flags.bit0=1, hop_count+1); UART `COMMAND`/`STREAM_CONTROL` in → `exo_hub_central_client_send_blepipe_to_node(...)` out.
3. `bridge_control.h` — 10 Hz `BRIDGE_HELLO`, connected-mask tracking, U11-local stream start/stop state machine driven by U9.

**U11 project config:** new CubeIDE project cloned from `firmware/master` (same chip/package/USART1 pins); `CFG_BLE_NUM_LINK=6`; MTU 247 + DLE on; USART1 async 921600; its own BLE identity (different `AD_TYPE_COMPLETE_LOCAL_NAME`/address so U9's scanner never mistakes it for a node); GAP central only (no peripheral service needed on U11 in v0).

**U11 main loop shape (per superloop iteration):** `exo_hub_central_client_process()` → `bridge_relay_pump()` (BLE→UART) → `bridge_uart_process()` (UART RX parse → relay/control) → `bridge_control_heartbeat()`. No blocking calls anywhere in the loop (existing loops already obey this).

U11's independent hardware IWDG is enabled only after startup reaches a
recoverable state. Its service condition depends on forward progress from both
BLE event pumping and bridge processing; an unconditional loop kick is not
acceptable. Startup, healthy, congested, and restarted states are reported to
U9 with a boot epoch so stale reliable operations can be rejected.

### 6.2 U9 — master firmware changes

1. **`CFG_BLE_NUM_LINK` 6 → 8** in `Master.ioc` (7 needed: 6 upper + 1 app; 8 = margin). Rebuild, then verify the 768 KiB flash / approximately 192 KiB CPU1 RAM linker budget, the 10 KiB shared-RAM reservation, and CPU2 BLE pool headroom in the `.map` (⚠️ U5). If RAM-tight: reduce unused GATT services on the peripheral side or trim pool sizes before accepting fewer links.
2. **New module `bridge_host.h/.cpp` on U9:**
   - UART RX parser (same `bridge_uart_transport` code, opposite role) → decoded frames.
   - **Injection point:** feed `LEAF_SAMPLE` payloads into the same live-stream path that upper-node samples take today, tagged with `src_id` 0x0107–0x010C, so the app sees lower-body nodes as ordinary streams. Re-stamp arrival per the existing forwarded-data pattern. (⚠️ U8: read the exact live-path function in `main.cpp` first; keep the app envelope format untouched.)
   - `LINK_STATS`/`STATUS`/`EVENT` from U11 → surface to app (topology view) and to the debug console.
   - Control path: when a command targets node 7–12 (or "all lower"), wrap in a bridge frame dst=0x0002 instead of calling the local central client.
3. **USART1 mode:** synchronous → asynchronous 921600 in `Master.ioc` (CK/PA8 unused in v0).
4. **Do not touch:** record-transfer credit logic, notification gate, existing leaf state machines, app GATT service.

### 6.3 Node ID assignment / commissioning flow (unchanged mechanism, extended range)

Commissioning stays "pair node → assign suit ID" as today, extended to 12:
factory-blank nodes use ID 0 and never enter normal live streaming; U9 owns
IDs 1–6 and U11 owns IDs 7–12. Each hub commissions one selected candidate by
BLE address, writes the ID, reads it back, verifies it, and only then allows
normal reconnection. Duplicate IDs are a hard topology fault, not a reason to
silently choose one node. The body-map (which ID = which body position)
remains app-side configuration — no firmware files carry position data
(consistent with the stakeholder decision that coaches never modify node
files).

### 6.4 Deferred to v1 (documented, not built now)

- **Recording-over-bridge**: lower nodes record locally (W25Q256) and their session uploads must cross U11 → UART → U9 SD archive. Reuse the existing chunked protocol (`ble_record_protocol` v6, 192 B chunks, credit 24) inside `LEAF_SAMPLE`-style bridge frames; bandwidth is trivial at playback-rate transfer. Only v0 design requirement: keep `bridge_relay` payload-agnostic and reserve msg_type space so v1 adds this without a format break.
- **Cross-MCU sync pulse** (one GPIO bodge wire between U9/U11) only if the Phase 3 calibration dry run shows timestamp skew matters.
- Lower-body haptic choreography coordinated suit-wide (v0 keeps all actuation node-local).

---

## 7. Phased validation plan (gates are hard: no gate pass → no next phase)

### Phase 0 — Desk checks & freeze (no hardware, ~half a day)

| # | Task | How | Pass |
|---|---|---|---|
| P0.1 | ⚠️ U1/U2 continuity: which pins bridge U9↔U11 | DMM/board viz on assembled master: U9.PB6→U11.PB7, U11.PB6→U9.PB7, optionally PA8↔PA8 | bridge pins confirmed; if different from PB6/PB7, update §5.1 and both `.ioc`s |
| P0.2 | ⚠️ U3 sensor bridges → U9 | visual/continuity on BNO_SDA↔BNO_SDA_MCU path | matches firmware expectation |
| P0.3 | ⚠️ U4 U11 SWD flashable | ST-LINK probe on T14–T18, read device ID | U11 programmable |
| P0.4 | ⚠️ U5 RAM headroom at 8 links | build U9 with `CFG_BLE_NUM_LINK=8`, inspect `.map` (SYSRAM + SRAM2 sections) | ≥ 15 % free SRAM2 after stack reservation, SYSRAM headroom unchanged |
| P0.5 | Freeze spec §5 + test plan §7 | review vs any new finding | spec v0 committed to repo (`docs/bridge-spec-v0.md`) |

### Phase 1 — UART link bring-up (master board only, no nodes, ~1–2 days)

| # | Test | Procedure | Pass criteria |
|---|---|---|---|
| P1.1 | BER soak | U11 firmware skeleton: TX ring streaming 244 B PRBS32-filled blepipe frames at ≥ 40 KB/s; U9 counts CRC fails + resyncs. 30 min. Repeat ladder: 921600 → 1.5M → 2M | 0 CRC fails at 921600 over 30 min; record max stable baud |
| P1.2 | RTT | U9 sends COMMAND-ping 100×/s for 10 s; U11 acks each; host measures | p99 RTT < 5 ms; zero lost pings |
| P1.3 | Corruption recovery | inject byte flips at 1 Hz during P1.2 traffic | parser resyncs < 100 ms; no deadlock; counters explain every event |
| P1.4 | Synthetic load | U11 generates 6 fake ICM streams (20 B @ 25 Hz) → UART → U9 → app BLE; browser app receives as node IDs 7–12 | 10 min zero drops; app-side sequence gaps = 0; U9 CPU/RAM unchanged beyond expected |

**Gate G1:** the UART bridge is proven at required throughput + recovery. This answers "can the two-STM approach work?" — before any node dependency.

### Phase 2 — U11 as a real 6-link hub (needs 6 node PCBs, ~2–4 days)

| # | Test | Procedure | Pass criteria |
|---|---|---|---|
| P2.1 | 6-link streaming | flash trimmed U11 build; connect 6 nodes (bench, suit not required); stream → UART → U9 → app | 6 × 25 Hz sustained 10 min; per-node drops = 0 (check `leaf_live_diag`) |
| P2.2 | Dual-radio contention soak | U11 with 6 links + U9 idle-radio (app connected but quiet), 1 h; then repeat with U9 also streaming upper 6 if nodes available | retransmit ratio (`leaf_link_retries`) < 5 %; zero conn drops on both radios; document AFH behavior |
| P2.3 | Control plane | U9 → bridge → U11 → BLE → node: ODR set, targeted reconnect, LED | round trip < 50 ms p99; node acks relayed intact |

**Gate G2:** lower-body hub proven with real nodes on the shared PCB.

### Phase 3 — Integrated 12-node system (needs all 12 nodes, ~3–5 days)

| # | Test | Procedure | Pass criteria |
|---|---|---|---|
| P3.1 | Full-suit live | 12 nodes streaming @ 25 Hz with BNO + chest ICM → app | 30 min: end-to-end drop rate < 0.1 %; per-node seq gaps logged and attributed (BLE vs UART vs app link) |
| P3.2 | Latency histogram | host tooling timestamps (node sample counter → browser arrival) | p95 < 150 ms; p99 < 250 ms |
| P3.3 | Worst-case contention | P3.1 + one SD recording session running on U9 simultaneously | no watchdog resets; drops within P3.1 thresholds |
| P3.4 | Calibration dry run | run the 3-phase whole-suit calibration (N-pose static → dynamic functional set → validation) from a bench rig with all 13 sensors mounted on a fixture | calibration converges; cross-hub timestamp skew does not corrupt pose estimates (decides whether the sync-pulse bodge of §6.4 is needed) |

**Gate G3:** system-level proof including the calibration path (the core research question from the stakeholder feedback).

### Phase 4 — Soak & PCB order go/no-go

- 24 h scripted soak: 12 nodes live at the required 25 Hz BNO+ICM profile, scripted 10 min recording sessions every 2 h, app connected throughout.
- **Pass → order the next PCB batch.** Abort/rework criteria: any watchdog reset, drop-rate drift, RAM leak (SYSRAM watermark trend), UART resync-count growth.
### Stretch 50 Hz experiment

Begin only after every required 25 Hz gate passes. Recalculate BLE airtime,
phone notification capacity, UART load, queue sizes, and timestamp-age
targets as a separate reviewed experiment. Do not silently change the
production profile or its 40–80 ms live-interval clamp.

---

## 8. Risk register

| ID | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | **Dual-radio self-interference** — two WB55s on one PCB, no coexistence interface on this chip; AFH resolves most collisions but expect elevated retries under load | Medium | Medium | Both antennas + filters already on the board (good isolation); 2M PHY shortens events; measure first (P2.2) before tuning; stagger conn anchors |
| R2 | **SYSRAM/SRAM2 exhaustion on U9 at 8 links** | Medium | High | P0.4 map check before any other work; trim GATT services/pools if tight |
| R3 | **Bridge wiring ≠ PB6/PB7 assumption** | Low-Med | High | P0.1 continuity check first; `bridge_uart_transport` is pin-agnostic (pins live in `.ioc` only) |
| R4 | **Cross-hub timestamp skew** corrupts calibration/fusion | Low | Medium | Counter-preserving re-stamp policy; P3.4 decides; sync-pulse bodge as upgrade |
| R5 | **U11 build bloat** (RAM/flash) | Low | Medium | scope freeze §6.1; node image is the size reference |
| R6 | **Scope creep** (recording-over-bridge, fusion on U11) | High | Medium | v0 = live+control+stats only (§5.3); v1 items parked in §6.4 |
| R7 | **Single master board, no spare** — bricking U11 via bad flash | Low | High | bench power + ESD discipline; U11 SWD has BOOT0/NRST header (T14–T18) for recovery; never flash U9's image onto U11 |
| R8 | **App/host tools assume ≤ N nodes** | Medium | Low | verify Web Bluetooth tooling tolerates IDs 7–12 early (P1.4 uses 7–12 deliberately) |

---

## 9. Verified repo reference (paths & symbols the implementer will need)

| What | Where |
|---|---|
| blepipe frame/CRC/lane/msg-type definitions, `blepipe_encode/decode` | `firmware/common/inc/exo/protocol/blepipe_proto.h` |
| BLE V2 live envelope (app-facing), `ble_v2_pack()` | `firmware/common/inc/exo/protocol/ble_stream_v2.h` |
| Sample structs (`Bno85SampleV3`, `Icm45686SampleV4`), ESOX session header | `firmware/common/inc/exo/types/recording_types.h` |
| 6-link central client API + per-leaf telemetry + `send_blepipe_to_node` | `firmware/common/inc/exo/ble/exo_hub_central_client.h` |
| Conn-interval/PHY/DLE tuning ladder | `firmware/common/inc/exo/ble/link_tune_state.h` |
| Notification watchdog | `firmware/common/inc/exo/ble/notification_gate.h` |
| U9 superloop (injection point lives here) | `firmware/master/Core/Src/main.cpp` (~5.4k lines; loop near the bottom) |
| U11 template (leaner loop, no storage) | `firmware/node/Core/Src/main.cpp` |
| BLE stack config (NUM_LINK, MTU, DLE, TX power) | `firmware/master/Master.ioc` lines ~438–451 |
| UART pin config (USART1 PB6/PB7/PA8, LPUART1 PB5/PA3) | `firmware/master/Master.ioc` lines ~256–299; same pins in `Node.ioc` |
| Master schematics (MCUs, RF, bridges, power) | `docs/ExoSkeleton Master PCB V1.1.pdf` |
| Record transfer chunking (for v1 bridge recording) | `firmware/common/inc/exo/protocol/ble_record_protocol.h`, `record_transfer_tuning.h` |

---

## 10. UNVERIFIED items ledger (each blocks the phase that uses it)

| ID | Item | Verify by | Blocks |
|---|---|---|---|
| U1 | U9↔U11 UART is PB6/PB7 (cross-wired) | P0.1 continuity | Phase 1 |
| U2 | PA8 (USART1 CK) bridged between MCUs | P0.1 continuity | only sync-mode fallback |
| U3 | Sensor 0R bridges route BNO/ICM to U9 on assembled boards | P0.2 visual/continuity | trust in existing sensor path |
| U4 | U11 SWD header T14–T18 accessible & flashable | P0.3 ST-LINK probe | Phase 1 |
| U5 | RAM headroom on U9 at `CFG_BLE_NUM_LINK=8` | P0.4 `.map` inspection | Phase 1 |
| U6 | Max reliable UART baud on the real trace | P1.1 BER ladder | 2 Mbaud stretch goal |
| U7 | U11 crystals + LSE functional (schematic has Y3/Y4) | P0.3/P1.1 heartbeat stability | Phase 1 |
| U8 | Exact live-stream injection point + envelope batching in U9 `main.cpp` | code read before touching `main.cpp` | §6.2 step 2 |
| U9 | Host tools tolerate node IDs 7–12 | P1.4 deliberate use | Phase 1/3 |

---

## Appendix A — Research context that produced this design (summary of prior work)

Prior research (repo docs review, 20 targeted web searches, stakeholder feedback analysis) established, and this handbook applies:

1. **Bandwidth is not the bottleneck** for 12 IMU nodes; link count, airtime scheduling, and sync are. (STM32WB55 ≈ 733 kbps @ 1M PHY measured-class figures; edge feature extraction can cut transmissions ~98 % if ever needed.)
2. **BLE mesh must never carry sensor streams** — connection-oriented links only (current architecture already complies; the two-STM split preserves it).
3. **Whole-suit calibration** follows a 3-phase pattern (N-pose static → dynamic functional set → validation, ~90 s + ~15 s quick recal); the bridge design keeps all 13 sensor streams available to the app so calibration is architecture-transparent (P3.4 validates).
4. **Haptic AAN loop stays node-local** end-to-end; nothing latency-critical crosses the bridge.
5. **Coaches configure, never reflash** — body-map is app-side config; commissioning = pairing + ID assignment, extended to IDs 7–12 by the same mechanism.
6. Earlier deliverables (research brief DOCX, R&D context MD) live in the project `download/` folder; this handbook supersedes the topology-options discussion by selecting and specifying the "dual-hub bridge" option.
