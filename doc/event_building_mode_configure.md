# EventBuildingMode Configure — Phase Map

## Overview

The `configureEventBuildingMode()` sequence for CFO and DTC front-ends uses a multi-phase,
multi-sub-step protocol to bring up the timing chain in a controlled order. Each phase is one
plain iteration inside a subsystem's configure pass (see "Iteration model" below);
sub-iterations handle steps within a phase that must each complete in <5s.

Two operating modes use this sequence:

- **EventBuildingMode** — runs SERDES edge fix phases (phases 4–6, 9) but skips RTF
  40 MHz offset calibration (phases 7–8 are idle). The CFO starts a fixed-width event
  run plan at Phase 2a for marker traffic during edge fix phases, and stops it at Phase 3e.
- **EventBuildingAndSyncMode** — assumes the RTF 40 MHz sync clock is present and runs the
  full sequence: edge fix (phases 4–6) plus RTF Marker Offset calibration (phases
  7–8) and error verification. The CFO also starts a fixed-width event run plan at Phase 2a
  and stops events at Phase 3e before ROC setup.

Both modes use the same phase count (14 phases, 0–13).

## Iteration model

otsdaq has three tiers of transition steps:

| Tier | Synchronized across | Cost per step |
|---|---|---|
| **Subsystem-iteration** | all subsystems, by the top-level Gateway | ~2 s (UDP round trip) |
| **Iteration** | the applications inside one subsystem, by that subsystem's Gateway | milliseconds |
| **Sub-iteration** | one application only | milliseconds |

Front-end code asks for the next step with `indicateSubsystemIterationWork()`,
`indicateIterationWork()` or `indicateSubIterationWork()`.

The 14 configure **phases are plain iterations** — they run back-to-back inside one
subsystem's pass with no top-level round trip. The phase number is the plain-iteration offset
from the first iteration of the FE's pass (`CFOandDTCCoreVInterface::configPhase()`), because
the iteration index never resets during a transition: a DTC that waited one subsystem-iteration
for the CFO subsystem starts its own pass at iteration 1, not 0.

**Standalone mode.** A Gateway with no top-level above it and no remote subsystems below it
sends `subsystemIterationIndex = VStateMachine::SUBSYSTEM_ITERATION_STANDALONE` (`-1`).
Every "whose turn" check is written as `standalone || index == myTurn`
(`isMyConfigureSubsystemIteration()`), so a standalone subsystem never idles a pass.

## Subsystem-iteration passes

Only the pass boundary crosses subsystems. Each subsystem runs its whole phase list in one pass:

| Subsystem-iteration | CFO subsystem (CFO + any DTCs in it) | Detector subsystems (DTCs, no CFO) | artdaq |
|---|---|---|---|
| **0** (or standalone) | Phases 0–13 as plain iterations. At Phase 2a the CFO also enables the CFO link on every DTC in its subsystem via FE macro. | idle — `indicateSubsystemIterationWork()` (skipped when standalone) | `configureInit`, launch configuring thread, poll with plain iterations, done. No DTC hardware access. |
| **1** (or standalone) | done | Phases 0–12 as plain iterations (Phase 13 is CFO-only). CDR lock is self-checked; edge fix / RTF offset run locally. | done |

**Whose turn (DTC).** A DTC configures in subsystem-iteration 0 if a `CFOFrontEndInterface`
exists in its subsystem's configuration tree (`subsystemHasCFO()`), otherwise in 1. No config
field is needed. DTCs in the CFO's subsystem therefore iterate with the CFO exactly as before.

**Reach of the CFO.** FE macros are routed by the local MacroMakerSupervisor and can only reach
front ends in the same subsystem. So the CFO-driven steps (enable DTC CFO links at 2a, CDR check
at 2b, edge fix at 2c) cover the DTCs in the CFO's subsystem only. Detector-subsystem DTCs rely
on their own Phase 2a `EnableLink`, the Phase 2b self-check and the Phase 3 local edge fix.

**Accepted consequences for detector-subsystem DTCs.** By the time they run (subsystem-iteration
1) the CFO has already stopped its marker/event run plan (Phase 3e) and enabled RF0 + punch
(Phase 6). Their edge fix therefore sees clock markers only, and RF0/punch are on during their
ROC/DCS setup.

