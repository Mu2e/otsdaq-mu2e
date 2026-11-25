# CFO: How to Load the Off-Spill and On-Spill Run Plans

Audience: software developers and agents writing run plans into the CFO over its register interface.
Companion to `CFO_Register_Map.md`. Written 2026-11-16 from the firmware source on branch `rrivera/commissioiningRF0marker`
(uncommitted working copy of that day). See "How this was checked" at the end for what is and is not tested.

## Rules that changed in this firmware (software must follow these)

| # | Rule | Before |
|---|---|---|
| 1 | **GOTO target is relative to the plan's base**: data = (target instruction) - (plan base). The firmware jumps to base + data. | absolute instruction number in the memory |
| 2 | **WAIT with data 0xFFFF_FFFF_FFFF means wait for the next RF0.** | treated as a 107 s wait (plan appeared stuck) |
| 3 | **A tick-count WAIT directly before a wait-for-RF0 is a minimum**: marker on the first RF0 at least that long after the previous marker. | the tick count was discarded |
| 4 | A GOTO re-chooses the plan like REPEAT does: if the on-spill state changed, the other plan starts at its first line. | GOTO never left the plan |
| 5 | Instructions are fetched at most up to the next MARKER, and no further until that marker is sent. Nothing of the old plan is queued behind a waiting marker. | about 32 fetched ahead (8 events of the old plan played out after a switch) |
| 6 | **The on-spill bit (event mode bit 32) is forced to 1 for every Heartbeat from the On-Spill plan**, ORed with the bit the run plan sets. | only what the run plan set |
| 7 | **The event duration in the CFO event record is the exact number of 25 ns ticks from marker to marker.** | one tick short (WAIT 4000 was recorded as 3999) |

Consequences for software:

- **A plan is position independent.** The same instruction words work at any base; nothing is recompiled when a base moves.
- **Software that wrote absolute GOTO targets for a plan at a non-zero base must subtract the base.** A plan at base 0 is unaffected.
- A plan built with these rules does not behave the same on older firmware (rules 1 to 3).
- Software that adds 1 to the recorded event duration to compensate must stop doing so (rule 7).
- Software does not need to set the on-spill bit in the On-Spill plan, and cannot clear it there (rule 6).
- The firmware does not know a plan's length. A GOTO target beyond the end of the plan lands in whatever follows it in memory, so the compiler must keep targets inside the plan.

## Short answer

Yes: set the memory write address first, then write the instructions. Each instruction is 64 bits and
goes in as two 32-bit writes, **low half first, then high half**. The address counts up by itself after
every write, so one address write is enough for a whole plan.

## Registers used

| Address | Name | Use |
|---|---|---|
| 0x9148 | ENABLEBEAMONMODE | bit 0 = enable the On-Spill plan |
| 0x914C | ENABLEBEAMOFFMODE | bit 0 = enable the Off-Spill plan |
| 0x930C | BEAMONBASE | where the On-Spill plan starts, **in instructions** |
| 0x9310 | BEAMOFFBASE | where the Off-Spill plan starts, **in instructions** |
| 0x9314 | RUNPLAN_BRAM_ADDRESS | memory write/read pointer, **in 32-bit words** |
| 0x9318 | RUNPLAN_BRAM_DATA | write: store one 32-bit word, pointer + 1. Read: return one word, pointer + 1 |
| 0x9100 | RAWDATA_CONTROL | bit 31 = Soft Reset (self-clearing), bit 0 = Hard Reset (self-clearing; puts registers back to defaults) |

## Enabling the plans (nothing runs until this is done)

Loading a plan does not start it. Each plan has its own enable register, and both are 0 after a Hard Reset.

| To run | Write |
|---|---|
| Off-Spill plan | 1 to **0x914C** (ENABLEBEAMOFFMODE, bit 0) |
| On-Spill plan | 1 to **0x9148** (ENABLEBEAMONMODE, bit 0) |
| RF0 input, needed for the On-Spill plan to ever be chosen | nothing to write: the RF0 input is always active. Bit 2 of 0x9100 ("RF0 input enable") is stored but not connected to anything in the current firmware (corrected 2026-11-24) |

