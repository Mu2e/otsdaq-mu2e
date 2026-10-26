# EVB link 7: DTC_1 "CDR Not Locked" on build 26_09_24_13

Date: 2026-09-24. Not compiled here (Ryan builds from DTC_compile/).

## Symptom
EVB Init on calo-12: DTC_0 link 7 `Tx OK  Rx OK  CDR LOCKED`; DTC_1 link 7 `Tx OK  Rx OK  CDR Not Locked
(link reset attempted 3/3x)` and it never locks on this bitfile. Roughly 2 of the last 10 builds do this.

## Changes made (dev copy, DTC/)
### 2. Software link reset now reaches link 7 (EVB3.v, `reset_serdes` block near line 485)
`reset_serdes_feeder <= user_reset || ~axi_str_s2c0_areset_n || ~axi_str_c2s0_areset_n || SERDESReset;`
One term added. `SERDESReset` is register 0x9118 bit 7 (`SERDESReset[7]` in DTC.v line 1159); it entered
EVB3 and went nowhere before. Same user_clk domain as the register block, the 32-deep shift stretches it,
and it is outside the `EVBSimMode` ifdefs (checked). The sim wrapper EVB_DTCSim.sv ties the port to 0,
unchanged (the GT is not in the sim).
- It is a LEVEL: software must write bit 7 back to 0, or the link stays in reset forever. Check what
  the EVB Init macro does after "link reset attempted" (if it only writes 1, the link will never come back).
- What restarts: wizard TX and RX start-up FSMs, the QPLL (GTXE2_COMMON_X0Y3 is used only by the EVB link;
  the ROC links in the quad run on their CPLLs, SYSCLKSEL = 00), both MMCMs, block sync, scrambler,
  RXLockedCounter. Reset-done bits 0x9138 [31,23,15,7] drop and come back; 0x9128 bit 7 drops and comes back.
- TX clock stops 50-400 us: never during a run (CFO markers at the marker CDC are lost, txgbeclk ILAs freeze);
  the peer DTC loses lock until 255 idles later. Between runs only.
- Not done: the per-direction split (`SERDESTXRXReset[7]` RX-only / `[15]` TX-only, which would let the
  peer keep lock). Needs the single `resetint` port of EVB10GBELink split into TX and RX resets.

### 1. Link-7 ILAs on
`compile/DTC.v`: `enable_link_ila0` 8'b01_000_000 -> **8'b11_000_000**. Bit 7 turns on the two ILAs
inside EVB10GBELink (`ila_18` on the TX clock, `ila_19` on the RX clock, 1024 deep, cross-triggered).
Both IP cores already exist and are enabled in the DTC_compile project (last built with them 2025-12-10).
Cost: about 14 more block RAM tiles (318 of 445 used now), no new ports.
Caveat: adding ILAs moves placement, which by itself can change a build that only fails 2 times in 10.

## What "CDR LOCKED" really is
0x9140 bit 7 = `RXLinkLocked` (EVB10GBELink.v). It is not the transceiver's CDR-lock pin, which is left
unconnected. RXLinkLocked = block lock from the 64b/66b sync machine (64 good headers in a row) AND 255 idle
words in a row from the descrambler; it drops after 0x7d4 non-idle words. "Not Locked" therefore means either
no block lock or no idle words reaching the compare.

## The software link reset did nothing on link 7 (until build after 26_09_24_13)
- 0x9118 bit 7 (`SERDESReset[7]`) reached EVB3 as an input port and was never used inside EVB3.
- The wizard reset block (`evblink_init.v`, EXAMPLE_USE_CHIPSCOPE = 0) ignores the external gtrxreset pin,
  so the `gt0_gtrxreset_i` we build from FireflyPresent/loopback/resetint only resets our own block-sync
  and lock counters, never the transceiver.
- The transceiver only restarted on `reset_serdes` = user_reset or a DMA channel reset (EVB3.v, `reset_serdes`
  chain). SoftReset is deliberately excluded (2026-08-21).
So "link reset attempted 3/3x" on build 26_09_24_13 could not have fixed anything. Fixed above (change 2).

