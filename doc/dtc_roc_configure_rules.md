# DTC Configure — ROC Ownership, ROC Configure, and ROC Link Enable

Three independent decisions govern what a `DTCFrontEndInterface` does to its ROC links and
ROCs at Configure. They are easy to conflate; this page states each rule on its own, then
walks the case table. Code: `otsdaq-mu2e/FEInterfaces/DTCFrontEndInterfaceImpl.cc`.

Phase numbers refer to [event_building_mode_configure.md](event_building_mode_configure.md).

## Inputs

| Input | Where it comes from | Read when |
|---|---|---|
| `roc_mask_` | `LinkToROCGroupTable` children with `Status = true`, bit per `linkID` | `DTCInstantiate()` (constructor) |
| `roc_emulated_mask_` | same children that also have `EmulateInDTCHardware = true` | `DTCInstantiate()` |
| `enable_roc_configure_step_` | DTC FE row column `EnableROCConfigureStep` (missing column → `false`) | `DTCInstantiate()` |
| hardware link-enable (0x9114) | read from the card | Phase 4, no-real-ROC DTCs only |

## Rule 1 — `has_real_roc_flow_` (does this DTC own real ROCs?)

```
has_real_roc_flow_ = (roc_mask_ & ~roc_emulated_mask_) != 0
```

**True iff at least one ROC in the group is enabled and not emulated in DTC hardware.**
Nothing else is consulted: not `EnableROCConfigureStep`, not whether a ROC has a
`ROCTypeLinkTable` or `LinkToSlowControlsChannelTable`. An emulated ROC never makes the DTC
"real", whatever tables hang off it.

| Enabled ROCs in group | `has_real_roc_flow_` | `real_roc_flow_reason_` |
|---|---|---|
| none (no group linked, or all `Status = false`) | false | `all ROCs emulated or none` |
| all `EmulateInDTCHardware = true` | false | `all ROCs emulated or none` |
| at least one `EmulateInDTCHardware = false` | **true** | `non-emulated ROC present` |

Logged once at instantiation:
`Real ROC flow: YES (non-emulated ROC present); ROC configure step enabled`.

What the flag selects (see the phase map for details): clock-establish phase (1a vs 1b),
whether Phase 0 takes the ROC links down for JA rework, whether Phase 4 runs DCS reception /
CRV punch / SoftReset, Phase 5 data-path setup, and whether `roc->start()` runs at Start.

## Rule 2 — ROC `configure()` (DCS-based ROC configuration)

ROC `configure()` runs iff **both**:

- `has_real_roc_flow_ == true`, and
- `enable_roc_configure_step_ == true`.

`EnableROCConfigureStep` does **only** this. It does not change ownership (Rule 1) and does not
change link-enable writes (Rule 3). The same two conditions gate `roc->start()` at Start step 0.

| `has_real_roc_flow_` | `EnableROCConfigureStep` | ROC `configure()` / `start()` |
|---|---|---|
| false | any | no |
| true | false (or column absent) | no — log: `EnableROCConfigureStep is false; skipping ROC configure.` |
| true | true | **yes** (Phase 4 sub-steps 1+) |

## Rule 3 — ROC link enable written (Phase 4 sub-step 0)

A "write" means the per-link `SetupROCs()` loop runs over all six ROC links, setting
link Rx/Tx enable, clock-marker enable, and the ROC emulator bit from this DTC's own ROC
table. It is all-or-nothing per DTC.

| `has_real_roc_flow_` | Any ROC link already Rx/Tx-enabled in hardware | Link Enable Written |
|---|---|---|
| true | not checked | **yes** |
| false | no — all six off | **yes** |
| false | yes | **no** — log: `No real ROCs: some ROC links are on, leaving all ROC links as they are` |

A real-ROC DTC always asserts its table onto the card and therefore overrides whatever a
no-real-ROC instance (typically the CFO subsystem's copy of the same DTC) wrote earlier.
A no-real-ROC DTC only fills in links nobody owns yet, so the timing chain reaches the ROCs.

When the write happens, per link *i*:

| ROC *i* in table | Link Rx/Tx | Clock markers | ROC emulator | Emulated packets |
|---|---|---|---|---|
| not enabled / absent | disabled | off | disabled | 0 |
| enabled, `EmulateInDTCHardware = false` | enabled | off (also CRV) | disabled | 0 |
| enabled, `EmulateInDTCHardware = true` | enabled | on | **enabled** (Internal) | 16 |

### Other paths that touch ROC link enable

| Path | Writes link enable? |
|---|---|
| Phase 0 JA rework, `has_real_roc_flow_ = true` | yes — disables links in `roc_mask_`; Phase 4 re-enables |
| Phase 0 JA rework, `has_real_roc_flow_ = false` | no |
| Hardware-dev mode (`configureCommon()`) | yes — unconditionally, no ownership or links-on check |
| Halt / Stop / Pause / `DTCInstantiate()` / `SoftReset()` | no |
| FE macros `SetupROCs`, `ManualLoopbackSetup` | yes — operator-invoked |

## Combined cases

Two FE instances usually address the same physical DTC: one in the CFO subsystem (ROC table
`TrkROC0-5`, all `EmulateInDTCHardware = true`) and one in the detector subsystem (ROC table
`TrkNN_DTCn_ROCk`, all `EmulateInDTCHardware = false`). Each instance evaluates the three rules
from its own table.

| Instance | ROC table | `has_real_roc_flow_` | Link enable written | ROC `configure()` |
|---|---|---|---|---|
| CFO subsystem | all emulated | false | only if all six links are off | never |
| Detector, `EnableROCConfigureStep = false` | all real | **true** | **always** | no |
| Detector, `EnableROCConfigureStep = true` | all real | **true** | **always** | **yes** |
| Any, no ROC group linked | — | false | only if all six links are off (writes all links disabled) | never |

Order of subsystems does not matter for the final link state: whichever instance has real
ROCs writes last-and-authoritatively in its own pass, and the emulated-only instance never
overwrites links that are already up.

## History

Before 2026-10-28, Rule 1 was:
`EnableROCConfigureStep = false` → forced `has_real_roc_flow_ = false`; otherwise any of
{non-emulated ROC, `ROCTypeLinkTable`, `LinkToSlowControlsChannelTable`} → true; otherwise
`EnableROCConfigureStep = true` → true. With `EnableROCConfigureStep = false` on the tracker
DTC rows, the tracker instances were classified "no real ROCs", took the Rule 3 "links already
on → write nothing" branch, and left the ROC-emulation bits that the CFO subsystem had written
when it found all links off. Rule 1 now depends on the ROC table only, and
`EnableROCConfigureStep` gates only Rule 2.
