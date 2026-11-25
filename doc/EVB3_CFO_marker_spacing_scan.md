# EVB3: How to Run a CFO Clock-Marker Spacing Scan

Audience: the software agent (and anyone writing the scan). Written 2026-11-24 by the CFO firmware agent.
Companion to `CFO_Register_Map.md`.

## What the scan is for

- In hardware event building the DTCs take turns on the 10GbE switch. A DTC sends to one destination for a
  "send window", then moves to the next.
- A send window is a whole number N of gaps between the clock markers the CFO sends. N is a DTC register (2, 3 or 4).
- Until now the gap was fixed at 1625 ns, so the window could only be 3250, 4875 or 6500 ns.
- Measured on 28 DTCs on 2026-11-24 (by the DTC firmware agent): N = 4 gave 729 MB/s per DTC, N = 2 gave 774,
  N = 3 gave 830. The best window is somewhere near 4875 ns, and whole multiples of 1625 ns cannot find it.
- The CFO gap is now a register, so the window length can be scanned in steps of N x 25 ns.

**Goal of the scan:** throughput per DTC as a function of window length (= N x gap), to find the best window
and the (N, gap) pair that gives it.

## The two registers that must agree

| Where | Register | Unit | Notes |
|---|---|---|---|
| CFO | 0x915C CLOCK_MARKER_SPACING | 25 ns ticks, bits 7:0 | default 65; writes clamped to 40..140; reads return the clamped value |
| every DTC | 0x92C4 (per the DTC firmware agent) | 10GbE transmit clocks of 6.2061 ns | the gap the DTC plans its send window with |

- The CFO register sets the real gap. The DTC register tells each DTC what gap to expect.
  **They must describe the same gap, on the CFO and on every DTC, or the DTCs' windows drift against each other.**
- Conversion: DTC clocks = ticks x 25 ns / 6.2061 ns = ticks x 4.02832. It is never a whole number
  (65 ticks = 261.84 clocks; the DTCs measure 261 to 262).
- Rounding rule for 0x92C4, as given to the software agent by the DTC firmware agent: floor(ticks x 25 / 6.206) - 4,
  in whole-number arithmetic (61 -> 241, 65 -> 257, 75 -> 298, 82 -> 326). The table below gives the exact,
  unrounded value for reference. N is set through DTC register 0x9178 (default value 0x00730400 per the software agent).
- The DTC register address, its format and the address of the N register are DTC-side facts. This document
  repeats what the DTC firmware agent said; the DTC register map is the authority.

## Before the scan: check the CFO firmware

- The spacing register exists from CFO firmware version **0xC6112415** on (built 2026-11-24).
- Read 0x915C. New firmware returns 65 after a Hard Reset. Older firmware returns 0 and ignores writes.
  **If it reads 0, stop: the scan would change nothing.**
- Clock markers must be enabled: 0x9100 bit 1 (it is on by default).

## How the CFO behaves (what software can rely on)

- **A new value takes effect at the next clock marker**: one gap at the old length, then every gap at the new
  length. There is no short gap. A write is safe at any time; no reset is needed.
- **All 8 CFO links send clock markers on the same 25 ns tick.** One schedule is shared by all links.
- **A clock marker whose tick is taken by an event window marker is left out**, not sent late, and the schedule
  does not move. The DTC supplies the missing marker itself for its window count.
- Event window markers, Heartbeat Packets and everything sent to the ROCs do not depend on this register.
- **Only two things change the register:** a write to 0x915C, and a Hard Reset (0x9100 bit 0, also done by the
  firmware itself when the FPGA is configured), which puts it back to 65. A Soft Reset does not change it, and
  neither does loading, enabling or running a run plan.

## Scan procedure

For each (N, ticks) point:

1. **Stop the data flow** the way the throughput test normally does between configurations. Changing the gap
   under load is safe for the CFO, but the DTCs see the CFO change before their own register changes.
2. **Write the CFO**: ticks to 0x915C. Read it back and compare. A different value means the clamp acted
   or the firmware is old: record it and skip the point.
3. **Write every DTC**: the matching value to 0x92C4, and N to the DTC's window register. Read both back on
   every DTC.
4. **Wait** at least 1 ms (hundreds of gaps) so every DTC has counted several full windows at the new gap.
5. **Start the data flow and measure** throughput per DTC with the same method and duration as the reference
   measurement above, so the numbers are comparable.
6. **Record** for the point: N, ticks, gap in ns, window in ns, the 0x92C4 value written, per-DTC throughput
   (minimum, mean, maximum over DTCs), and any DTC or switch error counters the test already reads.

At the end, and on any error or abort: **write 65 back to the CFO and the matching default to every DTC**, and
read them back. Leaving a non-default gap behind would silently change every later run.

## Which points to take

Order asked for by the DTC firmware agent:

1. **65** first: must reproduce the reference numbers (729 / 830 / 774 MB/s for N = 4 / 3 / 2).
   If it does not, stop and report; the scan would not be comparable.
2. **75**.
3. **62 to 82 in steps of 1.**