## Cheap checks on the broken DTC_1, no rebuild needed
Bit positions are from the read-back lines in register_map.v (the register map doc had FFCSR wrong).
| register | meaning | read 2026-09-24 16:06 (DTC_0 = DTC_1) |
|---|---|---|
| 0x9138 bit 31 / 23 | link-7 RX reset FSM done / GT rxresetdone. 0 = transceiver RX never came up. 1 = transceiver is fine and the block sync / idle detection is what fails | 0xffffffff: all 1 |
| 0x9138 bit 15 / 7 | same for TX (expect 1, matches "Tx OK") | 1 |
| 0x9128 bit 7 | link-7 PLLLocked (QPLL + TX MMCM + RX MMCM) | 0x3ff: 1 |
| 0x93A0 bits 26:24 | FireflyPresent[2:0]; bit 25 = Firefly 1 (the EVB link's RX); low with loopback 0 holds the block-sync machine in reset | 0x03030000: present = 011, Firefly 1 present |
| 0x9108 bits 23:21 | link-7 GT loopback select, must be 000 (0x9130 is RXCDRLOCKCOMMACOUNT, wrong register, reads its reset value 0x400) | not read yet |

Result: the transceiver, its PLLs and the reset FSMs are all up on both DTCs and read identically. The
failure is in the 64b/66b layer: block sync or idle-word detection on DTC_1's RX, or the data DTC_0's TX
puts on the fiber. That is exactly what the two link ILAs show (RX ILA on DTC_1, TX ILA on DTC_0).

## What the build reports say (snapshot `ai_scripts/evb_timing_reports/h26_09_24_13/`)
- Timing met everywhere: design WNS 0.075 ns / WHS 0.003 ns; inside the link worst setup 0.641 ns,
  worst hold 0.092 ns, worst async-reset removal 0.508 ns.
- Vivado warnings on the link: GT has no LOC constraint (AVAL-324, it lands on X0Y15 from the pins);
  a 5-input LUT drives the block-sync machine's async clears (LUTAR-1) fed by `reset_serdes` and the loopback
  register bits (user_clk) plus the raw `FireflyPresentn[1]` pad, none synchronized into the RX clock.
  report_cdc user_clk -> link: 717 unknown 1-bit crossings, 73 unknown async-reset crossings, all untimed
  (asynchronous clock group). These are the same in a working build unless placement changes their delays,
  which is exactly what a snapshot diff will show.

## ILA recipe for the next build (signal names, EVB10GBELink.v)
RX ILA (`ila_19`, clock `gt0_rxusrclk2_i`): `gt0_block_lock_i`, `RXLinkLocked`, `gt0_rxgearboxslip_i`,
`gt0_rxheadervalid_i`, `gt0_rxheader_i`, `gt0_rxdatavalid_i`, `gt0_rxresetdone_i`, `gt0_rxfsmresetdone_i`,
`gt0_rxmmcm_lock_i`, `gt0_qplllock_out`, `IdleWordCounter`, `RXLockedCounter`, `rxdata`, `BlockSyncReset`,
`DescramblerReset`, `gt0_gtrxreset_i`.
1. First just arm with no trigger and look: is `gt0_block_lock_i` 1? Is `BlockSyncReset` stuck 1? Do
   `gt0_rxheader_i` values look like 01/10 with `gt0_rxheadervalid_i`?
2. If block lock toggles: trigger on `gt0_rxgearboxslip_i == 1` to see the slip loop.
3. If block lock is 1 and RXLinkLocked is 0: trigger on `IdleWordCounter` reload (`IdleWordCounter == 8'hff`
   while `gt0_block_lock_i == 1`) and read `rxdata`; that shows what non-idle word keeps breaking the run.
If the RX MMCM is not locked (0x9128 bit 7 = 0) the RX ILA has no clock and will not arm; use the TX ILA
(`ila_18`) and the registers above instead.

## Snapshot / compare flow
`ai_skills/vivado_timing_report.md` has the recipe. Per bitfile: copy the run's reports into
`ai_scripts/evb_timing_reports/h<yy>_<mm>_<dd>_<HH>/vivado/`, run `evb_timing.tcl` and `evb_link7_timing.tcl`
(they write into that folder by DCP time), save `src_md5_DTC_compile.txt`. When a build locks again:
`diff h26_09_24_13/link7/link7_summary.txt h<good>/link7/link7_summary.txt` first.