**artdaq.** The board readers run with `skip_dtc_init = true`, so they do not touch DTC
hardware at configure (no ROC-link writes, no `ReleaseAllBuffers`). The DTC and CFO front ends
release the DAQ DMA buffers themselves at the Final SoftReset (Phase 12); the board readers
release again at `start()`. `ARTDAQSupervisor` therefore configures without any phase gate.

Key design constraints:

- **Dual DTC instances**: The same physical DTC can be controlled by two FE instances — one in
  the CFO subsystem (no real ROCs / emulated-only) and one in the detector subsystem (with real
  ROCs). Iteration ordering avoids duplicating work (e.g. JA lock at phase 0 is visible to
  the instance at phase 1).
- **DCS ownership**: Only one DTC instance can control DCS (ROC communication via DMA). This is
  the instance with real ROCs. The CFO-subsystem DTC must never call `EnableDCSReception()`.
- **No-ROC DTCs** skip ROC/EVB setup entirely — they only participate in clock/sync phases
  (phases 0–9) and Final SoftReset (phase 12).
- **Sub-step budget**: Each sub-step must complete in <5s to avoid SOAP/xoap timeouts.

## Phase Map

The CFO column runs in the CFO subsystem's pass; each DTC column runs in whichever pass that
DTC belongs to (see "Subsystem-iteration passes"). "Phase" is the plain-iteration offset
within the pass.

| Phase | Name | CFO | DTC (no real ROCs) | DTC (real ROCs) |
|---|---|---|---|---|
| 0 | **1a — Establish Clocks** | `halt()`, `DisableBeamOnMode(ALL)`, `DisableBeamOffMode(ALL)`, `ClearControlRegister()`, `DisableAllOutputs()`, JA setup + lock poll | `ClearControlRegister(keepMask)`, `DisableCFOLoopback()`, JA setup + lock poll (keepMask preserves bits 5-7: edge settings) | idle |
| 1 | **1b — Establish Clocks** | idle | idle | `ClearControlRegister(keepMask)`, `DisableLink(EVB)`, disable ROC links, `DisableCFOLoopback()`, JA setup + lock poll (keepMask preserves bits 5-7: edge settings) |
| 2 | **2a — Timing Chain: Enable** | `EnableLink(CFO_Link_ALL)`, `EnableEmbeddedClockMarker()`; in both EB modes starts fixed-width event run plan (1.7µs, mode 0, infinite markers) for aggressive 8b10 traffic during edge fix | `EnableLink(DTC_Link_CFO)`; in Sync mode also enables CFO-RTF Offset Control (bit 6) | `EnableLink(DTC_Link_CFO)`; in Sync mode also enables CFO-RTF Offset Control (bit 6) |
| 3 | **2b — Timing Chain: CDR Check** | Enumerate DTCs, call "Get Link Lock Status" FE Macro on each; if unlocked -> `ResetSERDES()` + retry; throw on 2nd failure | `ReadSERDESRXCDRLock(DTC_Link_CFO)` — throw if not locked | `ReadSERDESRXCDRLock(DTC_Link_CFO)` — throw if not locked |
| 4 | **2c — CFO Edge Fix** | RTF validation on all DTCs, then iterative edge fix loop (call "Fix CFO Clock Edge" FE Macro on each DTC per pass until 2 consecutive clean passes or max 10 passes; throw if not converged). Active in both EB modes. | idle | idle |
| 5 | **3a — Edge fix (no-ROC)** | idle | Clear JA counters + SoftReset, wait markers > 1000, check edge errors (txMarkers/rxToTx/parity/batchSlip) — toggle edge + SoftReset if needed, retry once; then verify JA counters stayed 0. Active in both EB modes. | idle |
| 6 | **3b — Edge fix (ROC DTCs)** | idle | idle | Same edge fix sub-steps as 3a. Active in both EB modes. |
| 7 | **3c — Sync: RTF offset + verify (no-ROC)** | idle | Clear JA counters, apply RTF offset + `SoftReset()`, wait markers > 1000, verify error flags + RTF counters; if RTFPhase-only, toggle RTF punched clock edge (bit 7) + `SoftReset()` and re-verify once; then verify JA counters stayed 0. **Sync mode only.** | idle |
| 8 | **3d — Sync: RTF offset + verify (ROC)** | idle | idle | Same offset + verify sub-steps as 3c. **Sync mode only.** |
| 9 | **3e — Stop CFO Events** | `DisableBeamOnMode(ALL)`, `DisableBeamOffMode(ALL)`. Active in both EB modes. | idle | idle |
| 10 | **4 — ROC and DCS Setup** | idle | idle | `SetupROCs()` per link, `EnableDCSReception()`, CRV `SetPunchEnable()`, `SoftReset()`, ROC DCS-based configure |
| 11 | **5 — ROC Data Path Setup** | idle | idle | `DisableLink(EVB)`, `SetEVBInfo()`, DRP mode, `EnableLink(EVB)`, `SetCFOEventModeRequiredMask()` |
| 12 | **Final SoftReset** | `SoftReset()` | `EnableLink(CFO)`, `SoftReset()` | `EnableLink(CFO)`, `SoftReset()` |
| 13 | **6 — Enable CFO Operation** | `EnableAcceleratorRF0()`, `SetPunchEnable()` | idle | idle |

