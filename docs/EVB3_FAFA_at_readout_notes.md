# RESOLVED: switch forwards nothing — EVB_OnlyOneIdlePacket unconnected (2026-08-21)

**Symptom:** after the MAC-symmetry fix, the switch learned the correct MACs
(0000.9900.0080/0081), counted every input frame (136B, FCS good), had the dest
in its FDB — and still forwarded ZERO frames. Reverting the DTCs to the old
EVB2-based bitfile (0xd5041988, branch rrivera/sourceCountsBRAM3) made the
switch forward (output = input/2; the old round-robin included self, and
self-addressed frames are correctly not forwarded out their ingress port).

**Diagnosis chain:** old-vs-new frames are identical in addressing (old EVB2
'h04/'h05 == restored form, ILA-verified on the wire) and identical in size
(136.0 B/pkt both versions). The difference is the TRAFFIC PATTERN: old EVB2
transmitted only when it had DMA data (no info-packet path at all); EVB3 added
the space-exchange info packets — and the new build streamed them BACK-TO-BACK
every ~240ns (~4.2 Mpps, ~4.5 Gbps sustained unicast).

**Root cause:** `EVB_OnlyOneIdlePacket` was never connected in the DTC.v
EVB3/EVB2 instantiations. An unconnected input synthesizes to 0, making
`(!EVB_OnlyOneIdlePacket || !txInfoPacketSent)` always true -> info packets
free-run. Sim ties the port to 1'b1 (EVB_DTCSim.sv:410/522), so simulation
never showed the flood. The switch (restrictively configured: unknown-unicast
flooding off) rate-polices the flood to zero forwarded.

**Fix:** DTC.v — `.EVB_OnlyOneIdlePacket(1'b1)` added to both EVB3 and EVB2
instantiations (matches sim and design intent: one info packet per
destination-window opportunity).

**REQUIRED SOFTWARE SETTING:** EVB dead time (0x9158[31:16]) must be NONZERO
(sim uses 0x20). With OnlyOneIdlePacket=1, the info packet re-arms only on a
destination CHANGE; with 2 nodes and dead time 0 the round-robin wraps
0x81->0x81 (never "new") and only ONE info packet would ever be sent per EWM,
starving the space exchange. Dead time parks the dest at 0xFF each window
boundary, so the return re-arms exactly one info packet per window
(~<=450kpps total — the rate class the switch forwards happily).
Suggested: write 0x9158 = 0x0020_8002 for the 2-node bench.

**Expected after rebuild + dead-time setting:** switch output counters track
input counters; DTC RxCount/RxIdleCount advance; evb_rx_ila rxctrl==0 trigger
fires; per-window info-packet cadence (~2.2us+) instead of 240ns flood.

---

# Change Notes: 250MHz timing fixes in EVB2_buffer_manager arb/scan (2026-08-21)

Three failing path groups on userclk1 (4ns), worst -0.107ns. Fixed two in
[EVB2_buffer_manager.v](YearMonDD_HH_VV.V/Aldec/design1/src/EVB2_buffer_manager.v):

**A. rotated_empty_r -> scan_result_r_cntmux (-0.107):** the 25-bit priority
scan + add + conditional-subtract was one 8-level cone. Added stage-1a register
(`scan_offset_r`/`scan_found_enc_r`) between the encoder and the add/wrap; the
scan pipeline gains one cycle, covered by widening `arb_settle` 3 -> 4 (grants
still only fire on an exact, fully-refreshed snapshot after entering CHUNK_ARB).

**B. AXIMux FIFO dout -> rd_source replicas (-0.028):** the grant cone computed
`local_peek_wc` (adder off `local_in_data`), the zero/last checks, and the
signed fit compare combinationally from the FIFO output across the hierarchy.
Added registered qualifiers (`local_peek_wc_r`, `local_last_r`,
`local_wczero_r`) guarded by `local_flags_fresh`, which blanks the one stale
cycle after any local consumption or a valid rise — FWFT head data is stable
otherwise, so drops/grants act only on flags that exactly reflect the current
head word. `chunk_wc` now loads `local_peek_wc_r` (equal at grant time by the
same guard). Filler drops take one extra cycle each; grants unchanged in
behavior (arb_settle already dominates their latency).

**C. AXIMuxFromRingsData packetcount -> s_axis_tdata CE (-0.026):** NOT
changed — the cone runs through the long-standing ROC mux FSM
(ROC_empty_packet_count -> ROC_event_ready); registering those flags without a
full FSM audit risks granting a just-emptied FIFO. Recommend retrying with
phys_opt/another strategy first (magnitude is placement-noise scale); if it
persists across builds, do an audited fix.

**Verify:** re-run HardwareEventBuilding_tb — arbitration gains 1 cycle of
scan latency and 1 cycle per filler drop; all counters/SEQMON/stress checks
must stay green. Sim exercises grants, drops, local+remote chunks, and DMA
close paths.

