# EVB3 send-window alignment: what software reads and sets

For bitfiles built after `0xd6112095` (2026-11-21). Simulated on 6 DTCs; not yet tried on hardware.
All DTCs of a cluster must run the same bitfile for the readings below to mean anything.

## Why
Each DTC sends to one destination per window (4 clock-marker gaps, about 1047 transmit clocks of 6.2 ns).
All DTCs should open their windows together. Two things spoil that:
1. A DTC can start one clock-marker gap (about 262 clocks) late. On `0xd6112095` this happened to the five DTCs
   with permanent offset 0 in about 4 runs of 10, and those runs were 10 % slower.
2. The markers reach each DTC after a different number of hops down the timing chain (about 16 clocks per hop).

## The three registers
| register | what it does | reset |
|---|---|---|
| `0x9178` bit 19 | aligned start: 1 = every DTC counts its windows from the same clock marker (cures item 1) | 0; kept through SoftReset |
| `0x915C` [7:0] | marker delay in 5 ns steps: delays this DTC's windows by that much (for item 2). Bits [31:8] are other fields: read, change the low byte, write | 0; kept through SoftReset |
| `0x9160` type `0x6` | the measurement, one row per source DTC (below) | cleared by SoftReset |

`0x9178` in full: [15:0] self chunk cap (1024), bit 16 second credit read, bit 17 short last frame,
bit 18 leave 0, bit 19 aligned start. Today's setting `0x00030400`; with the aligned start `0x000B0400`.
Set the same value on ALL DTCs. A change of bit 19 or of `0x915C` takes effect at the next SoftReset
(the windows restart at the first event marker after it).

## Reading the offset between two DTCs
Row read: write `0x9160` = (0x6 << 5) | source offset, read `0x9160` twice, keep the second. Signed 32 bit, in
transmit clocks.
The row at DTC B for source A = how much later A's windows open than B's, plus the travel time through the switch.

For a pair A, B, with W = one window (about 1047 clocks):
- a = row for source A read at B; b = row for source B read at A
- travel time T = ((a + b) mod W) / 2
- offset = (a - T), folded into -W/2 .. +W/2 = clocks by which A's windows open LATER than B's

Rules for a good number:
- A row is only filled by idle frames; it reads 0 until the first one after SoftReset. Data frames leave it alone.
- Single readings are sometimes 30 to 80 clocks LOW (a frame that crossed the switch faster). Read the table
  several times, or use all the receivers that see a source, and take the value most readings agree on
  (the median), not the mean.
- Expected: aligned DTCs 0; one hop of chain delay about 16; a DTC started one marker gap late about +262.

## Procedure
1. Load the bitfile on all DTCs. Keep `0x9178` = `0x00030400`, `0x915C` low byte 0.
2. Run, read the table (several reads), compute each DTC's offset against one reference DTC.
   Check: in a slow run the five permanent-offset-0 DTCs show about +262; in a fast run they do not.
3. Set `0x9178` = `0x000B0400` on all DTCs. Run several times. Check: no DTC at +262, every run at the fast rate.
4. What is left is the chain delay. For each DTC: marker delay = (largest offset - own offset) x 6.2 / 5, rounded,
   written to `0x915C` [7:0] (0 to 255). The latest DTC gets 0.
5. SoftReset (the normal run sequence does it), run, read the table again. Check: all offsets within a few clocks.

Stop and report if `0x9370` bit 4 appears (a frame still on the wire when its window closed).

## On bitfiles up to `0xd6112095`
The type `0x6` row is the receiver's own time counter at the idle frame's arrival, and a data frame writes 0
over it. Only differences between rows of one receiver mean anything there (about 1020 clocks per source offset
= one window). There is no bit 19.