In **EventBuildingMode**, phases 4–6 and 9 are active (SERDES edge fix + run plan cleanup).
Phases 7–8 (RTF offset calibration) are idle because the permanent marker offset is
meaningless without a 40 MHz reference clock to align to.

Constants in `CFOandDTCCoreVInterface.h`:

- `CONFIG_PHASE_ESTABLISH_CLOCKS_A` = 0
- `CONFIG_PHASE_ESTABLISH_CLOCKS_B` = 1
- `CONFIG_PHASE_ESTABLISH_TIMING_CHAIN_ENABLE` = 2
- `CONFIG_PHASE_ESTABLISH_TIMING_CHAIN_CHECK` = 3
- `CONFIG_PHASE_CFO_EDGE_FIX` = 4
- `CONFIG_PHASE_ESTABLISH_SYNC_A` = 5
- `CONFIG_PHASE_ESTABLISH_SYNC_B` = 6
- `CONFIG_PHASE_ESTABLISH_SYNC_C` = 7
- `CONFIG_PHASE_ESTABLISH_SYNC_D` = 8
- `CONFIG_PHASE_ESTABLISH_SYNC_E` = 9
- `CONFIG_PHASE_ESTABLISH_ROC_CONFIG` = 10
- `CONFIG_PHASE_ROC_DATA_PATH` = 11
- `CONFIG_PHASE_FINAL_SOFT_RESET` = 12
- `CONFIG_CFO_EVENT_SENDING_START_ITERATION` = 13

Operating mode constants in `CFOandDTCCoreVInterface.h`:

- `CONFIG_MODE_EVENT_BUILDING` = `"EventBuildingMode"`
- `CONFIG_MODE_EVENT_BUILDING_AND_SYNC` = `"EventBuildingAndSyncMode"`

## How a DTC knows its type

A DTC is classified as "with real ROCs" (`has_real_roc_flow_ = true`) based on the
`EnableROCConfigureStep` configuration parameter and ROC mask checks:

0. **Override**: If `EnableROCConfigureStep` is explicitly `false`, the DTC is forced to the
   no-ROC path regardless of ROC masks or link tables. No further rules are checked.
1. At least one enabled, non-emulated ROC: `(roc_mask_ & ~roc_emulated_mask_) != 0`
2. Any enabled ROC has `ROCTypeLinkTable` connected (not `NO_LINK`)
3. Any enabled ROC has `LinkToSlowControlsChannelTable` connected (not `NO_LINK`)
4. If none of rules 1–3 triggered but `EnableROCConfigureStep` is `true` (the default),
   the DTC is classified as real-ROC to exercise the full configure flow.

Rules 1–4 allow exercising the "real ROC" configuration flow (Phases 1b, 3b, 3d, 4, 5) with
emulated-only ROCs — useful for testing subsystem configure sequences without physical hardware.
Rule 0 allows forcing a DTC with physical ROCs to skip ROC-specific phases (e.g. during
commissioning when the ROC side is not yet ready).