**Round 2 (same day) — clk_pll_i (200MHz DDR ui_clk), worst -0.044ns:**
`EVB3_tx_subevent_fifo_mgr` write_ptr -> write_addr_reg CE. The raw cone was
a PTR_W subtract + threshold compare per channel, a 25:1 mux by write_addr,
and the W_IDLE gate — 8 levels. Fix: registered `almost_full_r` vector
(on ddr_ui_clk — NOTE the module's `clk` port is sim-only); W_IDLE and
`almost_full_out` both use it. One cycle of staleness admits at most one
extra burst past the threshold (flag only consulted in W_IDLE, refreshes
during the burst) — trivially covered by ALMOST_FULL_THRESHOLD's 100-burst
margin. Paths 104/105 in the report are MIG-internal with positive slack.

---

# Change Notes: EVB frame MAC symmetry for commercial switch (2026-08-21)

**Problem:** with EVB TX/RX proven healthy on both DTCs, no frames were ever
received (rxctrl stuck 1, only line idles 0x1E00_0000_0000_0000 arriving).
The commercial switch's counters showed ~1.27e12 input frames per DTC port at
exactly 136 B/frame (our idle packets) and ~0 forwarded. Its dynamic MAC table
had learned each DTC's source MAC — 0000.9980.8080 (DTC_0) / 0000.9981.8181
(DTC_1) — which also proves the FCS/CRC is GOOD (switches never learn from
CRC-failed frames).

**Root cause: a 2026-07-07 regression (commit 3d4306b8).** EVB2's original
header construction was symmetric — MAC = {00,00,partition,00,00,node} for
both dest and source (EVB2.v:1177-1197). The EVB3 copy replaced the 00 bytes
with self DTCID/IP bytes, producing dest = {DTCID_self, IP_self, part, dest,
DTCID_self, dest} = 80:80:99:81:80:81 vs the peer's learned source MAC
00:00:99:81:81:81. A commercial switch forwards unicast by exact 6-byte
lookup against learned source MACs; every frame was unknown unicast and
dropped (flooding disabled on the switch). No 0x9154/0x9158 register value
can make the regressed form symmetric (self fields would need to equal peer
fields) — software init was verified correct. Sim never catches it: the TB
wires DTCs directly and the RX parser only reads single bytes.

**Fix (EVB3.v 'h04/'h05 header words): restored EVB2's exact construction.**
- dest MAC   = 00 : 00 : 0x9154[15:8] (partition) : 00 : 00 : dest node
- source MAC = 00 : 00 : 0x9154[15:8] (partition) : 00 : 00 : 0x9154[7:0]
The upper 5 bytes are the event-building group "subnet" (keyed by partition
ID): all DTCs of a group share the prefix, so distinct groups occupy disjoint
MAC spaces on one switch. The dest node byte comes from the round-robin over
0x9158[15:8] base .. base+numNodes-1.

DTC ID (0x9154[31:24]) does NOT belong in the MAC: it is the DATA-path
identity, stamped into DTC subevent headers as "Data Source DTC ID"
(AXIMuxFromRingsData.v:499,744) and checked by PatternChecker(PCI). Sim
drives DTCID = 0xa5+SELF_DTC_IP, deliberately != the EVB address byte — the
regression had conflated the two identities.

RX parse positions/values unchanged (dest = word4 byte5, source = word5
byte3); none of the modified bytes are parsed by RX (verified).

**Verify:** sim HardwareEventBuilding_tb (frames still decode: RxCnt/DropCnt/
SEQMON/stress all green); hardware: switch FDB learns 0000.9900.0080/0081
(old entries age out), switch output counters on eth-0-31/32 start tracking
input counters, DTC RxCount/RxIdleCount advance, rxctrl=0 frames visible on
evb_rx_ila, rx_illegal_destination stays 0. No switch config change needed.

---

# RESOLVED: "tx_fsm stuck at 0x00 / offset_dest_mac_reg=0x1F" root cause (2026-08-20)

**Root cause: SoftReset resets the 10G EVB GT, killing txgbeclk for the GT
reset/lock time (~50-400us), and every Event Window Marker arriving in that
window is lost at the marker CDC (a dead flop samples nothing).**

Chain, all source-confirmed:
1. `reset_feeder = user_reset || ~areset_n || SoftReset` -> `reset_onuserclk_fanout`
   (EVB3.v ~390) -> `EVB10GBELink_inst.resetint(reset_onuserclk_fanout[3])`
   (EVB3.v ~3244, `ifndef EVBSimMode`) -> GT reset -> txusrclk2 (= txgbeclk) stops.
2. Marker CDC (stretch on CFOtxusrclk -> 2-flop sync on txgbeclk) has no reset and
   is correct — a ~19ns stretched pulse cannot be missed by a RUNNING 156MHz flop.
   It is only lost when txgbeclk is not toggling.
3. evb_tx_ila is on txgbeclk too, so during the outage its trigger cannot fire —
   "trigger never fired" was itself the symptom, while cfo_interface_ila (own
   clock alive) showed the same EWMs forwarded perfectly (EventStart_sig pulses).
4. The software marker macro issues SoftReset then emits EWMs at ~2.2us cadence:
   N=20 spans 44us (all inside outage, all eaten -> destination stays parked at
   0xFF -> offset_dest_mac_reg reads 0x1F = 0xFF - EVBBaseNode(0x80));
   N=200 spans 440us (tail lands post-recovery -> validates, EVB runs).
5. Sim never reproduces it: EVBSimMode ties txgbeclk = refclkin (free-running).

Not the cause (each individually verified): EVB config regs (0x9158=0x8002 ok),
EVB_DelayOffset (0x915C[7:0]=0), LinkInterfaceRxEnable (40MHz markers cross the
same gate), RTF alignment (saturated in time; CFOInterface launch healthy at
2 EWMs), the stats-BRAM clear sweep (holds tx_fsm/txCRCrst only — the sweep
restarts while reset/rx-disable persist, then takes a final 512 rxgbeclk; it
never touches the marker path or destination validation), the stretch depth
(sets all 3 bits; [2] is high through the full 111->110->100 drain).

**Mechanism confirmed in EVB10GBELink.v:** `resetint` drives
`.gt0_gttxreset_in` (line 401, direct GT channel TX reset -> TXOUTCLK/
txusrclk2 stops), `.soft_reset_tx_in`/`.soft_reset_rx_in` (306-307, full GT
wizard startup FSMs), and `gt0_gtrxreset_i` + BlockSync/Descrambler resets
(294-296). QPLL reset is commented out (317) -- consistent with the observed
~50-400us outage (reset-done sequence, not a PLL relock).

**FIX (2026-08-20): SoftReset removed from the SERDES reset.** This violated
the design's reset semantics -- SoftReset = counters + state machines ONLY;
HardReset = factory defaults (clocks, SERDES IPs, register settings). EVB3.v
now has a parallel `reset_serdes` feeder/stretch (same 32-stage idiom, next to
the original at ~line 400) driven by `user_reset || ~axi_str_s2c0_areset_n ||
~axi_str_c2s0_areset_n` only, and `EVB10GBELink_inst.resetint` is wired to it.
Everything else (dest park, TX/RX FSMs, stats-BRAM clear sweep, counters)
stays on the SoftReset-inclusive fanout, which is SoftReset's intended scope.
With txgbeclk alive through SoftReset, the first EWM after the ~128ns reset
tail validates the destination -- the bench marker-burst race is gone.
Sim unaffected (EVBSimMode has no 10G link; txgbeclk = refclkin there).

SoftReset -> then send EWM ordering remains good practice (the destination
still re-parks at 0xFF on SoftReset by design and waits for the next marker).

Decision: keep the existing stretch CDC (works whenever txgbeclk runs); no
toggle-CDC rewrite.

**Hardware verification (the test that previously failed):** arm evb_tx_ila
trigger on probe1 (or probe0[49] raw port pin) rising, run the 2-EWM macro
(SoftReset included). Expect: trigger fires on marker #1; capture shows
probe0[49] -> [48]/[47] stretch -> probe1 sync -> dest [38:31] 0xFF->0x81;
probe0[28] shows only the short reset tail; txdatavalidp keeps toggling.

**Follow-up (separate decision):** audit whether other SERDES IPs (ROC links,
CFO link SERDES, RingSERDES) also receive SoftReset-derived resets -- the
same semantics rule applies chip-wide.

---

# Change Notes: evb_tx_ila probe0 reset/marker-gate debug bits (2026-08-20)

HW debug of "tx_fsm stuck at 0x00, offset_dest_mac_reg=0x1F" (= DestinationDTC_ip
parked at 0xFF awaiting its first Event Window Marker). Suspicion: the software
marker-send macro issues a Soft Reset first, and the EWMs are counted by the CFO
interface (0xA418) but arrive inside the EVB reset shadow, so the destination
never validates. 40MHz markers were PROVEN to cross the marker CDC (evb_tx_ila
capture), so the CDC/offset/clock infrastructure is good.

Repurposed the '0-padding of `evb_tx_ila` probe0 (existing fields keep their bit
positions; no size/BRAM change):