- **Normal operation needs both enables set.** The firmware then switches between the two plans by itself.
- Off-Spill only: that plan runs all the time, also during spills. On-Spill only: nothing is sent between spills.
- Both registers read back the bit that was written. Writing 0 stops the plan at its next REPEAT or GOTO.

## Memory layout

- One memory holds both plans: 1024 instructions of 64 bits = 2048 words of 32 bits.
- Instruction number `N` occupies word `2N` (bits 31:0) and word `2N+1` (bits 63:32).
- **Two different units.** BEAMONBASE / BEAMOFFBASE count instructions (GOTO targets too, from the plan's base).
  RUNPLAN_BRAM_ADDRESS counts 32-bit words. A plan based at instruction 256 is written starting at
  word 512 (0x200).
- Both base registers are 0 after a Hard Reset, so the two plans overlap until you set them.
  Keep the plans apart, for example On-Spill at 0 and Off-Spill at 256 or 512.

## Sequence of writes

1. **Stop processing.** Write 0 to 0x9148 and 0 to 0x914C. Do not rewrite a plan while it is running:
   the firmware could read an instruction with only one half updated.
   A running plan stops at its next REPEAT or GOTO, not instantly.
2. **Write the On-Spill plan.**
   1. Write `2 x on_base` to 0x9314.
   2. For each instruction in order: write bits 31:0 to 0x9318, then bits 63:32 to 0x9318.
3. **Write the Off-Spill plan.** Write `2 x off_base` to 0x9314, then the instructions the same way.
4. **Set the bases.** Write `on_base` to 0x930C and `off_base` to 0x9310.
5. **Optional read-back.** Write the word address to 0x9314, then read 0x9318 once per word and compare.
6. **Optional Soft Reset** (0x9100 bit 31) for a clean start: event tag back to 0, counters cleared,
   any old queued instructions dropped. It resets other CFO logic as well, and it does not clear the
   two enable bits, so do it while both are still 0.
   It is required after a plan has stopped at END or with a run plan error.
7. **Enable.** Write 1 to 0x914C and/or 0x9148. Processing starts as soon as an enable is set.
   The RF0 input needs no enable (0x9100 bit 2 is not used by the current firmware).

## Instruction format

64 bits: `opcode[63:56]`, `unused[55:48]` (write 0), `data[47:0]`.

| Opcode | Name | Data field |
|---|---|---|
| 0x00 | NOOP / LABEL | ignored |
| 0x01 | HEARTBEAT | 48-bit event mode (bit 32 = on-spill bit, see below). All ones = keep the current mode (as built by AND/OR) |
| 0x02 | DATA_REQUEST | ignored |
| 0x03 | SET_TAG | 48-bit event window tag |
| 0x04 | INC_TAG | ignored; the tag always goes up by 1 |
| 0x05 | WAIT | bits 31:0 = 25 ns ticks from the previous marker to the next one. All 48 bits ones = wait for the next RF0 (see below) |
| 0x06 | LOOP | bits 31:0 = number of passes (not 0) |
| 0x07 | DO_LOOP | bits 28:0 = (address of this DO_LOOP) - (address of its LOOP), in instructions |
| 0x08 | REPEAT | ignored. Restart from the first line of whichever plan should run now |
| 0x09 | END | ignored. Stop for good; only a Soft Reset restarts processing (clearing and setting the enables does not) |
| 0x0A | MARKER | ignored. Send the Event Window Marker |
| 0x0B | GOTO | bits 28:0 = instruction to continue at (the line after the label), **counted from the plan's base** |
| 0x66 | AND_MODE | 48-bit mask ANDed into the event mode |
| 0x67 | OR_MODE | 48-bit mask ORed into the event mode |
| 0x6F | OR_MODE_SINGLE_SHOT | as OR_MODE, then the firmware zeroes this data in memory so it acts once |

Rules the firmware applies:

- A normal event is `WAIT, HEARTBEAT, MARKER, INC_TAG`. The marker is held until at least 1.3 us after
  its Heartbeat Packet, so events cannot be closer than about 1.40 us (1.35 us before the fetch rule 5 change).
- A WAIT too short for the firmware to meet raises a run plan error and processing stops until a reset.
  This error is not readable in any register today; the symptom is that heartbeat counts stop rising.
- Every plan must end in REPEAT, GOTO or END. Unwritten memory reads as NOOP and would be run through.
- All addresses inside a plan are relative (GOTO from the plan base, DO_LOOP back from itself, REPEAT to the base),
  so a plan can be loaded at any base unchanged. LOOPs nest up to 15 deep.

## Event mode and the on-spill bit

The 48-bit data of a HEARTBEAT instruction is the event mode sent in the Heartbeat Packet.

**On-spill bit = (the Heartbeat comes from the On-Spill plan) OR (bit 32 as the run plan set it).**

| Heartbeat comes from | On-spill bit sent |
|---|---|
| On-Spill plan | always 1, whatever the plan wrote |
| Off-Spill plan | 0, unless the plan sets bit 32 itself (allowed, e.g. for development) |

- The bit follows the plan being run, not the live RF0 state: the 2 On-Spill events after the last RF0
  of a spill are sent with the bit set, and the Off-Spill events at the start of a spill with it clear.
- AND_MODE / OR_MODE act on the run plan's own value of bit 32. The forced 1 does not carry over into
  the Off-Spill plan, and an AND mask cannot clear the bit for an On-Spill Heartbeat.
- The monitor registers 0x931C / 0x9320 show the event mode with the forced bit applied.

| Event mode bits | Meaning |
|---|---|
| 31:0 | event mode word |
| 32 | on-spill bit, see above |
| 39:33 | event mode flags. Bits 33 and 34 are overwritten by the firmware's subrun indicators when the subrun registers (0x9154, 0x9158) are non-zero |
| 47:40 | RF marker TDC prediction; sent only when the on-spill bit is 1, otherwise sent as 0 |

- In the 64-bit instruction, event mode bit 32 is bit 0 of the high 32-bit half: `0x0100_0001_xxxx_xxxx`
  sets it (only useful in an Off-Spill plan), `0x0100_0000_xxxx_xxxx` leaves it to the firmware (the normal case).
- Simulated: bit 32 of the event mode leaving the run plan logic, on every Heartbeat of every test.
  Not simulated: the packet-building code that places it in the Heartbeat Packet (`RingController.v`,
  `ListProcessor.v`), which is unchanged.

## Wait for RF0 (WAIT with data 0xFFFF_FFFF_FFFF)

- The next MARKER goes out on the first RF0 that arrives after its Heartbeat's 1.3 us guard time
  (15 to 35 ns after the RF0 edge in simulation).
- **Minimum time.** If a tick-count WAIT is still pending when the wait-for-RF0 is read (no MARKER in
  between), it is kept as a minimum: the MARKER goes on the first RF0 that arrives at least that many
  ticks after the previous marker. The minimum is used once and cleared by that MARKER.
- If the spill ends while waiting, the marker is released at once, so the plan cannot hang.
- New in this branch and proven in simulation only. The all-ones encoding was taken from the 2023
  compiler's `WAIT NEXT` and **must be confirmed against the compiler in use**.

## How the two plans are chosen

| Enables | Off spill | On spill |
|---|---|---|
| Off-Spill only | Off-Spill plan | Off-Spill plan |
| On-Spill only | halted | On-Spill plan |
| both | Off-Spill plan | On-Spill plan |

- On spill means RF0 pulses are arriving: the flag sets after about 22 pulses and clears 2 us after the last.
- The choice is made only at start, at REPEAT and at GOTO, and a new plan always starts at its first line.
  A plan with a long LOOP therefore keeps running until it reaches its REPEAT or GOTO.
- An event whose Heartbeat has already been sent is always completed with its marker. Apart from that one
  event in progress, nothing of the old plan is carried out after a change.

## Default plans (100 us Off-Spill window, one event per RF0 On-Spill)

These are the two plans proven in simulation (scenario 14 of `OnOffSpillSwitch_tb`).
On-Spill at instruction 0, Off-Spill at instruction 256.
Neither plan sets the on-spill bit (event mode bit 32): the firmware sets it for every Heartbeat of the
On-Spill plan. This is exactly what was simulated.
The low bytes 0xA1 (On-Spill) and 0xB0 (Off-Spill) are test bench placeholders: **put the real event mode
value in bits 31:0 of the two HEARTBEAT lines.**

Off-Spill plan (base 256):

| Addr | Instruction (64-bit) | Meaning |
|---|---|---|
| 256 | 0x0500000000000FA0 | WAIT 4000 ticks = 100 us since the previous marker |
| 257 | 0x01000000000000B0 | HEARTBEAT, Off-Spill event mode |
| 258 | 0x0A00000000000000 | MARKER |
| 259 | 0x0400000000000001 | INC_TAG |
| 260 | 0x0800000000000000 | REPEAT |

On-Spill plan (base 0):

| Addr | Instruction (64-bit) | Meaning |
|---|---|---|
| 0 | 0x0500000000000FA0 | WAIT 4000 ticks: minimum 100 us for the last Off-Spill window (runs once per spill) |
| 1 | 0x0000000000000000 | LABEL |
| 2 | 0x0500FFFFFFFFFFFF | WAIT for RF0 |
| 3 | 0x01000000000000A1 | HEARTBEAT, On-Spill event mode (on-spill bit added by the firmware) |
| 4 | 0x0A00000000000000 | MARKER |
| 5 | 0x0400000000000001 | INC_TAG |
| 6 | 0x0B00000000000002 | GOTO 2 = base + 2, the line after the LABEL |

The On-Spill plan must loop with LABEL/GOTO, not REPEAT, so the opening WAIT runs only once per spill.
The GOTO data stays 2 wherever the plan is loaded.

### What these plans do (measured in simulation, two spills, identical in both)

| Property | Result |
|---|---|
| Off-Spill event windows | all exactly 100.000 us, Heartbeat marked Off-Spill |
| Last Off-Spill window before the spill | 100.075 and 101.375 us in the two spills: at least 100 us, ended by the next RF0 (so up to one RF0 period more) |
| On-Spill events | one per RF0 (1.7 us), marker 15 to 35 ns after the RF0, Heartbeat marked On-Spill |
| Last On-Spill window (to the first Off-Spill marker) | exactly 100.000 us |
| Every marker | carries the mode and tag of its own Heartbeat; tags never repeat or skip |
| On-spill bit | 1 on all On-Spill plan Heartbeats, 0 on all Off-Spill plan Heartbeats (1495 checked) |
| Entering the spill | 1 more Off-Spill marker after the on-spill flag rises; first On-Spill marker about 165 us after it |
| Leaving the spill | exactly 1 On-Spill-marked event after the last RF0-locked one, with no RF0 behind it (see below) |
| Heartbeat timing in the spill | sent 110 ns after the previous marker; 275 to 295 ns to spare before the next RF0 at a 1695 ns RF0 period |
| Recorded event duration | equals the marker-to-marker tick count for every event (1607 checked) |

End of every spill, as recorded (ticks of 25 ns):

| Event | Recorded duration | Marked |
|---|---|---|
| normal On-Spill events | 67 or 68 (1.675 / 1.700 us) | On-Spill |
| last event with an RF0 behind it | 81 (2.025 us) | On-Spill |
| **one event with no RF0 behind it** | **4000 (100 us)** | **On-Spill** |
| first Off-Spill event | 4000 (100 us) | Off-Spill |

The one beam-less On-Spill event per spill cannot be avoided: its Heartbeat is sent as On-Spill while the
firmware is still waiting for an RF0, and it only learns 2 us later that none is coming. It then sends the
marker at once and switches to the Off-Spill plan. **Software can recognise it as the On-Spill event with the
Off-Spill duration, immediately before the first Off-Spill event.**

### Register writes for the default plans

| Step | Register | Value | Meaning |
|---|---|---|---|
| 1 | 0x9148 | 0x00000000 | On-Spill off |
| 2 | 0x914C | 0x00000000 | Off-Spill off |
| 3 | 0x9314 | 0x00000000 | pointer to word 0 (instruction 0) |
| 4 | 0x9318 | 0x00000FA0, 0x05000000 | WAIT 100 us (minimum) |
| 5 | 0x9318 | 0x00000000, 0x00000000 | LABEL |
| 6 | 0x9318 | 0xFFFFFFFF, 0x0500FFFF | WAIT for RF0 |
| 7 | 0x9318 | 0x000000A1, 0x01000000 | HEARTBEAT, On-Spill event mode (on-spill bit added by the firmware) |
| 8 | 0x9318 | 0x00000000, 0x0A000000 | MARKER |
| 9 | 0x9318 | 0x00000001, 0x04000000 | INC_TAG |
| 10 | 0x9318 | 0x00000002, 0x0B000000 | GOTO 2 (relative to the On-Spill base) |
| 11 | 0x9314 | 0x00000200 | pointer to word 512 (instruction 256) |
| 12 | 0x9318 | 0x00000FA0, 0x05000000 | WAIT 100 us |
| 13 | 0x9318 | 0x000000B0, 0x01000000 | HEARTBEAT, Off-Spill event mode |
| 14 | 0x9318 | 0x00000000, 0x0A000000 | MARKER |
| 15 | 0x9318 | 0x00000001, 0x04000000 | INC_TAG |
| 16 | 0x9318 | 0x00000000, 0x08000000 | REPEAT |
| 17 | 0x930C | 0x00000000 | On-Spill base = instruction 0 |
| 18 | 0x9310 | 0x00000100 | Off-Spill base = instruction 256 |
| 19 | 0x9100 | bit 31 (keep the other bits) | optional Soft Reset |
| 20 | 0x914C | 0x00000001 | Off-Spill on |
| 21 | 0x9148 | 0x00000001 | On-Spill on |

Steps 4 to 10 and 12 to 16 are two writes each to 0x9318, in the order shown (low half, then high half).

Counters to watch afterwards: 0x9240 heartbeats, 0x9244 heartbeats on spill, 0x9248 heartbeats off spill,
0x9260 markers, 0x9200 RF0 received; 0x9324 / 0x9328 current event tag, 0x931C / 0x9320 current event mode.

## How this was checked

- **Simulated** (`OnOffSpillSwitch_tb`, 12 scenarios pass, 0 fail, 4 info-only): opcodes, plan choice, switching, wait for RF0 with
  and without a minimum, the default plans, relative GOTO at base 0 and at base 256, the forced on-spill bit, the recorded event
  duration (the event record module is in the bench), the 64-bit instruction layout and the instruction-unit base addresses.
  Logs and the simulated sources are in `ai_sim_evidence/` (`changes_AB`, `onspill_bit`, `relative_goto`, `default_plans`).
- **From reading the firmware, not simulated here**: the register write path (0x9314 / 0x9318 pointer
  and auto-increment, low word first), read-back through 0x9318, and the enable / Soft Reset behaviour.
  The older bench `ListProcessorWithRingController_TB.v` loads plans the same way (address strobe, then
  low word, high word).
- **Not checked**: the current software compiler's output format, and anything on hardware.

Sources: `src/register_map.v`, `src/RunPlanInterface.v`, `src/ListFeeder_BRAM.v`,
`src/mu2e_runPlan_protocol_include.vh` under `YearMonDD_HH_VV.V/CFO/`.