The classification reason is stored in `real_roc_flow_reason_` and included in error messages
so operators can see why a DTC took a particular phase path.

## Phase 1a/1b — Establish Clocks

### CFO sub-steps (phase 0)

0. `next_starting_event_window_tag_ = 0`, `halt()`, `DisableBeamOnMode(ALL)`,
   `DisableBeamOffMode(ALL)`, `ClearControlRegister()`, `DisableAllOutputs()`
1. JA setup: if JA unlocked -> full reset; if locked -> mux-only select
2+. JA lock polling (1s intervals, ~10 attempts)

### DTC sub-steps (phase 0 for no-ROC, phase 1 for ROC DTCs)

0. `ClearControlRegister(keepMask)`, `DisableCFOLoopback()`.
   `keepMask` preserves control register bits 5 (CFO-RTF Edge Select), 6 (CFO-RTF Offset
   Control), and 7 (RTF Punched Clock Edge Select) — run-time error counters and previously
   calibrated edge settings are carried into Phase 2c for the first edge fix decision.
   ROC DTCs also `DisableLink(EVB)` and disable configured ROC links. No-ROC DTCs skip these.
1. JA setup (same lock-aware pattern as CFO)
2+. JA lock polling

## Phase 2a — Timing Chain: Enable (phase 2)

### CFO

`EnableLink(CFO_Link_ALL)`, `EnableEmbeddedClockMarker()`.

In both event-building modes, the CFO also starts a fixed-width event window run plan
(`CompileSetAndLaunchTemplateFixedWidthRunPlan`: 1.7µs duration, infinite markers, mode 0,
clock markers enabled). This introduces heartbeats and event window markers — a richer 8b10
character mix on the CFO→DTC timing links — that is more likely to expose edge-related bit
errors than clock markers alone. Events remain active through Phases 2c–3d and are stopped
at Phase 3e.

When a DTC shares the CFO's subsystem, the CFO also runs the `Enable/Disable DTC Link` FE
macro on it (link 6, Tx and Rx enabled), so the chain bring-up does not depend on the DTC
front end having reached Phase 2a itself. Any DTC that cannot be reached fails the CFO's
configure with the DTC named.

### DTC (all types)

`EnableLink(DTC_Link_CFO)` — enables the CFO receive link on the DTC so that CDR lock can
be established before Phase 2b checks it. Idempotent with the CFO-driven enable above; it is
what DTCs in subsystems without a CFO rely on.

In **EventBuildingAndSyncMode**, also sets control register bit 6 (Enable CFO-RTF Offset
Control) so the measured RTF position updates and is available for edge fix decisions at
Phase 2c and RTF offset calibration at Phases 3c/3d.

## Phase 2b — Timing Chain: CDR Check (phase 3)

### CFO — sub-steps

0. Enumerate all DTC FE interfaces via config tree, call "Get Link Lock Status" FE Macro on
   each. If any DTC unlocked -> `ResetSERDES()` (upstream + per-link), advance to retry.
1. Retry CDR lock check. If still unlocked -> throw exception.

### DTC

Self-check `ReadSERDESRXCDRLock(DTC_Link_CFO)`. If not locked, throws exception.

## Phase 2c — CFO Edge Fix (phase 4)

Active in both event-building modes.

### CFO — sub-steps

0. Enumerate all DTCs, call "Get RTF Interface Status" FE Macro on each. Validates: CFO
   Emulation Mode OFF, JA Source RJ45, Saturated YES, CFO CDR Lock LOCKED. Throws if any check
   fails.

1+. Iterative edge fix passes. On each pass, the CFO calls "Fix CFO Clock Edge" FE Macro on
   every DTC. That macro waits for CFO TX Clock Markers > 1000 (100ms polling, throws if no
   count increase or if count stays ≤ 1000 after ~3s), then checks edge errors (txMarkers,
   rxToTx, parity, batchSlip). If errors are present, it toggles the CFO sample edge, settles
   100ms, and issues a `SoftReset()`.

   - If **no DTC toggled** on two consecutive passes → chain converged. Done.
   - If **some toggled** and pass count < 10 → another pass.
   - If pass count ≥ 10 → throw exception listing which DTCs still have errors.

   Changing an upstream DTC's edge can affect downstream DTCs, so multiple passes may be needed
   for convergence. Early DTCs in the chain do not lose their correct edge once found; the loop
   is monotonically converging.