| probe0 bits | signal | meaning |
|---|---|---|
| [50] | stretched_CFOClock40MHzMarker_any | 40MHz stretch OR (comparison reference) |
| [49] | CFOEventWindowMarker | raw EWM at the EVB3 port pin (CFOtxusrclk, ~2 cycles) |
| [48] | stretched_CFOEventWindowMarker_any | OR of all 3 stretch bits — what the txgbeclk sync now samples |
| [47] | stretched_CFOEventWindowMarker[2] | top stretch bit |
| [46:39] | EVBNumNodes | must be > 1 for the first EWM to validate the dest |
| [38:31] | DestinationDTC_ip | live dest; 0xFF = parked awaiting first EWM |
| [30] | txenable_ontxgbeclk | tx_fsm held at 0 while low |
| [29] | reset_txgbeclk_count_nonzero | tx_fsm frozen during stats-BRAM clear sweep |
| [28] | reset_txgbeclk | parks dest at 0xFF while high |
| [27:20] | txDestinationDTC_ip | (unchanged position) |
| [19:0] | statsBRAM tx_we/tx_addr/sel_reg/wdata | (unchanged positions) |

Note (same date): the txgbeclk marker sync now samples the OR of all stretch
bits (`stretched_*_any`) instead of bit [2] alone — with the all-ones set
pattern the two are equivalent (bit [2] stays high through the 111->110->100
drain), but the OR states the stretch intent explicitly and bits [49:47] make
the whole port-pin -> stretch -> sync chain visible in one txgbeclk capture.
The marker-loss suspect list is thereby fully instrumented: if [49] pulses but
probe1 never does while the ILA is capturing, the crossing itself is at fault;
if [49] never pulses, the loss is upstream of the EVB3 port; if nothing
captures during the loss window, txgbeclk itself paused (10G GT reset).

Trigger recipes:
1. probe0[28] Falling edge, trigger position mid-window, then run the software
   marker macro: pre-trigger shows whether EWMs (probe1) pulse while reset=1
   (markers eaten by reset shadow), post-trigger shows dest behavior at release.
2. probe1 (CFOEventWindowMarker_txgbeclk) Rising edge: at the marker instant,
   read probe0[28] (reset), [46:39] (num nodes), [38:31] (dest). If reset=0,
   nodes=2, and dest stays 0xFF after the pulse, the gate logic itself is suspect.

---

# Change Notes: Stats BRAM readback — ROOT CAUSE FOUND + FIX (2026-08-20)

**Issue:** Software readback of per-DTC BRAM stats (via register `0x9160`) returned
garbage in hardware but worked in simulation (which only read during TX idle).

**Root cause (proven by testbench stress test — reads during active TX):**
The 2-stage `statsBRAM_sel_tx_addr_reg` capture gate protects only the sel *rising*
edge. On the *falling* edge, `statsBRAM_sel_tx_addr` drops via the every-cycle
default while `statsBRAM_tx_addr` (hardwired to `statsBRAM_addrb`, mux commented
out) still holds a TX address — it is only restored to the software address in FSM
states 0x00 and 0x04. Two cycles after sel falls the gate opens onto wrong-address
data and captures it into `EVBSourceStats_rdata`.

**The specific culprit path is the SPACE read (states 0x15/0x16/0x17):** unlike the
TXSEQ path (0x01/0x02/0x03, which re-assert sel every state), the SPACE wait states
asserted nothing — sel fell at end of 0x15, `tx_addr` kept the RXSEQ_AND_RXSPACE
address until the next pass through 0x00, and the gate opened mid-sequence. The
stress test caught exactly this: reading txIdle[1] (true value 1) returned 0x2002 —
the RXSEQ_AND_RXSPACE word (rx_seq=2) — at fsm=0x15 at an event-window boundary.
SPACE reads fire on every new-dest transition, so in hardware (continuous event
windows) software readback was corrupted at high probability; in the original sim
(reads only during long idle) it never was.

**Why the naive mux alone (the earlier reverted "fix") was insufficient:** simply
uncommenting the mux breaks two paths that silently relied on `addrb = tx_addr`
while sel=0:
1. **Idle-count writeback (state 0x0D):** sets `statsBRAM_tx_we` without asserting
   sel; the write is presented the *next* cycle. With the mux and sel=0, the
   TX_IDLE_COUNT increment would write to whatever address software selected.
2. **SPACE read under wait (state 0x17):** sel had already fallen at end of 0x15;
   with the mux, addrb flips to the software address at 0x16, so any delayed exit
   (`txCanReadSpaceAvailable_ontxclk` low) reads software-address data as available
   space — corrupting the event-building throttle. Likely why the mux was
   originally commented out.

