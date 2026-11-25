# EVB3 Software Window Alignment

How software sets up and checks the DTCs' event-building send windows.
Register detail: [EVB3_software_status_registers.md](EVB3_software_status_registers.md).

Describes the bitfile AFTER `0xd6112399` (2026-11-24, not built yet when this was written). The last section lists
what differs on `0xd6112399` and earlier. All DTCs of a cluster must run the same bitfile and the same settings.

## What a send window is
Each DTC sends to one destination per window and steps to the next destination when the window ends. All DTCs must
open their windows together. A window is a number of clock-marker gaps (one gap is about 262 transmit clocks of 6.2 ns):

| markers per window | window W | a full window carries |
|---|---|---|
| 4 | about 1047 clocks | 4 frames and a short one, about 824 words |
| 3 | about 785 clocks | 3 frames and a short one, about 598 words |
| 2 | about 523.5 clocks | 2 frames, about 372 words |

## The registers
| register | what it does | reset |
|---|---|---|
| `0x9178` [15:0] | longest self chunk to the DMA, words (use 1024) | 1024 |
| `0x9178` bit 16 | second credit read inside a window (use 1) | 0 |
| `0x9178` bit 17 | short last frame: a frame that no longer fits the window is cut to fit (use 1) | 0 |
| `0x9178` bits 18, 19 | not used, read 0 (removed after `0xd6112496`; on `0xd6112496` and earlier: write 0) | |
| `0x9178` bit 20 | marker fill: a clock marker the CFO left out is supplied to the window count (use 1) | 0 |
| `0x9178` bit 21 | deep prefetch: the next window's words are staged ahead (use 1) | 0 |
| `0x9178` [23:22] | clock markers per window: 0 = 4, 1 = 3, 2 = 2 (3 = 1, do not use) | 0 |
| `0x9178` [31:24] | not used, reads 0 | |
| `0x92B8` [8:0] | marker delay in 5 ns steps, 0 to 300 (larger values are stored as 300): delays this DTC's windows | 0 |
| `0x92C4` [9:0] | planning gap: the clock-marker spacing the sender plans with, transmit clocks. Write floor(CFO ticks x 25 / 6.206) - 4 (62 ticks: 245, 65 ticks: 257) | 254 |
| `0x92BC` [31:0] | read-only: clock markers this DTC found missing since SoftReset | cleared by SoftReset |
| `0x9160` type `0x6` | read-only: the window offset to each source DTC (below) | cleared by SoftReset |
| `0x9370` | error and status bits; bit 4 = a frame was still on the wire when its window closed | errors cleared by SoftReset |

`0x9178`, `0x92B8` and `0x92C4` keep their values through SoftReset and are cleared by a bitfile load: write them after every
load, before the first run. A change of either takes effect at the next SoftReset.

`0x9178` values:

| value | meaning |
|---|---|
| `0x00330400` | 4 markers per window, all options on (the setting on `0xd6112399`) |
| `0x00B30400` | 2 markers per window, all options on |
| `0x00730400` | 3 markers per window, all options on |

## Settings to use (study closed 2026-11-25)
DTC bitfile `0xd6112496`, CFO bitfile `0xC6112415`, 28 DTCs:
- CFO `0x915C` [7:0] = 62 (clock-marker spacing in 25 ns ticks; the CFO accepts 40 to 140, reset 65)
- DTC `0x9178` = `0x00730400` (3 markers per window, window 4650 ns)
- DTC `0x92C4` = 245
- DTC `0x9158` [31:16] = 32 (dead time, written by EVB Init)
- marker delays in `0x92B8` from the alignment procedure below

Result: 846 MB/s per DTC, wire use 66.5 %. With these settings W = 3 x 62 x 25 / 6.206 = 749.3 clocks.
Do not use 66 to 69 ticks on `0xd6112496` (clock markers go missing several in a row there).

## Reading the offset between two DTCs
Row read: write `0x9160` = (0x6 << 5) | source offset, read `0x9160` twice, keep the second. Signed 32 bit, in
transmit clocks. The row at DTC B for source A = how much later A's windows open than B's, plus the travel time
through the switch.

For a pair A, B, with W = the window length in clocks (table above; use 1047, 785 or 523.5):
- a = row for source A read at B; b = row for source B read at A
- travel time T = ((a + b) mod W) / 2
- offset = (a - T), folded into -W/2 .. +W/2 = clocks by which A's windows open LATER than B's

Rules for a good number:
- A row is only filled by idle frames; it reads 0 until the first one after SoftReset.
- Single readings are sometimes 30 to 80 clocks low. Read several times and take the median, not the mean.
- Reads after the last event count the same as reads during data, as long as they come before the next SoftReset.
- The formula cannot tell T from T + W/2. Check on a few sets of three DTCs:
  offset(A,B) + offset(B,C) + offset(C,A), folded, must be near 0. If it is near +-W/2, add W/2 to T and recompute.
  Measured travel time on the hardware switch: 137.5 to 138 clocks. With 2 markers W/2 is 262, still above it.

## Procedure after every bitfile load, recabling, or change of the DTC list
1. Write `0x9178` (the value to be used) on all DTCs. Write `0x92B8` = 0 on all DTCs.
2. Three runs. Compute each DTC's offset against one reference DTC (median over reads and runs).
3. For each DTC: marker delay = (largest offset - own offset) x 6.2 / 5, rounded. The latest DTC gets 0.
   Write it to `0x92B8`. Measured spread on 28 DTCs: -15 to +198 clocks, about 28 clocks per hop down a timing
   chain, so the largest delay is about 264.
4. One run to verify: all offsets within about 3 clocks of the reference.
5. After a run, `0x92BC` should read the same on all DTCs within 1 or 2 (about 4545 for 100,000 events).
   A DTC that reads more than the others missed clock markers the others got.

Stop and report if `0x9370` bit 4 appears, or any DTC's offset moves by more than 10 clocks between runs.

## What differs on `0xd6112399` and earlier
- The marker delay was `0x915C` [7:0] (0 to 255). `0x92B8` did not exist. From the next bitfile on `0x915C` [7:0] is
  not used and reads 0.
- `0x9178` [31:24] read back a copy of the missing-marker count capped at 255, so a readback had to be compared with
  `& 0x00FFFFFF`. From the next bitfile on those bits read 0.
- `0x9178` [23:22] did not exist: 4 markers per window, always.
- `0x9178` bit 21 exists from `0xd6112399`, bit 20 from `0xd6112289`, `0x92BC` from `0xd6112399`.
- Up to `0xd6112095` the type `0x6` row was not a window offset at all.

## Results so far (28 DTCs, 600 packets per ROC)
| bitfile | setting | rate per DTC |
|---|---|---|
| `0xd6112289` | marker fill off | 4 runs of 10 slow (638 MB/s), the rest 698 |
| `0xd6112289` | marker fill on | 698 to 700 MB/s, no slow run |
| `0xd6112289` | + marker delays | 713 MB/s, all DTCs within 4 clocks |
| `0xd6112399` | + deep prefetch | 729.5 MB/s |
| `0xd6112490` | 3 markers per window, 65 ticks | 830 MB/s |
| `0xd6112496` + CFO `0xC6112415` | 3 markers, 62 ticks, `0x92C4` = 245 | 846 MB/s, wire use 66.5 % |

Spacing scan of 2026-11-24/25 (77 runs, 1 to 4 markers, 58 to 93 ticks): the rate follows the window length and
is flat at 845 to 848 MB/s for windows of 4400 to 4650 ns. Full table in `EVB_window_length_2026-11-24.md`.