### DTC (all types): idle

## Phase 3a — Edge fix (no-ROC DTCs) (phase 5)

Active in both event-building modes.

Per-DTC fallback edge fix after the CFO-coordinated loop. Each no-ROC DTC independently checks
and fixes its own edge.

### CFO: idle

### DTC w/o ROCs — sub-steps

0. Clear JA counters (CDR Unlock, JA Unlock, JA Recovered Clock LOS, JA External Clock
   LOS — SoftReset does not clear these), then `SoftReset()`.
   Wait for CFO Rx Clock Markers > 1000 (3s timeout, throw if not reached).
1. Check edge errors (txMarkers, rxToTx, parity, batchSlip).
   If any non-zero -> `ToggleExternalCFOSampleEdge()`, 100ms settle, `SoftReset()`,
   continue to sub-step 2.
   If all zero -> verify JA counters stayed 0 (throw if not), done.
2. `SoftReset()` to clear transient counters from edge flip, wait for markers > 1000 again
   (3s timeout).
3. Re-check edge errors. If still present -> throw. Otherwise verify JA counters stayed 0.

### DTC w/ ROCs: idle

## Phase 3b — Edge fix (ROC DTCs) (phase 6)

Active in both event-building modes.

### DTC w/ ROCs

Same sub-step sequence as Phase 3a for no-ROC DTCs.

### CFO, DTC w/o ROCs: idle

## Phase 3c — Sync: RTF offset + verify (no-ROC DTCs) (phase 7)

Only active in **EventBuildingAndSyncMode**. The permanent marker offset aligns the fiber
marker to the 40 MHz clock boundary — without the 40 MHz reference there is nothing to
align to, so this phase is skipped in plain EventBuildingMode.

### DTC w/o ROCs — sub-steps

0. Clear JA counters (CDR Unlock, JA Unlock, JA Recovered Clock LOS, JA External Clock LOS).
   Read CFO Marker Pos from `ReadCFOMeasuredMarkerPosition()` (register 0x9398 bits [18:16]),
   verify saturated and markerPos <= 4. Compute Perm Offset via `ReadCFOImpliedMarkerOffset()`
   (`2 - markerPos`), call `SetCFOSamplePermanentOffset()`, `SoftReset()`.
1+. Wait for markers > 1000 (1s polls, up to ~7s timeout).
Verify. Read error flags (RTF 40MHz Phase Shift, Illegal Marker Timing, Event Start Marker
Tx, Clock Marker Tx, Rx-to-Tx Data Corruption) and RTF counters (Event Start Character Error,
40MHz Character Error, CDC Diagnostic parity + batch slip).
  - If **RTFPhase is the only error** (all other flags clear, all RTF counters zero): toggle
    RTF punched clock edge via `ToggleRTFPunchedClockEdge()` (Control Register bit 7),
    100ms settle, `SoftReset()`, then re-wait for markers > 1000 and re-verify.
    This retry happens at most once.
  - If any other error flag or RTF counter is non-zero (or RTFPhase persists after retry):
    throw with `getCFORTFSettingsStatusAndErrors()` output.
  - Otherwise: verify JA counters (CDR, JA, JA-Rec, JA-Ext) stayed 0 since the clear at
    sub-step 0 (throw if not). Pass.

### CFO, DTC w/ ROCs: idle

## Phase 3d — Sync: RTF offset + verify (ROC DTCs) (phase 8)

Only active in **EventBuildingAndSyncMode**.

### DTC w/ ROCs

Same sub-step sequence as Phase 3c.

### CFO, DTC w/o ROCs: idle

## Phase 3e — Stop CFO Events (phase 9)

Active in both event-building modes (the run plan started at Phase 2a must be stopped).

### CFO