**Fix applied (EVB3.v):**
1. Mux restored using the txgbeclk-registered copy (keeps the BRAM address path
   in-domain; CDC terminates at the existing `EVBSourceStats_raddr_reg`):
   ```verilog
   assign statsBRAM_addrb = statsBRAM_sel_tx_addr ? statsBRAM_tx_addr : EVBSourceStats_raddr_reg;
   ```
2. `statsBRAM_sel_tx_addr <= 1'b1` holds added in states 0x15, 0x16, 0x17 (SPACE
   read latency + wait loop) and 0x0D (so the idle-count write presented the next
   cycle steers addrb to tx_addr) — matching the existing 0x01/0x02/0x03 idiom.
3. Obsolete `statsBRAM_tx_addr <= EVBSourceStats_raddr` restores removed from
   states 0x00 and 0x04 (the mux now owns user-address takeover). Removing the
   0x04 restore also means a multi-packet revisit of TXFSM_STATE_HEADER presents
   its TXSEQ writeback at the TXSEQ slot instead of the software-selected slot.

**Capture-gate block (EVB3.v ~599-609) unchanged:** with the mux, its 2-stage depth
is exactly matched on both sel edges — first capture after sel falls lands one cycle
after software-address data reaches doutb; TX-address data is never captured.

**Invariant for future FSM edits:** any state that reads `statsBRAM_rdata` for its
own use, waits out BRAM read latency, or is the state *before* a
`statsBRAM_tx_we` write is presented, MUST assert `statsBRAM_sel_tx_addr <= 1'b1`.

