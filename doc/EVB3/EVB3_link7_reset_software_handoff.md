# EVB link 7 (10GbE) reset and lock status: what software must do on the next build

For: the DTC software agent (otsdaq FEMacros `EVB Init`, `DTC Read`, `DTC Write`).
From: firmware, 2026-09-24. Questions to Ryan Rivera.

## Why this exists
On bitfile 26_09_24_13 (0x9004 reads `0xd6092493`) `EVB Init` reported DTC_1 link 7 as
`CDR Not Locked (link reset attempted 3/3x)` and it never locked. Two firmware facts came out of it:

1. The "link reset" software has been writing (0x9118 bit 7) went nowhere in every build so far. It
   entered the EVB block and was never used. The three retries did nothing.
2. `CDR LOCKED` (0x9140 bit 7) is not the transceiver's clock-recovery lock. It is the EVB link's own
   check: 64b/66b block lock AND 255 idle words in a row from the other DTC. It drops after ~2000
   non-idle words in a row. Both DTCs on the bad build had a healthy transceiver (all reset-done and
   PLL bits set); the difference was only this bit.

The next build (any 0x9004 value later than `0xd6092493`, same `0xd60924xx` day or later) changes 1:
**0x9118 bit 7 now really resets the whole link 7.**

## What 0x9118 bit 7 does now
- Scope, as measured on the bench 2026-09-24 21:24 (0xd6092496): restarts the transceiver's TX and RX
  start-up sequencers (0x9138 bits 15 and 31 drop to 0 while held), and clears block sync and the lock
  counters. The GT's own reset-done bits (0x9138 bits 7 and 23) and the PLL bit (0x9128 bit 7) stay 1 to
  a register read: the sequencer re-pulses the GT resets for microseconds only, and the PLL is not
  re-reset once locked. TX stops for the sequence, so the peer loses lock, and everything is back within
  one poll after release. This is the intended scope (the wizard's soft reset); "whole transceiver" in
  earlier text over-stated it. There is no software knob that re-resets the PLL short of a hard reset.
- It is a **level, not a pulse**. It stays set until software writes it back to 0, or until a SoftReset
  (0x9100 bit 31), which zeroes the whole 0x9118 register (bench 2026-09-24 21:24; the earlier text
  saying SoftReset does not clear it was wrong). Keep the unconditional clear in EVB Init anyway. If
  software leaves it at 1 with no SoftReset, the link stays down: 0x9138 bits 15/31 read 0 and the
  peer's 0x9140 bit 7 is 0.
- It is a whole-link reset: while it is held and for a few ms after, this DTC sends nothing, so the
  **other DTC loses its lock (its 0x9140 bit 7 drops)** and regains it on its own once idles flow again.
- Never during a run. The TX clock stops and CFO markers are lost. Only with the run stopped.
- Bits 6:0 of 0x9118 are the ROC links 0-6 and are live. Write only bit 7 (read-modify-write, or write
  `0x00000080` then `0x00000000`; the other fields are also resets whose idle value is 0).

## Register cheat sheet (link 7 = the EVB 10GbE link)
Bit numbers are from the firmware read-back code, not older docs.

| register | bits | meaning | healthy |
|---|---|---|---|
| 0x9004 | [31:0] | firmware version word 2, build stamp `0xd6MMDDHH` with bit 7 = ROC-count flag. Bad build = `0xd6092493` | later than that |
| 0x9118 | bit 7 | link-7 reset (level; cleared by software write or SoftReset). bits 6:0 = ROC links, [15:8] PLL resets, [31:16] TX/RX resets for links 0-6 (do nothing for link 7) | 0 |
| 0x9138 | 7, 15, 23, 31 | link-7 TX reset done, TX start-up FSM done, RX reset done, RX start-up FSM done. Software prints `Tx OK` / `Rx OK` from these | all 1 |
| 0x9128 | bit 7 | link-7 PLL and both link clocks locked | 1 |
| 0x9140 | bit 7 | link-7 `RXLinkLocked` = the `CDR LOCKED` word. Needs the other DTC to be sending idles | 1 |
| 0x9108 | [23:21] | link-7 transceiver loopback select | 000 |
| 0x93A0 | [26:24] | Firefly present bits; bit 25 = Firefly 1 = the EVB link. If 0 the lock logic is held in reset | bit 25 = 1 (read `0x03030000` on calo-12) |
| 0x9114 | bit 7 / bit 15 | EVB link TX enable (also turns on hardware event building) / RX enable | as configured |
| 0x9370 | [31:16] status, [15:0] sticky errors | EVB3 status/error word (see EVB3_software_status_registers.md). Builds after 0xd6092496: [31:27] = count of RX FCS-bad frames (the bit-13 events) since SoftReset, saturating at 31 | errors 0 |

## Required changes to `EVB Init` (the link-reset path)
1. Only run the link-7 reset with the run stopped (0x9114 bit 7 clear on this DTC is the safe sign).
2. Read 0x9118, set bit 7, write it back. Do not disturb bits 6:0.
3. Read 0x9138 and 0x9128 once while the bit is set. Link-7 bits must now read 0 (proof the reset
   reached the transceiver; on the old build they stay 1). Log both values.
4. Clear bit 7 (read-modify-write, bit 7 = 0). **Mandatory.** Also do this once unconditionally at the
   start of `EVB Init`, in case an earlier attempt or crash left it set.
5. Poll 0x9138 bits 7, 15, 23, 31 all 1 and 0x9128 bit 7 = 1. Timeout 20 ms. If it times out, stop and
   report: that is a clocking fault, not a lock fault.
6. Poll 0x9140 bit 7 = 1. Timeout 200 ms (needs 255 idles from the other DTC, which arrive continuously
   when its TX is enabled). Before declaring failure, check the OTHER DTC has 0x9114 bit 7 set and its
   own 0x9138/0x9128 link-7 bits at 1: if the peer is not transmitting, this DTC can never lock and a
   reset here is pointless.
7. After a reset on one DTC, re-read 0x9140 bit 7 on the other DTC as well and wait for it to return to
   1 (it drops during our reset). Report both.
8. Keep the 3-attempt loop, but wait for step 5 between attempts instead of firing resets back to back.
9. Print in the `EVB Init` result, per DTC, before and after each attempt: 0x9004, 0x9118, 0x9138,
   0x9128, 0x9140, 0x9108, 0x93A0, 0x9114, 0x9370. Hex, one line each. This is what firmware needs to
   see when a build refuses to lock.

## Verification plan on the new bitfile (two DTCs, both locked to start)
| # | do | expect | proves |
|---|---|---|---|
| 1 | on DTC_A write 0x9118 bit 7 = 1, read 0x9138, 0x9128 on DTC_A | 0x9138 bits 15 and 31 = 0 (bits 7, 23 and 0x9128 bit 7 stay 1: measured 2026-09-24, this is the reset's scope) | the reset reaches the link's start-up sequencers (old build: nothing changes) |
| 2 | still held: read 0x9140 on DTC_B | bit 7 = 0 | whole-link reset stops our TX, peer loses lock as documented |
| 3 | still held: read 0x9138 / 0x9140 bits 6:0 on DTC_A | unchanged from before | bits 6:0 (ROC links) untouched by a bit-7 write |
| 4 | clear bit 7, poll | 0x9138 link-7 bits and 0x9128 bit 7 back to 1 within 20 ms; 0x9140 bit 7 back to 1 on BOTH DTCs within 200 ms | link recovers on its own after the level is released |
| 5 | write bit 7 = 1 and leave it 5 s, then SoftReset (0x9100 bit 31) | 0x9118 reads 0 and the link comes back (measured 2026-09-24; the original expectation "still 0x80" was wrong) | SoftReset clears the register; software's own clear is belt-and-braces |
| 6 | clear bit 7 | link back as in 4 | |
| 7 | run `EVB Init` normally on both DTCs | `Tx OK Rx OK CDR LOCKED` on both, register dump printed | the macro sequence works end to end |
| 8 | if a DTC shows `CDR Not Locked` after the 3 attempts | register dump shows 0x9138 all link-7 bits 1 and 0x9128 bit 7 = 1 | then it is the 64b/66b layer again: stop, keep the bitfile loaded, hand to firmware (link-7 ILAs are in this build) |

## Status 2026-09-24: software side implemented (DTCFrontEndInterfaceImpl.cc, EVBInit)
Software agent report, checked against the RTL by firmware:
- Bit 7 cleared unconditionally at the start, read-modify-write on 0x9118, proof read of 0x9138/0x9128
  while held, mandatory clear, 20 ms transceiver poll, 200 ms lock poll, peer check first, peer re-lock
  poll, register dump before/after each attempt: all as requested.
- Reset path runs only if 0x9140 bit 7 is not set 200 ms after the SoftReset. Good: a healthy bench sees
  no link-7 reset at all.
- The old ResetSERDESTX/RX/ResetSERDES(DTC_Link_ALL) writes are gone from EVB Init. Good: they hit the
  ROC and CFO links (bits 6:0 of 0x9118 are live) and never touched link 7 anyway.

Two firmware notes on the implementation:
1. "Peer not transmitting" check: use the peer's 0x9138 link-7 bits and 0x9128 bit 7, NOT its 0x9114
   bit 7. A DTC with EVB TX disabled still sends idle words (the TX block idles by default; the TX
   enable only gates data frames), so the peer's lock can be established with the peer's TX enable
   clear. Treating "peer 0x9114 bit 7 = 0" as "cannot lock" would refuse to reset when the reset is
   exactly what is needed. If the code stops on the peer's TX enable, relax it to the two health words.
2. Disabling TX (0x9114 bit 7) before the reset also resets the TX frame machine and the TX-side
   sticky error flags in 0x9370[15:0]. Fine at init, but it means a 0x9370 read after EVB Init no
   longer shows TX-side errors from before it. Read 0x9370 before EVB Init if that history matters.

Not yet done on the bench: the verification table above (proof read, peer lock drop, SoftReset does
not clear, recovery times). Run it on the first build after 0xd6092493 and paste the dumps.

## Bench run 2026-09-24 18:05 on 0xd6092496: answers to the software agent's questions
Result as reported: EVB Init locked first try on both DTCs, 18 of 18 CDR reads LOCKED, 100k events clean
(zero-sum PASS, wire loss 0 both ways, RxMissingPktCnt 0, 0x9370 sticky = bit 13 on both, bit 4 on DTC_1).
The reset path (0x9118 bit 7) was not taken and is still unexercised; the six pre-Init EVB Status reads
showing CDR LOCKED with TX/RX disabled confirm the idle-flow point from the earlier correction.

**Q: CDR lock difference between 0xd6092493 and 0xd6092496: firmware or fiber?** Firmware changed three
files: DTC.v (version stamp, link-7 ILAs on), EVB3.v (0x9118 bit 7 now resets link 7), and the sim
wrapper. Nothing in the link's own code changed. Timing met in both builds; the link's descrambler and
block-sync logic landed about 100 slice rows away from where it was, and the GT-to-descrambler path lost
0.45 ns of margin but still meets. So there is no firmware fix to point at; the honest answer is "a
different placement, and possibly the fiber". Firmware does not know whether the fiber was reseated; that
is a bench question for Ryan. With a roughly 2-in-10 history, one good build proves nothing. Keep the
per-build lock result in the log at the bottom of `ai_skills/evb_link7_lock_study.md`.

**Q: bit 4 on DTC_1 at the first poll, 0 subevents, never again.** Bit 4 is the OR of two things: the
TX state machine's own fault flags (undefined state, or destination offset > 31) and the TX window overrun
detector (a frame still on the wire when the destination rotation moved to another destination). With
start node 0x7c and DTC_1 = 0x7d the offset is 1, so it is the overrun detector. The likely mechanism:
- Each DTC's destination rotation is not running until it sees its first event window marker; the first
  marker starts it, and from then on it advances on every 4th 40 MHz marker. Both markers come from that
  DTC's own CFO emulator, which is started by its own enable write.
- The two emulators were enabled milliseconds apart, so for that interval DTC_0 was already rotating and
  sending idle frames while DTC_1's rotation had just started at a different phase. On this bench each
  DTC has only one destination, so the rotation is destination / dead time / destination; a frame that
  starts near the end of a window and runs into the dead time trips the detector.
- The detector had been clean since the 09-22 wire-time fit fix, which sizes each frame against the
  remaining window. The one thing the fit cannot cover is the very first window after the rotation
  starts, when the countdown has just been loaded and the first idle or data frame goes out immediately.
This is a first-window startup artifact, not a run-time overrun. Nothing was lost: TxCount, RxCount and
RxMissingPktCnt agree exactly on both sides. Confirming it needs the evb_tx_ila on DTC_1 triggered on
`tx_window_overrun` (one-clock pulse) with the run started the same way. Until then treat "bit 4 alone,
at the first poll, no repeat, exact wire counts" as expected when the two emulators are not started
together; a run in which it appears later, or with a wire deficit, is a real overrun.
Software mitigation that removes the ambiguity: enable both CFO emulators in the same write group with
DTC_1 first, or SoftReset both DTCs after both emulators are running and before the first event. The
protocol note "all DTCs SoftReset together before the first event" already asks for this.

**Bit 13 on both DTCs at the first poll (every build since 09-17).** The FCS compare arms itself only
after the first checked frame (`rx_fcs_first_armed`), and both known false-alarm causes (window close on
a bubble; bubble right after the T) were fixed before this build. So a bit 13 at the first poll on a build
after 0xd60922a0 is either a real bad-FCS frame at startup or a third startup case in the compare. It is
on the evb_rx_ila trigger list (`rx_fcs_bad` pulse, probe13[10]) and that ILA is in this bitfile. Capture
recipe: arm evb_rx_ila on rx_fcs_bad == 1 on both DTCs BEFORE the emulator enable, start the run as
usual, read both captures. Software keeps treating bit 13 as real; do not add an exception for it.

**Q: bit 22 stuck for ~100 ms stretches with events flowing.** Bit 22 = any RX source buffer at or above
3/4 (768 of 1024 words). It is a level, not an error. On this bench the only source buffer with traffic is
the one peer, so "which source buffer" is already known. The RX source buffers drain through the buffer
manager into the DMA stream; if software's poll loop is CPU bound, the PCIe DMA is the throttle, bit 23
(DMA back-pressure) shows the same thing from the PCIe side, and the credit protocol holds the peer's TX
(peer sees bit 18 CREDIT_THROTTLE: DTC_0 124 samples, DTC_1 37). All of that is consistent with the
sender being throttled by the receiver's drain rate, which is the protocol working as designed, not loss.
Loss would show as bit 0 (buffer written while full) or a wire-count deficit; neither happened. No ILA
needed for bit 22 at this point. If you want it quieter, drain faster (thin the 0x9370 read, or read it
only when the event queue is empty plus once per N iterations); the stall-onset information is preserved
either way because the bits are levels. Firmware has no objection to thinning; the dense sample was
useful once and this run is it.

**Bit 17 only on DTC_0.** Self-throttle holds this DTC's own subevent when its tag leads the slowest peer's
latest delivered tag by more than 1024. DTC_0 was enabled first and stayed slightly ahead; expected.

**Which run next.** 6 links x 300 pkt x 3.4 us on this build: it is the run that exercises the DDR path
and the 60-word question, and it is the one this build has not seen. The 10 pkt x 3.4 us run is worth
doing after it only if bit 22 at 1.7 us still matters after the polling change.

**Also, on a good build:** run the 0x9118 bit-7 verification table (section "Verification plan"). It has
never been exercised; a good build with the run stopped is the safe time.

## Verification table run 2026-09-24 21:24 on 0xd6092496: result and firmware reading
All eight steps run by the new `Exercise Link 7 Reset` macro, both DTCs, then EVB Init on both (21:27).
| step | result | firmware reading |
|---|---|---|
| 1 | 0x9138 0xffffffff -> 0x7fff7fff (bits 15, 31 drop; 7, 23 stay), 0x9128 stays 0x3ff | correct scope. The reset restarts the wizard start-up sequencers; they re-pulse the GT resets for microseconds and never re-reset a locked PLL. Rows 1 and 5 of the table above are corrected to what was measured; the macro should print this as "sequencers reset, GT/PLL held" rather than FAIL |
| 2 | peer 0x9140 0xc1 -> 0x41 | as expected |
| 3 | bits 6:0 unchanged | as expected |
| 4 | recovery 0 / 0 / 0 ms | faster than the 20 / 200 ms bounds; keep the bounds |
| 5 | SoftReset cleared 0x9118 to 0, link came back | firmware text was wrong: the register block's SoftReset branch zeroes 0x9118. Docs corrected. EVB Init's unconditional clear stays |
| 6, 7 | recover; EVB Init Tx OK Rx OK CDR LOCKED both, locked path, dumps printed | as expected |
| 8 | not reached | |
| extra | DTC_0 showed bit 1 (RX seq gap) for one read after DTC_1's release | expected: the peer's TX sequence restarted mid-stream; a link bounce is a protocol reset. SoftReset all before the next run, as always |
Both DTCs identical; nothing distinguishes the one that would not lock at 14:06. The reset path is now
verified end to end. Software cleanups (comment fix, step-1 wording) are fine to do now; no behavior change.

## Run 2026-09-24 21:07 (6 x 50 pkt, 3.4 us) did not start: bit 13 start-gate race. Firmware answer
What the log shows, read against the RTL: before a DTC's CFO emulator is enabled, its EVB TX sends only
raw 64b/66b idle words (that is what keeps the peer's CDR LOCKED); no EVB frames go out, because the
destination rotation sits at the illegal value until the first event window marker and the TX window
budget is 0 there. The emulator enable produces the first marker, the rotation starts, and the first EVB
idle PACKETS (0x78 frames) leave. So "DTC_1 enabled, and within a few ms bit 13 latched on DTC_0" means:
**bit 13 fires on the peer's very first frames after its rotation starts**, every run, both directions.
Same moment as the bit 4 seen on the later-enabled DTC in the 18:05 run. The receiver already skips its
first checked frame (`rx_fcs_first_armed`), so it is the 2nd or later frame of the sender's first burst.

Is it real? Unknown, and that is the point: three compare bugs have been fixed since 09-17 and it still
fires, always before data, never with a wire deficit across many 100k runs. Because the sticky bit is set
at startup on every run, it is blind for the rest of the run: nobody knows whether it fires once or
continuously. Firmware has no counter of FCS failures (candidate small change: a per-source BRAM stat
row or a saturating count in 0x9370[31:27]; Ryan's call).

It is reproducible on every start, which makes it a one-capture ILA job on this bitfile:
1. On the receiving DTC (say DTC_0) arm `evb_rx_ila`, trigger `rx_fcs_bad == 1` (probe13[10]), trigger
   position late in the window (about 900 of 1024) so the previous frame and the failing frame are both
   in the capture. Signals to read: `rxdata`, `rxctrl`, `rxdatavalidp`, `rxCRCword` (probe22[47:16], the
   engine's CRC at the compare beat), `rxCRC_frame78`, `rx_fcs_check_pending`.
2. On the sending DTC (DTC_1) arm `evb_tx_ila`, trigger on the first frame start (`tx_mon_in_frame`
   rising) or on `tx_window_overrun` (evb_tx_ila probe19[13] since 2026-09-28; was probe21[30]). Read `txdata`, `txctrl`, `txdatavalid`,
   `DestinationDTC_ip`, `tx_window_words_remaining`.
3. Then enable DTC_1's emulator. Both ILAs fire on the first burst. Compare the sender's T block FCS with
   the receiver's rxCRCword: equal = receiver compare bug; different = the sender's first frames really
   carry a bad FCS (sender-side startup) or the wire corrupts them.
Export CSVs, read with `DTC/parse_ila.py`.

**On options (a)/(b)/(c):** firmware view.
- (d), not in the list: fix the race, not the gate. Do the readiness read on ALL DTCs first, then enable
  all emulators. Today each thread does check-then-enable on its own, so whichever DTC enables first
  poisons the other's check. With a check-all-then-enable-all order, bit 13 latches after both are
  running and shows up in the end-of-run report exactly as it has on every run. No exclusion, no
  override, honest log. If the FEMacro framework cannot barrier across the two targets, then:
- (b) is acceptable: an operator-typed mask for the START gate only, printed in the result. The
  end-of-run validity must still report bit 13; do not mask it there.
- (a) hides a bit we have not explained; no.
- Either way, keep "bit 13 = real" in the software until the ILA pair above says otherwise.

## 2026-09-24 22:05-22:20: bit 13 root cause found; answers to the 22:04 and 22:17 reports

**Bit 13 is a receiver-side compare bug, not bad data on the wire.** Ryan's evb_rx_ila capture (iladata_7,
trigger rx_fcs_bad): every frame's FCS on the wire matches its own words. When a frame's LAST data word
lands on a gearbox bubble, the link repeats that data word for one beat but the control flag has already
moved on to the terminate block's flag. The FCS checker took "control flag" to mean "the terminate block is
here", closed one beat early, skipped the last word, never compared, and the NEXT frame was judged on top
of the leftover. One frame in about 33 has its last word on a bubble, so on idle traffic this happens within
the first frames after every start: that is the "first poll, every run" pattern. Fixed in firmware (close
and enable now look at the data byte, not the flag); in the next build bit 13 should stay clear and the
new count in 0x9370[31:27] should read 0. Until that build, treating bit 13 as an exclusion on 0xd6092496
is correct and matches the physics. Do not add an exclusion for the next build: bit 13 there is real.

**60-word DDR-path loss (0xd60922a0): closed as fixed between builds.** The cause was found and fixed on
2026-09-23 (DDR write CDC FIFO dropped a word handshaked on the cycle it filled; fixed with a registered
almost-full, sim-verified run 28). 0xd60922a0 did not have the fix; 0xd6092496 does. Four clean 300-pkt runs
(three at 3.4 us, one at 1.7 us) with exact word counts and bit 3 clear confirm it. No ILA needed.

**Runs 1 and 3 at 6 x 300 x 1.7 us "stalled": agree with the software reading, and the register that
tells the difference is bit 17.** Nothing was lost (RxMissingPktCnt 0, wire exact at the mid-run read).
At this rate the ROC emulator cannot answer 300 packets x 6 links in 1.7 us, so the DTCs live in
back-pressure; bit 19 (DDR channel almost-full) for 300-400 polls with bit 16 (ROC held) for the same
stretch is the design working: the DDR write side holds the ROC input while it drains, and a 7-10 ms
stretch is fine (bit 20, DDR CDC actually full, is the one that would mean a real jam, and it never set
during data). What made 1 and 3 stop and 2 finish is the self-throttle: a DTC whose tags lead its slowest
peer's delivered tag by more than 1024 holds its own stream (bit 17). With one DTC DDR-bound and the other
drain-bound, the two drift apart, one crosses 1024, and both freeze until software drains; the "513 open
tags" is software's half of the same 1024 window. Watch bit 17 per DTC in the poll log for runs 1 and 3
versus 2; expect it set for long stretches only in 1 and 3. There is no register for DDR fill; bit 19 is a
level from the write buffer's almost-full and bit 20 is the CDC full flag. If you want a number, ask Ryan
for a DDR occupancy readout (the fifo manager has a read-occupancy port that is not exported).

**Wire-loss line across a reset / mid-run: agree with all three software items.** Snapshot both DTCs'
counters before the halt issues any reset, gate the wire check on both threads stopped and no reset since
run start, and thin the 0x9370 change logging to the sticky half. Go on all three, one rebuild.

**Run sequence:** firmware cannot see the macro log; confirm run 3's halt with Ryan.

**Repeat 6 x 300 x 1.7 us after the software fixes:** yes, once, to get a clean wire check on a run that
stops on the throttle. Log bit 17 per DTC.

**Rebuild sequence question:** the bitfile on the bench is 0xd6092496 (16:56) throughout; every macro
change since is software only. The reset-macro verdict wording and the exclusion are both software; which
of them was in which library build is a software log question, not a firmware one.

**Next firmware build will contain:** the bit-13 fix, 0x9370[31:27] FCS-bad count, the link-7 ILAs, the
0x9118 bit-7 reset. Expected on the bench: bit 13 clear at the first poll with the count at 0; if the count
is ever non-zero with bit 13 set, that is a real bad frame and the evb_rx_ila trigger is the next step.

## 2026-09-25 00:10-00:40: build 0xd60924a0 (20:00) -- the counter is counting real checker events, the checker was still wrong, now fixed for real

**Bits 31:27 count exactly what they were meant to: rx_fcs_bad pulses.** They saturated during clean data
because the checker in 0xd60924a0 still misfires. Ryan's second capture (00:25, same trigger) shows why: the
09-24 fix judged "control block" by the data byte (0xCC or 0x1E) when the flag was ambiguous. A payload
word whose top byte is 0x1E, on a bubble, was then treated as an idle: skipped by the engine, no compare,
and the NEXT frame blamed. Idle packets (0x23.. payload) never trigger that; data payloads do, dozens of
times per 100k run. Wire FCS was correct on every captured frame. So: still a receiver compare bug, still
no bad data, and the counter did its job by showing "continuously", which is what sent Ryan back for the
second capture.

**Real fix (in the dev source now, next build):** the control flag is re-paired with its data word at the
parser input, so no downstream test has to guess. Replayed offline against the capture and a 965-case
bubble sweep with 0 failures.

**Exclusion policy:** add 0xd60924a0 to the exclusion list (its bit 13 is known-false). The build after it
is the first where bit 13 and the count should read 0 on every run; do not exclude that one. If the count is
non-zero there, it is real. Option (c), an operator override per session, is worth having anyway for the
next surprise; firmware has no objection.

**Bit 12 at teardown (run 2, DTC_0, alone, during the SoftReset):** not meaningful. The wire-size counter
arms on an accepted frame and compares at the terminate block; a SoftReset mid-frame cuts the frame, the
count comes up short, and bit 12 latches for the one poll before the reset clears it. A bit 12 during data,
or one that survives the reset, would be real.

**Rebuild sequence (firmware view):** 0xd6092496 (16:56) had the link-7 ILAs and the 0x9118 bit-7 reset,
no bit-13 changes. 0xd60924a0 (23:03, stamp 20:00) added the counter and the first (wrong) checker fix.
Which software library build carried which macro change is a software log question.

**Next bench steps on 0xd60924a0:** Exercise Link 7 Reset once per DTC with the corrected verdicts (expect
all PASS), then 6 x 300 x 1.7 us with bit 17 logged and Status before and after Halt, as planned. Bit 13
excluded on this build, count expected at 31.

## 2026-09-25 09:53 report on 0xd60924a0: firmware reading, and the new bitfile 0xd6092589

**Reset table ALL PASS on both DTCs: agreed, nothing further on the reset path.** The values match the
three passes on 0xd6092496 and match the register code: bits 15/31 drop while held, 7/23 and 0x9128 bit 7
stay 1, SoftReset zeroes 0x9118, peer lock drops and returns. The 0x9140 baseline 0xc0 vs 0xc1 (ROC link 0
before its emulator is set up) is not a finding, agreed.

**Both 100k runs clean, and the bit-17 counter answers the Sep-24 question.** DTC_0 more than 1024 tags
ahead of DTC_1 for 14.7% of the 300-pkt run, DTC_1 never: the throttle held DTC_0, the run still finished,
so the Sep-24 stalls were throttle plus the 2 s assembly timeout, not loss. Keep the per-DTC bit-17 line in
every run report. The mid-run wire DIFF 65154 / 186 with both threads running, then OK 0 after halt, is the
in-flight difference doing exactly what the label says.

**Bits 13 and [31:27] on 0xd60924a0: known false, as ruled; nothing changes.** The count saturating at 31
on clean data is the 09-24 checker misfiring on 0x1E.. payload words (second ILA capture, 00:25). Keep
0xd60924a0 excluded.

**New bitfile is built: 0xd6092589 (2026-09-25 10:09).** It carries the real bit-13 fix (control flag
re-paired with its data word at the parser input), the 0x9370[31:27] count, the link-7 ILAs and the
0x9118 bit-7 reset. Both DTCs CDR-lock on it (Ryan, this morning). Expectation, and the first build where
it must hold: **bit 13 = 0 and bits [31:27] = 0 on every run, first poll included.** Do not add it to the
exclusion list. If the count is ever non-zero there with bit 13 set, that is a real bad frame: stop, keep
the bitfile loaded, tell firmware (evb_rx_ila trigger rx_fcs_bad is the next step).

**Answers to the three questions:**
1. Rebuild sequence 21:07-22:04 Sep 24: no firmware bitfile changed in that window. 0xd6092496 was on the
   bench from 16:56 until 0xd60924a0 (built 23:03, stamp 20:00) was loaded. Which software library build
   carried the exclusion vs the verdict wording is a software log question; firmware has no record of it.
2. Next bench step, firmware view: (a). Load 0xd6092589 and repeat exactly this session on it: reset table
   once per DTC, 6 x 50 and 6 x 300 at 1.7 us, bit 17 logged, Status before and after Halt. The pass
   condition that is new: bit 13 and [31:27] read 0 throughout, no exclusion. (b), emulators enabled DTC_1
   first to see whether the bit-17 side flips, is worth one run on the same bitfile afterwards; it tests
   the "whoever starts first leads" reading and costs nothing.
3. Git: Ryan's call, not firmware's.

## 2026-09-25 10:18-10:21 report on 0xd6092589: bit 13 closed; one real link-7 recovery; throttle asymmetry explained

**Bit 13 and 0x9370[31:27]: closed.** Six 300-pkt runs at 1.7 us, ~55k polls per DTC per run, no exclusion,
bits 0-15 never set and the count 0 on every read, wire and zero-sums exact. That is the acceptance
condition stated for this build, met on the first session. The bit-13 thread (three checker fixes since
09-16) is closed on the firmware side too. The exclusion list stays exactly 0xd6092293 .. 0xd60924a0 and
must never grow past it; from this build on, bit 13 with a non-zero count is a real bad frame, stop and
call firmware. The evb_rx_ila capture of the old checker is already on file (iladata_7 #1 and #2, in the
investigation note); nothing more to capture.

**Version word: 0xd6092589 is right, my "0xd6092509" was wrong.** The low byte of 0x9004 is
{6-ROC flag, stamp[6:0]}: stamp 26_09_25_09 -> 0x89. Same rule gave 0x93 / 0x96 / 0xa0 for the earlier
builds. Fixed in every firmware doc.

**Session start, DTC_0 link 7 unlocked (0x9140 = 0x41), one 0x9118 bit-7 reset fixed it: this is the
first real use of the new reset path, and it is worth recording.** Reading: after the bitfile load DTC_0's
transceiver was up (the reset only dropped 0x9138 bits 15/31, so bits 7/23 were 1 before it) but the link
never reached block lock or never saw 255 idles. Restarting the wizard sequencers and the block-sync machine
cured it at once, and it stayed locked for the whole session. That matches the 26_09_24_13 failure picture
(transceiver healthy, 64b/66b layer not locked) and is the strongest hint yet that the fault is in the
block-sync start-up, not the fiber or the transceiver. So: **the reset path is now proven useful, and every
EVB Init log line of the form "link 7 unlocked -> reset -> locked" should be kept and reported to firmware
with the 0x9140 / 0x9138 values before and after, plus which DTC.** Two in a row on the same DTC and I will
arm the RX link ILA (ila_19) on that DTC before the bitfile load to catch the block-sync machine in the act.

**Bit 17 always on DTC_0, never on DTC_1, regardless of enable order: expected, and it is a speed
difference, not a sequencing effect.** The throttle compares this DTC's own tag with the slowest peer's
delivered tag. DTC_0 runs its ROC emulator loop about 3% faster (your progress lines show it), so over a
300-pkt run it pulls more than 1024 tags ahead of DTC_1 and gets held for a while; DTC_1 is never ahead so
it never throttles. Enable order only shifts the first few hundred tags, well inside the 1024 window, which
is why run 3 looked the same. Run 6 (no throttle, fastest) is the run where the two happened to stay within
the window. Nothing to do; the throttle bounds software staging to 1024 events as designed. If the DTC_0
lead ever shows up as an event-rate loss you care about, the knob is the ROC emulator pacing, not EVB.

**Back-pressure bits 16/18/19/22 a handful per run, 20 and 23 never: healthy at this rate.** Same as
0xd60924a0.

**Nothing pending for firmware from this log.** Keep running on 0xd6092589. The only standing request is
the one above: report every link-7 recovery in EVB Init.

## Not changed
`Expected DTC MAC`, 0x9154, 0x9158, 0x915C, 0x9170 handling. Idle packet words, gap and burst count
are as before. 0x9370 semantics unchanged.

## Firmware references (for the record)
- EVB3.v, `reset_serdes` block near line 485: `SERDESReset` OR-ed into the link reset (2026-09-24).
- EVB10GBELink.v, `RXLockedCounter` block: the lock check behind 0x9140 bit 7.
- register_map.v `reg_rd_data_pre_*` lines: the bit layouts in the table above.
- DTC/EVB_link7_CDR_lock_investigation.md and ai_skills/evb_link7_lock_study.md: the full study.