`DisableBeamOnMode(CFO_Link_ALL)`, `DisableBeamOffMode(CFO_Link_ALL)`. Stops the event window
run plan started at Phase 2a. This ensures no heartbeats or event markers are in flight before
ROC/DCS and data path setup begins.

### DTC (all types): idle

## Phase 4 — ROC and DCS Setup (phase 10)

Only DTCs with real ROCs act. CFO and no-ROC DTCs idle.

Sub-step 0:
1. `SetupROCs()` per link — enables/disables links, configures emulation per `roc_mask_` and
   `roc_emulated_mask_`. CRV DTCs force `clockMakersEnabled = false`.
2. `EnableDCSReception()`
3. CRV: `SetPunchEnable()`
4. `SoftReset()` to clear lock counters
5. If `EnableROCConfigureStep` config is true: begin ROC DCS-based configure (continue to
   sub-step 1+)

Sub-steps 1+: ROC DCS-based configure. The DTC acts as FESupervisor for its ROCs — sets each
ROC's sub-iteration index, calls `roc->configure()`, checks `getSubIterationWork()`. First
sub-step also calls `WaitForLinkReady()` per ROC. Repeats until all ROCs are done.

## Phase 5 — ROC Data Path Setup (phase 11)

DTCs with real ROCs perform the full data path setup. No-ROC DTCs with connected ROCs
(`roc_mask_ != 0`) still set the DRP mode based on `EnableSoftwareDataRequestMode`.
No-ROC DTCs with no connected ROCs idle.

### DTCs with real ROCs

1. `DisableLink(EVB)`, read `EventBuilderDTCID`/`EventBuilderMode`/`EventBuilderPartitionID`/
   `EventBuilderMACIndex` from config, `SetEVBInfo()`
2. Software DRP mode: `EnableSoftwareDRP()` or `DisableSoftwareDRP()` based on config
3. `EnableLink(EVB)`
4. Read `EventModeRequiredMask` from config (default 0), `SetCFOEventModeRequiredMask(mask)`

### No-ROC DTCs with connected ROCs

Software DRP mode only: `EnableSoftwareDRP()` or `DisableSoftwareDRP()` based on config.
This ensures the Autogenerate DRP bit (0x9100 bit 23) is set even when `EnableROCConfigureStep`
is false.

## Final SoftReset (phase 12)

All DTCs: `EnableLink(CFO)` to ensure the CFO receive path is active before the CFO begins
sending heartbeats and event window markers, `ClearDetectorEmulatorInUse()`,
`ReleaseAllBuffers(DTC_DMA_Engine_DAQ)`, then `SoftReset()` to clear accumulated errors from
ROC/DCS and data path setup. CFO: `ReleaseAllBuffers(DTC_DMA_Engine_DAQ)` then `SoftReset()`.

This is the last phase for a DTC in a subsystem without a CFO. A DTC in the CFO's subsystem
idles one more iteration so it finishes together with the CFO's Phase 6.

## Phase 6 — Enable CFO Operation (phase 13)

CFO only. All DTCs idle.

- `EnableAcceleratorRF0()`
- `SetPunchEnable()`

Links and clock markers are already enabled from Phase 2a. The configure-time DAQ buffer
release happens at Phase 12 in the front ends; the board readers release again at `start()`.

## Start transition

Every start step is a real cross-subsystem ordering point, so the steps are counted with
`getSubsystemSyncStepIndex()` / `indicateSubsystemSyncStepWork()`: subsystem-iterations under a
top-level, plain iterations when standalone (the standalone index never increments, and plain
iterations are already synchronized inside the one subsystem). `minReady` is
`MinReadyForEventGenerationStartIteration` (aggregated max over all FEs and ROCs, ≥ 2).

| Step | CFO | DTCs | artdaq (`ARTDAQSupervisor`) |
|---|---|---|---|
| 0 | reset event-window tag; idle | `SoftReset()` + start all ROCs | idle |
| 1 | idle | idle | `do_start_running`; sub-iterates until artdaq is running |
| 2 … minReady−2 | idle | idle | idle |
| minReady−1 | idle | final `SoftReset()` + 500 ms settle | idle |
| minReady | launch fixed-width run plan (if `AutoFixedWidthRunPlanEnable`) | done | idle |