- **Keep ticks at 61 or more** (DTC firmware agent's limit). The CFO accepts down to 40, so software must
  enforce 61 itself.
- Take each gap with N = 2, 3 and 4 if time allows. The three N values cover different window ranges:

| N | Window for ticks 62 to 82 |
|---|---|
| 2 | 3100 to 4100 ns |
| 3 | 4650 to 6150 ns |
| 4 | 6200 to 8200 ns |

- Analyse by **window length**, not by ticks: plot throughput against N x gap with one curve per N.
  If the curves for different N agree where their windows overlap or meet, the window length is what matters,
  and the smallest N that reaches the best window is the one to use (Ryan would like N = 2 to be usable).
- Repeat 65 at the end of the scan. A different result from the first 65 means something drifted during the scan.

| ticks | gap (ns) | exact DTC clocks (6.2061 ns) | window N=2 (ns) | N=3 (ns) | N=4 (ns) |
|---|---|---|---|---|---|
| 62 | 1550 | 249.76 | 3100 | 4650 | 6200 |
| 63 | 1575 | 253.78 | 3150 | 4725 | 6300 |
| 64 | 1600 | 257.81 | 3200 | 4800 | 6400 |
| 65 | 1625 | 261.84 | 3250 | 4875 | 6500 |
| 66 | 1650 | 265.87 | 3300 | 4950 | 6600 |
| 67 | 1675 | 269.90 | 3350 | 5025 | 6700 |
| 68 | 1700 | 273.93 | 3400 | 5100 | 6800 |
| 69 | 1725 | 277.95 | 3450 | 5175 | 6900 |
| 70 | 1750 | 281.98 | 3500 | 5250 | 7000 |
| 71 | 1775 | 286.01 | 3550 | 5325 | 7100 |
| 72 | 1800 | 290.04 | 3600 | 5400 | 7200 |
| 73 | 1825 | 294.07 | 3650 | 5475 | 7300 |
| 74 | 1850 | 298.10 | 3700 | 5550 | 7400 |
| 75 | 1875 | 302.12 | 3750 | 5625 | 7500 |
| 76 | 1900 | 306.15 | 3800 | 5700 | 7600 |
| 77 | 1925 | 310.18 | 3850 | 5775 | 7700 |
| 78 | 1950 | 314.21 | 3900 | 5850 | 7800 |
| 79 | 1975 | 318.24 | 3950 | 5925 | 7900 |
| 80 | 2000 | 322.27 | 4000 | 6000 | 8000 |
| 81 | 2025 | 326.29 | 4050 | 6075 | 8100 |
| 82 | 2050 | 330.32 | 4100 | 6150 | 8200 |

## Things to watch for

- **Mismatch between CFO and DTCs.** The most likely way to get a wrong result. Read back every register
  at every point and store the read-back values with the data.
- **Gaps close to the event spacing on spill.** On spill the CFO sends an event window marker every 1695 ns
  (67.8 ticks). At the default 65 ticks about 1 clock marker in 68 is left out, spread evenly. At 67 and 68 ticks
  the clock markers and event markers stay lined up for several gaps in a row, so several consecutive
  clock markers can be left out, followed by a long stretch with none. This is the CFO firmware agent's reasoning
  from the firmware, not a simulated result. Flag these two points in the results and ask the DTC firmware
  agent whether consecutive missing markers are handled.
- **Off spill** (one event per 100 us) at most one clock marker per 100 us is left out, at any gap.
- **A run plan with a fixed event window (for example 68 ticks, no RF0) locks to the clock-marker schedule.**
  Both then repeat exactly, so which clock markers are left out depends on the gap and on the phase between
  the two, which is set when the run plan is launched and differs from run to run:
  - gap = 68 ticks with a 68-tick event window: **either every clock marker is left out for the whole run, or none.**
  - even gaps (62, 64, 66, 70, ...): a fixed fraction is left out (1 in 34, or 1 in 17 for gaps divisible by 4), or none.
  - odd gaps (63, 65, 67, ...): always exactly 1 in 68, whatever the phase. 65 is one of these.
  This follows by arithmetic from the simulated skip rule; it is not itself simulated. Keep the event window the
  same at every point (it sets the data rate), but treat 68 as a special point and expect even gaps to vary
  between repeats.
- **Do not use 0x9200 to tell whether beam was present.** It is the low 16 bits of the RF0 count inside the
  current spill; it restarts at every spill and reads 0 between spills. Use 0x9244 (Heartbeat Packets sent while
  the CFO's on-spill flag was high, 24 bits, cleared by Soft Reset): if it grew during the run, RF0 was present
  for at least part of it. 0x9248 is the same count for off spill, so the two give the on/off-spill share.
- **Beam state changes the result.** Note for every point whether RF0 was present (on spill) or not.
- **Simulation only so far.** The register and the skip rule are simulated and built, not yet tried on hardware.
  The register write and read-back path was checked by code reading only, so the read-back in step 2 is also
  the first hardware test of it: report it if 65 does not read back as 65.

## Sources

- CFO firmware: `src/CFORingInterface.v` (modules `CFORingInterface`, `ClockMarkerScheduler`), `src/register_map.v`.
- Simulation: `ai_sim_evidence/cfo_clockmarker_tb.spacing_register.log` (5 phases passed).
- DTC-side facts (0x92C4, N, the 61 limit, the reference throughput numbers): the DTC firmware agent's notes of 2026-11-24.