**Multi-packet tx_seq bug — FIXED (same day):** multi-packet bursts
(0x09 -> 0x03 loop, only when a burst exceeds MAX_PACKET_BYTES = 1492, i.e.
~186 words/packet) read `statsBRAM_rdata` at TXFSM_STATE_HEADER without
re-issuing the TXSEQ read, so packets 2..N derived tx_seq/wdata from stale
doutb (software-address data). Pre-existing bug, independent of the mux.
Fix: 0x03 now discriminates on `txFirstPacket` (set at burst start in 0x00,
cleared at 0x05) — first packet increments from the BRAM read as before;
packets 2..N increment the in-register `tx_seq` locally, which is
authoritative for the rest of the burst (destination cannot change
mid-burst). The per-packet BRAM writeback is kept and lands at TXSEQ
correctly (sel asserted in 0x03; tx_addr persists across the loop).
Sim coverage added: MAX_PACKET_BYTES promoted to an EVB_DTCSim parameter
(default 1492) and overridden to 128 (16 words/packet) in
HardwareEventBuilding_TB.v, so ~25-word bursts split into 2 packets. A
SEQMON block in the TB samples every DTC0 packet header (first cycle of
tx_fsm=='h05) and checks seq = prev+1; final report at 39us requires
continuation packets > 0 (proves the path fired) and 0 seq errors.
Receiver cross-check: DropCnt in the stats dumps must stay 0.

**Verification (sim, user runs):** stress test expects 32/32 passed with no
NON-MONOTONIC; `have_data` TX windows still appear; TxIdleCnt still increments
(0x0D write lands correctly); `tx_available_space_in_words` sane after 0x17
(SPACE read intact). Hardware: `evb_stats_bram_ila` probes (addrb vs raddr, sel,
gate) directly show the mux steering.

---

# Change Notes: ILA probe additions for CFO marker + idle count debug (2026-08-20)

Replaced `'0`-tied probes across 5 EVB ILAs with diagnostic signals to debug
the CFO Event Window Marker CDC path and correlate idle counts with FSM state.

**Problem:** `DestinationDTC_ip` stuck at `0xFF` in hardware — round-robin never
starts because `CFOEventWindowMarker_txgbeclk` rising edge is never detected.
No ILA in the current bitfile had visibility into the CFO marker CDC path
(`CFOEventWindowMarker_txgbeclk` / `CFOEventWindowMarker_txgbeclk2` had been
replaced with `calib_done` / `axi_str_s2c0_areset_n` on the now-commented-out
`evb_ddr_ila`).

**Changes in EVB3.v:**

| ILA                     | Clock     | Probe | Was  | Now                                                  |
|-------------------------|-----------|-------|------|------------------------------------------------------|
| `evb_tx_ila`            | txgbeclk  | 1     | `'0` | `CFOEventWindowMarker_txgbeclk`                      |
| `evb_tx_ila`            | txgbeclk  | 3     | `'0` | `CFOEventWindowMarker_txgbeclk2`                     |
| `evb_tx_ila`            | txgbeclk  | 8     | `'0` | `CFOClock40MHzMarker_txgbeclk`                       |
| `evb_tx_ila`            | txgbeclk  | 25    | `'0` | `{txIdleCnt[0][7:0], txIdleCnt[1][2:0]}`            |
| `evb_tx_ila`            | txgbeclk  | 27    | `'0` | `CFOClock40MHzMarker_txgbeclk2`                      |
| `evb_ddr_tx_ila`        | txgbeclk  | 6     | `'0` | `{txIdleCnt[1][7:0], txIdleCnt[0][7:0], CFOmarker2, CFOmarker}` |
| `evb_ddr_tx_ila`        | txgbeclk  | 7     | `'0` | `CFOClock40MHzMarker_txgbeclk`                       |
| `evb_ddr_tx_ila`        | txgbeclk  | 8     | `'0` | `CFOClock40MHzMarker_txgbeclk2`                      |
| `evb_ddr_tx_ila`        | txgbeclk  | 9     | `'0` | `{1'b0, txSeq[0][1:0]}`                             |
| `evb_ddr_tx_ila`        | txgbeclk  | 10    | `'0` | `txdatavalidp`                                       |
| `evb_tx_idle_cnt_ila`   | txgbeclk  | 5     | `'0` | `CFOEventWindowMarker_txgbeclk`                      |
| `evb_tx_idle_cnt_ila`   | txgbeclk  | 6     | `'0` | `CFOClock40MHzMarker_txgbeclk`                       |
| `evb_rx_ila`            | rxgbeclk  | 5     | `'0` | `{rxIdleCnt[0][7:0], rxIdleCnt[1][2:0]}`            |
| `evb_rx_ila`            | rxgbeclk  | 25    | `'0` | `{rxCount[0][7:0], rxCount[1][2:0]}`                |
| `evb_rx_ila`            | rxgbeclk  | 26    | `'0` | `rx_packet_has_data`                                 |
| `evb_rx_ila`            | rxgbeclk  | 27    | `'0` | `CFOEventWindowMarker_rxgbeclk[0]`                   |
| `evb_rx_ila`            | rxgbeclk  | 28    | `'0` | `CFOEventWindowMarker_rxgbeclk[1]`                   |
| `evb_rx_idle_cnt_ila`   | rxgbeclk  | 5     | `'0` | `CFOEventWindowMarker_rxgbeclk[1]`                   |
| `evb_rx_idle_cnt_ila`   | rxgbeclk  | 6     | `'0` | `reset_rxgbeclk`                                     |

**Debug plan:** Trigger `evb_tx_ila` on probe1 (CFOEventWindowMarker_txgbeclk)
rising edge. If it never fires, the marker isn't crossing from `CFOtxusrclk` —
check `loopbackCalibratedOffset` via register `0x915C[7:0]` and verify
`EventWindowStart` is pulsing from the CFO interface.

---

# Change Notes: TX idle count BRAM read latency fix (2026-08-18)

**Bug:** TX idle count R-M-W in EVB3.v had only 1 BRAM wait state, but
EVBSourceStatsBRAM has 2-cycle read latency. The FSM at state 0x0C read
stale `statsBRAM_rdata` from the previous address instead of the
TX_IDLE_COUNT value. Both DTC instances were affected, but DTC_0 happened to
get the right answer from residual data on the bus.

Verified in simulation: `addrb` updates to 0xE0 at state 0x0B, but `rdata`
doesn't reflect that address until 2 cycles later — state 0x0C was 1 cycle
too early.

All other TX R-M-W paths already had 2 wait states:
- TX SEQ: states 0x01 ("1of2-clock latency") and 0x02 ("2of2-clock latency")
- TX SPACE: states 0x15 and 0x16 ("wait for read latency")

**Fix (EVB3.v lines 1740-1755):** Added second wait state at 0x0C, moved
write-back to 0x0D. State 0x0D was previously unused (fell into default/error
case). Auto-advance `tx_fsm <= tx_fsm + 1` handles the new state naturally.

---

# Change Notes: FAFA chunk headers at buffer-manager readout (2026-08-12)

Architecture change per Ryan's direction: FAFA is a **header** before each
chunk, with the size known **in advance** (never counted); all PCIe-bound data
(local + remote) flows through the buffer manager, which owns m_axis and does
all FAFA framing; chunks are **bundled** into DMA transfers like
`AXIMuxFromRingsData_bundled.v` with a 200 µs close timeout.

Protocol spec (software-facing): [EVB3_DMA_FAFA_protocol.md](EVB3_DMA_FAFA_protocol.md)
Firmware internals doc (updated): [EVB3_counters_and_protocol.md](EVB3_counters_and_protocol.md)

## PREREQUISITE — FIFO IP regeneration (Ryan, in Vivado)

`EVB2_SourceRxDataFIFO` must be regenerated with **"Read Data Count"** enabled.
The new buffer manager connects `.rd_data_count(buffer_rd_cnt[i])` (expected
width `[W:0]` = [5:0], same as `wr_data_count`). **Compile/elaboration will
fail until the IP has this port.** Also update the Aldec-side IP sim model if
it is compiled separately.

Why rd_data_count and not the existing wr_data_count: it is native to the read
clock (user_clk) — no risk of sampling a torn value from the rxgbeclk binary
counter mid-packet-write — and it never over-reports, so a snapshot guarantees
that many words are actually readable (no mid-chunk empty stalls).

## Files changed

### EVB2_buffer_manager.v (major rewrite of the read side)

- New ports: `local_in_data/valid/last/ready` (self-destined ROC stream,
  user_clk), `SelfDTC_ip`, `EVBBaseNode`, `DMA_max_size` (bytes),
  `data_out_is_local`, `data_out_chunk_last`. `data_out_is_header` is now
  generated by the chunk FSM (no longer FIFO bit 64).
- Old drain-until-empty reader FSM replaced by a chunk-servicing FSM
  (`CHUNK_ARB → CHUNK_FAFA → CHUNK_DATA → CHUNK_ARB`, plus `CHUNK_CLOSE`):
  - Remote grant: `chunk_wc` = `rd_data_count[src]` snapshot, `chunk_src` =
    `EVBBaseNode + src`; drains exactly `chunk_wc` words.
  - Local grant: `chunk_wc` peeked from the first word's byte count
    (`local_in_data[15:3]`) before acceptance; `chunk_src` = `SelfDTC_ip`.
  - Local/remote alternate priority on simultaneous requests
    (`last_was_local`); remote sources keep the existing round-robin scan,
    with `rd_source` advanced past a served source at chunk end.
  - Boundary words with tlast (generator DMA-close fillers) and zero-byte-count
    words are consumed and dropped (not stored / counted / FAFA'd).
  - Bundling (like `_bundled`): chunks stack into one DMA;
    `CHUNK_CLOSE` emits one all-ones filler flagged `data_out_chunk_last`
    when a pending chunk no longer fits under `DMA_max_size`, or on the
    200 µs `bundled_timeout`, or once after reset (`need_close`).
  - A lone chunk that can never fit is sent anyway (deadlock avoidance) and
    flagged sticky in `error[1]` (`chunkOversize`) — oversize local subevents
    are NOT yet split across DMAs.
- `evb_buf_rd_ila` probe3 now carries the chunk FSM state
  (`chunk_src, chunk_state, chunk_is_local, bundled_has_data, need_close,
  data_out_chunk_last`).
- `rd_active` kept as a derived wire for EVB3's hierarchical dbg references.

### EVB3.v

- New input port `DMA_max_size[15:0]` (bytes; wire it from the same register
  as the DMA engine's `DMA_max_size`, 0x9188-era default 16'h8000 — in
  hardware tops this needs a top-level connection).
- Arb FSM is now a pure ROC **demux** (`ARB_BUFMGR_DRAIN` deleted along with
  `self_header_sent` and all preemption coupling). `ARB_SELF_ROC` connects the
  ROC stream to the buffer manager local port; `roc_tready_arb =
  bufmgr_local_in_ready` (buffer manager throttles). Remote path unchanged.
- `bufmgr_local_in_valid` is gated by `HardwareEventBuildingEnable` so
  passthrough mode cannot shadow-consume the ROC stream.
- m_axis is a plain registered AXI stage fed only by the buffer manager, with
  a load-guard (`!m_axis_tvalid || axi_str_c2s0_tready`) that never overwrites
  an unaccepted word. `m_axis_tlast <= data_out_chunk_last` — **the remote
  path now asserts tlast** (previously never did); DMA transfers are closed by
  the all-ones filler word.
- RX-side FAFA injection deleted at both write points (mode78 ~line 797,
  mode33 ~line 876); `rx_data_to_buffer_is_header` removed; buffer manager
  `data_in` bit 64 tied 0.
- Counters:
  - `wc_self_transfer`: now counted at the buffer manager output
    (`is_local && !is_header && !chunk_last` handshakes).
  - `wc_bufmgr_output`: `!is_local && !is_header && !chunk_last`.
  - `wc_output_stream`: excludes `m_axis_is_header` AND `m_axis_tlast`
    (the close filler).
  - `wc_gbe_rx`: counts every buffer-manager write (no header gate needed).
  - `wc_roc_input`: unchanged — includes any generator filler words that the
    local port drops, so it may exceed `wc_self_transfer + wc_gbe_ddr_fifo`
    by one per DMA-close filler.
- `evb_user_axi_ila` probe4: `self_header_sent` slot replaced by
  `bufmgr_data_out_is_local`.
- `dbg_pcie_transfer_src/wc` latch unchanged — now latches correct
  known-in-advance values from each FAFA chunk header (`evb_pcie_transfer_ila`).

### EVB_DTCSim.sv

- EVB3 instantiation: added `.DMA_max_size(16'h8000)`.

### EVB2.v — untouched (its buffer manager instantiation is commented out).

## What to check in sim

1. `dbg_pcie_transfer_wc`/`_src`: local chunks should show the record byte
   count / 8 (8 words for the 0x40-byte CFO Event Record) with src = self IP;
   remote chunks show the FIFO snapshot with src = sender IP. No more 5/6
   fill-level values.
2. Walk the m_axis stream: FAFA(wc,src) → exactly wc data words → next FAFA or
   all-ones filler with tlast. Multiple chunks per DMA under back-to-back
   traffic; 200 µs timeout close under sparse traffic; exactly one filler per
   DMA.
3. Counter zero-sums: `wc_output_stream = wc_self_transfer +
   wc_bufmgr_output`; `wc_gbe_ddr_fifo = wc_ddr_to_tx = other DTC's
   wc_gbe_rx = wc_bufmgr_output`. Check the `wc_roc_input` delta to confirm
   how the generator actually closes DMAs (filler word vs tlast override with
   `CFOEventRecordMaxDMA_countPreset=998` / 200 µs timeout) and that the
   local filler-drop fires (dropped word = tlast at a chunk boundary).
4. First DMA after reset: a lone all-ones tlast word (the post-reset
   `need_close`) — software/scoreboard should discard it.
5. Buffer manager `error[1]` (chunk oversize) should stay 0 in the standard
   sim.
6. tlast behavior change on the remote path: confirm the DMA engine cleanly
   closes buffers now that remote data also ends in tlast (it previously never
   saw tlast from the bufmgr path).

## Post-sim debug round 1 (2026-08-12, /tmp/waves3.vcd)

Counters observed: `wc_roc_input` 512 ✓, `wc_gbe_ddr_fifo` / `wc_ddr_to_tx` /
`wc_gbe_rx` 256 ✓, but `wc_self_transfer` **248** (should be 256),
`wc_bufmgr_output` **263** (should be 256), `wc_output_stream` 511 (= 248+263).
FAFA chunks per subevent period: local **31** + remote **24+9 = 33** for
32-word subevents. Three bugs found and fixed:

1. **Local chunk off-by-one (data loss, 1 word per subevent).** The subevent
   header's byte-count field `[15:0]` counts the bytes *following* the header
   word (verified on the waveform: 32-word subevent carries 0x0F8 = 248 = 31
   words). The peek used `field/8` directly, so the FAFA claimed 31, the
   32nd word landed on a chunk boundary, its low bits are zero, and the
   zero-byte-count corruption guard dropped it. Fix:
   `local_peek_wc = local_in_data[15:3] + 1` (drop/req guards now check the
   raw field for zero instead of the +1'd peek).

2. **Remote duplicate words (+1 per subevent, data corruption).** The pop
   enable was gated with `buffer_empty_at_rd_source` (registered, one cycle
   stale) while `data_out_valid` and the chunk countdown used the live
   `buffer_empty`. At an FWFT fall-through gap (empty flickers high mid-chunk
   while a counted word propagates to dout) a word was counted as transferred
   without being popped -- emitted twice, and the chunk finished one pop
   short, rolling a +1 into the next chunk (the 24+9=33 pattern). Fix: pops
   are now gated per-FIFO with the live `buffer_empty[p]` -- identical gate to
   valid/count, and each BRAM enable is free of the 25:1 empty mux (the
   registered copy remains for ILA viewing only).

3. **`buffer_rd_cnt` width.** The regenerated IP is 1024 deep with an 11-bit
   `rd_data_count`; the RTL declared `[W:0]` = 6 bits (silent truncation above
   63 words). Widened to `[10:0]`.

Answers to the two observations:

- **`axi_str_c2s0_tlast` never goes high**: by design the DMA closes only when
  (a) a pending chunk no longer fits under `DMA_max_size`, (b) the 200 µs
  bundling timeout expires with no new chunk, or (c) once after reset (the
  single pulse at t=376 ns). With `DMA_max_size` = 16'h8000 (4096 words), only
  ~530 words of traffic, chunks arriving every ~1.7 µs, and a 31 µs sim,
  neither (a) nor (b) can trigger. Changed the sim wrapper `DMA_max_size` to
  **16'h0400** (128 words) so closes appear every few chunks; hardware keeps
  the register value.

- **`wr_data_count` doesn't count as expected**: the regenerated 1024-deep IP
  kept `Write_Data_Count_Width` = 6, and Xilinx narrow data counts report the
  count MSBs, i.e. **occupancy/16** -- it reads 1 only when >= 16 words are
  backlogged (matches the observed brief 0->1 pulses while a remote packet
  waits behind a local chunk). `rd_data_count` (11-bit) has full resolution,
  which is why it counts normally. **Flow-control flag**: `buffer_cnt` /
  `space_used` are fed by `wr_data_count`, so the space-availability protocol
  is now in units of 16 words. If word resolution matters, regenerate with
  Write_Data_Count_Width = 11 and widen `buffer_cnt`/the space protocol.

Expected after these fixes: `wc_self_transfer` 256, `wc_bufmgr_output` 256,
`wc_output_stream` 512, local FAFA wc = 0x20, remote FAFA wc values summing to
256 per side, no local drops (the boundary now lands on the next record's
header), and periodic tlast closes with the all-ones filler.

## Post-sim debug round 2 (2026-08-12, after round-1 fixes) -- VERIFIED

All round-1 fixes confirmed in the new waveform:

- Counters zero-sum exactly on both DTCs: `wc_roc_input` 512 =
  `wc_self_transfer` 256 + `wc_gbe_ddr_fifo` 256; `wc_output_stream` 512 =
  256 + `wc_bufmgr_output` 256; `wc_gbe_rx` = `wc_ddr_to_tx` = 256. No local
  drops (the chunk boundary now lands on the next record's header).
- Local FAFA wc = 0x20 (32) with src = self IP; remote chunks sum to exactly
  32 per subevent (24+8, 23+9, or 23+8+1 splits).
- DMA closes working: transfers of 109/102/125/112 words (incl. FAFAs +
  filler), all under the 128-word sim `DMA_max_size`, closed by the fit check
  with exactly one all-ones tlast filler each; the 1-word post-reset close DMA
  appears at t=376 ns.

Notes on observed (correct but notable) behavior:

- **Small tail chunks (e.g. FAFA wc = 1)**: the reader (250 MHz x 64b) drains
  faster than 10GbE RX writes (156 MHz x 64b), so a packet still arriving gets
  split -- the snapshot catches e.g. 23, then 8, then the last word alone as a
  1-word chunk with its own FAFA. Protocol-correct (software appends per-src),
  costs one FAFA word of overhead per split. If the overhead matters, a
  minimum-chunk threshold (don't grant a remote source below N words unless a
  flush timeout expires) would coalesce the tails -- latency trade-off, not
  implemented.
- **DMA open at sim end** (~100 words accumulated, no tlast yet): expected --
  it would close on the 200 us bundling timeout, which a ~31 us sim never
  reaches. Software sees those words only after the timeout (or size) close.
- VCD extraction artifact: for signals present in both DTC scopes the
  extraction script's CSV is overwritten by the last scope, and one DTC1
  signal id mis-parses into garbage rows (e.g. `wc_ddr_to_tx` "141399
  transitions, final 0") -- verify against the per-scope summary header, not
  the summary table alone.

## Known limitations / follow-ups

- Local subevents larger than `DMA_max_size` are not split across DMA
  transfers (sent oversize + sticky `error[1]`). If real subevents can exceed
  32 KB, a multi-DMA crossover like `_bundled`'s is needed.
- While a granted local chunk is mid-service, a ROC valid gap stalls the
  m_axis output (the chunk must finish before other sources are serviced) —
  bounded by record/subevent size.
- `DMA_max_size` reaches EVB3 only in the sim wrapper so far; hardware top
  needs the register wired when EVB3 goes to hardware.

## Timing mitigation (2026-08-13) — EVB2_buffer_manager.v

Bitfile implementation failed timing: 95 of the 100 worst paths ran from the
FIFO IP's `rd_data_count` register (`rd_dc_i_reg`) into the chunk FSM register
CEs (worst slack -1.03 ns on the 4 ns user_clk, 12-13 logic levels). Root
cause: `buffer_rd_cnt[scan_result_r]` was a **live 25:1 mux** feeding the
`!= 0` request check, the 18-bit fit adders/compares, grant arbitration, and
from there nearly every FSM register CE in one cycle, with nets spanning 25
BRAM FIFOs. Changes (no functional behavior change beyond ~3 extra idle
cycles per chunk arbitration):

1. **Registered count capture (`_r2` scan stage)**: the count mux output now
   lands directly in registers (`scan_cnt_r2`, `scan_nonzero_r2`) with an
   already-registered select; `scan_result_r2` / `scan_found_r2` /
   `scan_src_r2` (precomputed `EVBBaseNode + index`) keep everything aligned.
   Grants consume only `_r2` signals.
2. **Dedicated select copy** `scan_result_r_cntmux` (`dont_touch`) used only
   to index the count mux, so the placer can put it near the FIFO count
   registers; `(* max_fanout = 16 *)` hints on `scan_result_r` and
   `rd_source` (Vivado had already auto-replicated `rd_source`).
3. **`arb_settle` freshness guard** (2-bit counter, preset 3 while outside
   `CHUNK_ARB`): grants and `close_for_room` wait ~3 ARB cycles so the
   registered pipeline refreshes — a stale mid-drain count for a now-empty
   FIFO can never be granted (would hang CHUNK_DATA). No FIFO reads occur
   while sitting in ARB, so counts only grow and a settled snapshot never
   over-reports. Visible in `evb_buf_rd_ila` probe3 (2 bits added).
4. **Registered fit headroom** `bundled_headroom = DMA_max_wordcount -
   bundled_total_words - 2` (signed, 1-cycle lag covered by arb_settle);
   `local_fit`/`remote_fit` are now single compares — no adders in the grant
   cone.
5. **`bundled_timeout` decoupled from the grant chain** (31 failing
   endpoints): standalone saturating decrement in ARB (`has_data && != 0`),
   unconditional preset reload in `CHUNK_FAFA` and `CHUNK_CLOSE`, timeout
   close moved to the end of the ARB chain as `== 0` check. Same 200 µs
   semantics.

Remaining 5 failing paths were outside the buffer manager and likely
congestion fallout — re-check after this fix:

- `AXIMuxFromRingsData_i` `packetcount → s_axis_tdata` (4 paths, -0.44): if
  still failing, pipeline the packetcount compare terms.
- `fromRingsMuxDATA_sz_ila` internal trigger (1 path, -0.35, fanout 99): if
  still failing, regenerate the ILA with input pipe stages >= 1.

**Re-check in sim after this change**: identical FAFA framing and counters;
inter-chunk gap grows by ~3 user_clk cycles (arb_settle); no spurious
`error[1]` (oversize); back-to-back chunks from one source still drain via
successive snapshots.

## Per-DTC idle packet counters (2026-08-16) — EVB3.v

Two new `BRAM_TYPE` entries in the stats BRAM (`0x9160` indirect access):

| Type               | Value | By          | What it counts                        |
|--------------------|:-----:|:-----------:|---------------------------------------|
| `TX_IDLE_COUNT`    | `0x7` | destination | idle/empty packets **sent** to a DTC  |
| `RX_IDLE_COUNT`    | `0x8` | source      | idle/empty packets **received** from a DTC |

**TX side** (`txgbeclk` domain): 3 new FSM states (`TXFSM_STATE_IDLE_CNT` =
`0x0A`, `0x0B`, `0x0C`) perform a port-B read-modify-write after the
inter-packet gap, only for idle packets (`!have_data`). State `0x09` branches
to `0x0A` instead of `TXFSM_STATE_IDLE` when `!have_data`. Data packets are
unaffected. Cost: 3 extra `txgbeclk` cycles per idle packet.

**RX side** (`rxgbeclk` domain): `rx_fsm_shr` widened from `[18:0]` to
`[22:0]`. State 18 sets the read address (`BRAM_TYPE_RX_IDLE_COUNT`); state 21
conditionally increments (only when `!rx_packet_has_data`); state 22 restores
the debug RXSEQ address override (moved from old state 18). Collision checks
added at states 21 and 22. `RECV_COUNT` (type `0x0`) is unchanged — it still
counts all packets (data + idle).

Software status doc: [EVB3_software_status_registers.md](EVB3_software_status_registers.md).

---

# Change Notes: Add stats BRAM readback ILA (2026-08-20)

**Purpose:** Debug the stats BRAM software readback pipeline in hardware.
The mux at line 578 is currently commented out (the "old way"), so
`statsBRAM_addrb = statsBRAM_tx_addr`. Need to verify whether the pipeline
approach (state 0x00 loading `EVBSourceStats_raddr` into `statsBRAM_tx_addr`)
delivers correct readback data during active TX.

**New ILA:** `evb_stats_bram_ila` (`ila_5` on `txgbeclk`), inserted after
`evb_rx_idle_cnt_ila`.

| Probe | Width | Signal | Purpose |
|-------|-------|--------|---------|
| probe0[63:32] | 32 | `EVBSourceStats_rdata` | Data returned to software |
| probe0[31:0] | 32 | `statsBRAM_rdata` | Raw BRAM port B output |
| probe1 | 1 | `statsBRAM_sel_tx_addr` | 1=TX owns addr, 0=sw eligible |
| probe2 | 1 | `statsBRAM_sel_tx_addr_reg[1]` | Capture gate (0=rdata updates) |
| probe3 | 6 | `tx_fsm[5:0]` | TX FSM state |
| probe4[17:9] | 9 | `EVBSourceStats_raddr` | Software address input (async) |
| probe4[8:0] | 9 | `statsBRAM_addrb` | Actual BRAM port B address |
| probe4[26:18] | 9 | `statsBRAM_tx_addr` | TX address register |
| probe4[35:27] | 9 | `EVBSourceStats_raddr_reg` | Registered sw address |
| probe4[36] | 1 | `statsBRAM_tx_we` | BRAM write enable |
| probe4[38:37] | 2 | `statsBRAM_sel_tx_addr_reg` | Pipeline register |
| probe4[46:39] | 8 | `DestinationDTC_ip` | Round-robin destination |
| probe4[51:47] | 5 | `offset_dest_mac_reg` | Dest node offset |
| probe4[53:52] | 2 | `tx_fsm[7:6]` | Upper FSM bits |
| probe5 | 1 | `statsBRAM_tx_we` | Write enable (trigger) |
| probe6 | 1 | `have_data` | FSM has data to send |
| probe7[3:0] | 4 | `EVBSourceStats_raddr_reg[8:5]` | SW read BRAM type |

**Trigger suggestions:**
- Trigger `probe7 == 4'h7` → catch TX_IDLE_COUNT software reads
- Trigger `probe1` falling edge → see readback window opening
- Compare `probe0[63:32]` vs `probe0[31:0]` → sw data vs raw BRAM data

---

# Change Notes: Timing fix for wc_bufmgr_output / wc_self_transfer CE (2026-08-20)

**Failing paths (userclk1, 4.0 ns, slack -0.028):** source RX FIFO FWFT
`empty_fwft_i_reg` (SourceRxBuffer[7]) → 5 logic levels → `wc_bufmgr_output_reg[*]/CE`.

**Root cause:** `bufmgr_data_out_valid` is combinational out of
`EVB2_buffer_manager` (`data_out_valid = !buffer_empty[rd_source]` in the
output mux), so the CE of the 16-bit `wc_bufmgr_output` / `wc_self_transfer`
counters carried the full source-FIFO empty cone (5 levels, fanout 16).
Registered copies `bufmgr_data_out_valid_r/ready_r` existed from a previous
fix ("breaks 25:1 mux timing path") but the counters never used them.

**Fix (EVB3.v only):**
- Extended the registered-copy block with `bufmgr_data_out_is_header_r`,
  `bufmgr_data_out_chunk_last_r`, `bufmgr_data_out_is_local_r`.
- Switched the `wc_self_transfer` and `wc_bufmgr_output` increment conditions
  to the `_r` set. CE cone is now flop outputs + the counter's own
  `!= 16'hFFFF` feedback only.

**Behavior:** increments land one clock later; every accepted word
(`valid && ready` sampled together, so the AND is the delayed handshake) is
still counted exactly once. Counter values and register map semantics
(EVB_WORD_COUNT_2 'h9208 etc.) are unchanged.
