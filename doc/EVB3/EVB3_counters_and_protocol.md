# EVB3 Word Counters and Subevent Protocol Flow

## Overview

EVB3 is the event builder module for the DTC FPGA. It routes subevents from ROCs
by destination: self-destined subevents go to local PCIe DMA, remote-destined
subevents go out over 10GbE. Subevents arriving from remote DTCs are received
into `EVB2_buffer_manager` per-source FIFOs.

**All PCIe-bound event data (local and remote) flows through the buffer
manager**, which owns the m_axis DMA stream, frames every chunk with a FAFA
protocol header, and bundles chunks into DMA transfers. The software-facing
framing spec is in [EVB3_DMA_FAFA_protocol.md](EVB3_DMA_FAFA_protocol.md).

Seven 16-bit wrap-around word counters (saturating until 2026-09-10; every zero-sum identity below holds modulo 2^16) track data flow through the pipeline. They
count **data words only** (excluding FAFA protocol headers and DMA-close filler
words), making them zero-sum across the split and merge points.

## Counter Definitions

### Registers

| Register | Bits    | Counter            |
|----------|---------|--------------------|
| 0x9200   | [15:0]  | `wc_roc_input`     |
| 0x9200   | [31:16] | `wc_self_transfer` |
| 0x9204   | [15:0]  | `wc_gbe_ddr_fifo`  |
| 0x9204   | [31:16] | `wc_ddr_to_tx`     |
| 0x9208   | [15:0]  | `wc_bufmgr_output` |
| 0x9208   | [31:16] | `wc_output_stream` |
| 0x920C   | [15:0]  | `wc_gbe_rx`        |

### Counter Details

1. **`wc_roc_input`** (user_clk) -- Total words received from all ROCs via the
   AXI ROC interface (`axi_roc_c2s0_tvalid && axi_roc_c2s0_tready`). This is
   the entry point; all ROC words pass through here (including any DMA-closing
   filler words emitted by a ROC-side generator, which the buffer manager
   local port consumes and drops -- see "Local filler drop" below).

2. **`wc_self_transfer`** (user_clk) -- Local (self-destined) data words passing
   through the buffer manager output (`bufmgr_data_out` handshake with
   `data_out_is_local` set), excluding FAFA headers and the DMA-close filler.

3. **`wc_gbe_ddr_fifo`** (user_clk) -- Data words written into the DDR-backed
   subevent FIFO for transmission to remote DTCs via 10GbE (`subevent_in_we`).
   No protocol words exist in this path.

4. **`wc_ddr_to_tx`** (txgbeclk) -- Data words read from the DDR-to-TX FIFO for
   10GbE transmission (`ddr_to_tx_re`). Should equal `wc_gbe_ddr_fifo` when all
   queued data has been transmitted.

5. **`wc_bufmgr_output`** (user_clk) -- Remote data words passing through the
   buffer manager output (`data_out_is_local` clear), excluding FAFA headers
   and the DMA-close filler. These are subevents received from remote DTCs.

6. **`wc_output_stream`** (user_clk) -- Total data words output on the m_axis
   PCIe DMA stream (`axi_str_c2s0_tvalid && axi_str_c2s0_tready`), excluding
   FAFA headers (`m_axis_is_header`) and the DMA-close filler (`m_axis_tlast`),
   both pipeline-aligned with `m_axis_tvalid`.

7. **`wc_gbe_rx`** (rxgbeclk) -- Data words received over 10GbE and written into
   the buffer manager (`rx_data_to_buffer_we`). Every write is a pure data word
   (no FAFAs are stored in the buffer manager). Should match the sending DTC's
   `wc_ddr_to_tx`.

## Zero-Sum Relationships

```
wc_roc_input = wc_self_transfer + wc_gbe_ddr_fifo (+ dropped local fillers)
wc_output_stream = wc_self_transfer + wc_bufmgr_output   (PCIe output merges)
wc_gbe_ddr_fifo = wc_ddr_to_tx                            (DDR pipeline drains)
```

Note: `wc_bufmgr_output` counts data from *other* DTCs, not from this DTC's
ROCs. In a symmetric N-DTC system where every DTC sends the same amount, the
full accounting closes: `wc_gbe_rx` (words received from other DTCs) should
equal `wc_ddr_to_tx` (words sent to other DTCs), and therefore:

```
wc_output_stream = wc_self_transfer + wc_gbe_rx
```

If the ROC-side generator emits per-DMA filler/tlast words (see "Local filler
drop"), `wc_roc_input` exceeds `wc_self_transfer + wc_gbe_ddr_fifo` by the
number of dropped fillers -- confirm the delta in sim.

Verified 2-DTC sim values under the PREVIOUS architecture (8 subevents each
way, 32 words per subevent) -- to be re-verified after the FAFA-at-readout
change:

| Counter            | Verified | Accounting                                |
|--------------------|----------|--------------------------------------------|
| `wc_roc_input`     | 512      | 16 subevents x 32 words                    |
| `wc_self_transfer` | 256      | 8 self subevents x 32                      |
| `wc_gbe_ddr_fifo`  | 256      | 8 remote subevents x 32                    |
| `wc_ddr_to_tx`     | 256      | matches wc_gbe_ddr_fifo                    |
| `wc_gbe_rx`        | 256      | matches other DTC's wc_ddr_to_tx           |
| `wc_bufmgr_output` | 256      | matches wc_gbe_rx                          |
| `wc_output_stream` | 512      | 256 self + 256 bufmgr = wc_roc_input       |

Note on waveform inspection: each ethernet packet in this sim *appears* to end
with the same 64-bit value twice (`0x98a9510800000000`, `0x98a9...`). This is
genuine subevent content, not a TX duplication -- the test subevent's last two
words are identical. Three independent confirmations: (1) the self path shows
the same doubled final word on PCIe, (2) the per-packet counts are exact, and
(3) transition-based CSVs from the VCD hide the repeat (no value change), so
word-counting from CSV rows undercounts by one -- count handshake cycles, not
transitions.

### TX FSM last-word bug (found via these counters, fixed)

An earlier sim showed `wc_bufmgr_output` = 248 and `wc_output_stream` = 504 --
one word short per ethernet packet. Waveform tracing (`txdata` vs
`ddr_to_tx_rdata`) showed each packet's header claimed 32 payload words and the
DDR-to-TX FIFO was popped 32 times, but only 31 payload words appeared on the
wire: the TX FSM consumed the final FIFO word during the CRC-insert state
(commented as a "CRC placeholder word") and replaced it on the wire with the
computed ethernet CRC. That word was real ROC data, not a placeholder -- the
write side stores exactly 32 words per subevent (`wc_gbe_ddr_fifo` = 256 for 8
subevents). **The remote path silently dropped the last word of every packet.**

Fix (TXFSM_STATE_PAYLOAD): pop the FIFO only while `txPacketWordCount != 0`;
when the count reaches 0, transmit the final FWFT output word without another
pop, then proceed to CRC. The freeze-on-empty guard now covers only the PAYLOAD
state (the CRC state no longer consumes a word, and must not stall when the
FIFO is legitimately empty after the last queued subevent).

## FAFA Chunk Framing (firmware view)

FAFA header format and the software extraction algorithm are specified in
[EVB3_DMA_FAFA_protocol.md](EVB3_DMA_FAFA_protocol.md). Firmware-side design
points:

### Sizes are known in advance -- never counted

- **Remote chunk** (`EVB2_buffer_manager` chunk FSM): `chunk_wc` is a snapshot
  of the source FIFO's `rd_data_count` (11-bit, full resolution) at grant time.
  `rd_data_count` is native to the read clock (user_clk) and never over-reports
  the words in the FIFO, so the snapshot guarantees that many words will be
  drained -- no CDC tearing (sampling the rxgbeclk-domain binary
  `wr_data_count` on user_clk could capture torn values mid-write). Exactly
  `chunk_wc` words are drained; words arriving during the drain become a later
  chunk with its own FAFA. FWFT caveat: `empty` can flicker high mid-chunk
  while a counted word is still falling through to `dout` -- the chunk pauses
  (no count, no pop, m_axis bubble) and resumes. The pop enable and the
  valid/count gate MUST use the same live `empty` -- gating pops with a
  registered copy made words count as transferred without being popped
  (duplicate words on the stream, +1 per remote subevent in sim).

- **Local chunk**: the record size is peeked from the count quadword at the
  head of the local_in bus *before* it is accepted (`local_in_ready` still
  low). The AXIMux quadword `{…, packets[14:0], 4'h8}` is a byte count that
  INCLUDES itself, so `record_words = field >> 3` (HW: 0x198 → 51 words; the
  earlier "+1" wording here was wrong). Since 2026-09-08 the record is split
  to `DMA_max_words - 2` per chunk; the remainder is carried in
  `local_cont_wc` and granted as continuation chunks (same FAFA, src = self).
  A head word that is not a plausible count quadword (low nibble != 8 or
  zero words) is passed as a 1-word chunk and flagged (0x9370 bit 6) — never
  dropped; only tlast fillers are dropped. The peek reads `[15:3]` (max 8191
  words = 65,528 B), which is exact because a record is one AXIMux DMA
  transfer of at most `DMA_max_size - 8` bytes; see "Subevent Fragmentation".

### Buffer manager FIFOs hold pure data words only

No FAFAs are stored; FIFO bit 64 (the old `is_header` metadata tag) is tied 0
at the EVB3 `data_in` packing and ignored. The old RX-side FAFA injection
(mode78/mode33 packet headers) is deleted. FAFA insertion happens only in the
buffer manager's chunk-servicing FSM at readout, where `data_out_is_header` is
now generated (it no longer comes from FIFO bit 64).

### Local stream routing and throttling

EVB3's demux FSM (`ARB_IDLE / ARB_STALL_DEST / ARB_SELF_ROC / ARB_REMOTE_ROC`)
routes each tlast-delimited ROC transfer by destination. In `ARB_SELF_ROC`
the ROC stream connects to the buffer manager `local_in` port and
`axi_roc_c2s0_tready = local_in_ready` -- the buffer manager throttles the
local stream, holding ready low while a different source's chunk is being
serviced. The ROC-to-remote path (DDR -> 10GbE TX) is independent of the
buffer manager. The old `ARB_BUFMGR_DRAIN` state, drain-until-empty reader,
and ROC-preemption coupling are deleted: every chunk has a pre-known bounded
size and runs to completion.

A mid-transfer ROC valid gap bails `ARB_SELF_ROC` to `ARB_IDLE` and safely
re-enters (`subevent_in_dest_valid` derives from the `tdest` sideband and
persists until the tlast handshake); the buffer manager's local chunk
countdown pauses and resumes.

### Which AXIMux the EVB build uses

EVB builds compile the PLAIN `AXIMuxFromRingsData.v`, never
`AXIMuxFromRingsData_bundled.v` (Ryan, 2026-09-08). Both files declare
`module AXIMuxFromRingsData` (instantiated by RingController), so the choice
is made by which file is in the Vivado project, not by a parameter or define.
Consequences for the HEB path:

- The mux does no bundling of its own: one DMA transfer per record, closed by
  one extra tlast word that the count quadword does not declare (end of
  transfer in fsm 12, pause-and-restart in fsm 7, or the `m_axis_needLast`
  override after a reset). The ONLY bundling in the path is
  EVB2_buffer_manager's (next section).
- The mux splits an aggregate above `DMA_max_packetcount` into several
  transfers ("need to do multiple DMAs"), each with its own count quadword.
  That is what bounds a record to `DMA_max_size - 8` bytes.
- The Aldec sim elaborates the same plain file, so sim and HW agree on this
  module in EVB mode.
- Timing: the plain file's fsm-7 `total == max` compare is registered
  (`total_DMA_at_max_r`, 2026-09-08), mirroring the `_bundled.v` fix.

### Local filler drop

A word presented at a chunk boundary with tlast asserted is the DMA-closing
extra word from the plain AXIMux above (real record/subevent header words
never carry tlast): the buffer manager consumes and drops it -- not stored,
not counted, no FAFA. Sim-verified (zero-sum exact with the drop). A header
word whose low nibble is not 8 or whose count field is zero is NOT dropped
(that was a silent loss): it passes as a 1-word chunk and sets the sticky
EVBERR_LOCAL_BAD_HEADER (0x9370 bit 6).

### DMA bundling and tlast

Multiple chunks stack into one DMA transfer (scheme borrowed from
`AXIMuxFromRingsData_bundled.v`: `bundled_has_data`, accumulated word count,
200 µs timeout -- EVB builds do NOT compile that file, see "Which AXIMux the
EVB build uses" above; the buffer manager is the only bundler). The DMA
engine requires tlast to close a transfer; the buffer manager emits one
all-ones filler word flagged `data_out_chunk_last`, which the m_axis stage
turns into `m_axis_tlast`, when a pending chunk no longer fits under
`DMA_max_size` or the timeout expires. After reset, one close filler is
emitted unconditionally (the `m_axis_needLast` pattern) to terminate any DMA
left open. No chunk can exceed `DMA_max_size`: remote and local chunks are
capped at `DMA_max_words - 2` (FAFA word + close filler) and the rest follows
as further chunks (2026-09-08; the old "sent anyway" grant and its oversize
flag are gone).

### m_axis output stage and pipeline alignment

The m_axis output is a registered AXI stage fed only by the buffer manager,
with a load-guard (`!m_axis_tvalid || axi_str_c2s0_tready`) that never
overwrites an unaccepted word; `bufmgr_data_out_ready` is the same condition,
so the buffer manager holds its combinational output word whenever the stage
stalls. `m_axis_is_header` and `m_axis_tlast` travel through the stage with
the data, so counter gating and the `dbg_pcie_transfer_*` latch see flags
aligned with `m_axis_tvalid` (checking the unregistered flags against the
m_axis handshake would be off by one cycle).

### Why FAFA words cannot be filtered by data pattern

The value `0xFAFA` can appear as legitimate data. Counters and the
`dbg_pcie_transfer_*` latch never pattern-match the data bus; they use the
`data_out_is_header` / `m_axis_is_header` and `data_out_chunk_last` /
`m_axis_tlast` flags generated by the chunk FSM. (Software, which only has the
data, relies on the framing invariants instead: a DMA buffer always starts
with a FAFA header, and `chunk_wc` walks exactly to the next header or the
close filler.)

## Data Flow Diagram

```
ROCs
  |
  v
axi_roc_c2s0 -----> [wc_roc_input]
  |
  |  (demux FSM, by tdest-derived destination)
  |
  +---(self-dest)---> buffer manager local_in port --------------+
  |                   (throttled by local_in_ready;              |
  |                    boundary tlast fillers dropped)           |
  |                                                              v
  +---(remote)------> subevent_in_we --> [wc_gbe_ddr_fifo]   EVB2_buffer_manager
                      |                                      chunk-servicing FSM
                      v                                      (FAFA header + exact
                      DDR FIFO                                chunk_wc per chunk,
                      |                                       DMA bundling + close
                      v                                       filler w/ tlast)
                  ddr_to_tx_re --> [wc_ddr_to_tx]                 |
                      |                                           |
                      v                              local words: [wc_self_transfer]
                  10GbE TX  ------->  remote DTC    remote words: [wc_bufmgr_output]
                                          |                       |
                                      10GbE RX                    v
                                          |               m_axis register stage
                                          v                       |
                                    per-source FIFOs              v
                                    [wc_gbe_rx]           axi_str_c2s0 (PCIe DMA)
                                    (pure data words) ->  [wc_output_stream]
```

## Subevent Fragmentation

A subevent can be large (the ROC aggregate can reach 2^12 packets x 6 ROCs),
but EVB3 never sees it as one record: the AXIMux splits any aggregate larger
than `DMA_max_packetcount = DMA_max_size[15:4] - 1` packets into several DMA
transfers, each starting with its own count quadword. A record is therefore at
most `DMA_max_size - 8` bytes (<= 65,528 B = 8191 words with the 16-bit
register), which is exactly what EVB3's `[15:3]` word fields (local peek, DDR
pad-strip, count validation) and software's 16-bit mask cover. Records of one
subevent are consecutive in the same source stream. The 10GbE
TX path fragments this into ethernet packets of at most `MAX_PACKET_BYTES`
(1492) bytes each. On the receive side the packets accumulate in the source's
FIFO as pure data; the readout FSM frames whatever has accumulated into chunks,
so a fragmented subevent reaches software as multiple FAFA chunks from the same
source. Software reassembles per-source and finds subevent boundaries from the
subevent headers inside the reassembled stream (see
[EVB3_DMA_FAFA_protocol.md](EVB3_DMA_FAFA_protocol.md)).

## Self-Subevent Throttle (software staging bound)

Added 2026-09-04. Self-destined subevents reach software immediately; the
peers' subevents for the same event arrive later through their TX windows.
Software must hold each event until all N subevents are present, so if the local
stream ran far ahead of the peers, software staging memory would be unbounded.

Rule (EVB3.v, ROC demux): a self subevent whose `EWT[15:0]` leads the **peer
frontier** by more than `EVB_LOCAL_THROTTLE_WINDOW` (fixed at 1024) is held in
`ARB_STALL_DEST` with ROC `tready` low until the frontier catches up. The peer
frontier is the MIN over the configured peers (`EVBStartNode .. +EVBNumNodes-1`,
excluding self) of the latest subevent tag actually **received** from that
peer; a per-source parser at the RX store tracks subevent boundaries from the
word-0 byte count. Peers that have not sent anything yet count as the run's
first tag, so a silent peer stalls the local stream after 1024 events instead
of leaving the bound open.

Consequences for software:
- Staging memory per DTC is bounded by 1024 events x max subevent (192 KB) =
  **192 MB**, independent of N (each DTC builds 1/N of the events, each with N
  subevents).
- A dead or disconnected peer stalls this DTC's ROC input (all counters freeze,
  `wc_local_throttle_events` grows) after 1024 events. This is intentional:
  event building cannot proceed; fix the peer or reduce `EVBNumNodes`.
- Because the ROC stream is in order, remote-destined subevents queued behind a
  held self subevent are delayed too, which also stalls this DTC's TX; the peers
  then see this DTC's frontier stop. Two DTCs cannot deadlock on each other
  (a DTC is only held while a peer is *behind* it, and that peer is not held).
- The tag comparison is a signed 16-bit modular difference, so tags may wrap.
- Requires all DTCs to be SoftReset before a run (the parser assumes each peer's
  first received word is a subevent header).

Visibility (no ILA port changes, packed into previously '0-tied probe bits):
`evb_ddr_tx_ila` (ila_109, txgbeclk, present in the HW build) carries
`frontier_valid_u`, `local_throttle_r` (trigger `local_throttle_r == 1` to catch
a hold), `frontier_min_u`, `local_tag16` in probe5's spare upper bits
(probe5[63:0] is still `ddr_to_tx_rdata`) and `wc_local_throttle_events` in
probe13; since 2026-09-08 also `roc_held_long` / `dma_bp_long` (condition held
≥ 262 µs), `evb_output_arb_state`, `subevent_in_dest_valid`, `roc_tready_arb`,
`axi_roc_c2s0_tvalid`, `subevent_dest_is_self`, `local_throttle_hold`,
`tx_credit_throttled`, `tx_staging_no_slot`, and the 0x9370 sticky error half
`evb_err_status_raw[15:0]` (trigger `== R` on a bit for its first occurrence).
Trigger and inspect by signal name in the hardware manager. These are user_clk
signals sampled by a txgbeclk ILA: levels and quasi-static values, fine for
debug. `evb_rx_ila` probe22[63:48] (rx-side frontier) and
probe11[63:50] (frontier valid, seeded, parser countdown). `evb_user_axi_ila`
is unchanged (probe0 word 0 [47:32] = the held tag, probe3[4:2] = ARB state).
Note: the same view also sits on `evb_ddr_ila` probe8/11/12/24, but that ILA
has been block-commented out since 2026-07-14 and is not in hardware.

## RX Buffer Space Throttle (drained-credit flow control, no drops)

Design rule: a DTC never drops a received packet. The receiver has no
back-pressure on the 10GbE link, so the sender must never put more on the wire
than the destination's per-source RX FIFO (`EVB2_SourceRxDataFIFO`, 1024 words
per source) can take. Since 2026-09-08 the protocol is credit-based:

- Every packet header a DTC sends, idle packets included, carries in bits [7:3]
  the **cumulative number of words it has drained** (popped for DMA) from the
  destination's FIFO at the sender, in 32-word units mod 32
  (`EVB2_buffer_manager.space_drained`, counted on the read clock, gray-coded
  for the txgbeclk sample).
- The destination stores that per source in `RXSEQ_AND_RXSPACE` and its TX
  keeps `tx_sent_words[dest]`, the declared payload words it has sent to each
  destination (credited once per data packet at the header, exactly what the
  RX stores: pads and idle packets are not stored).
- **2026-09-17 correction (fix B):** the free-space computation is now done in
  WORDS: `outstanding_upper = sent_words - drained_units x 32` (mod 1024, an
  upper bound because the receiver's drained report is truncated to units),
  `free_words = 992 - outstanding_upper`, saturating at 0. The earlier
  unit-only difference below could read one unit LOW (floor(O/32) vs
  floor(O/32)+1 depending on residues) and the cap then only bounded O at
  1023 words, so for O in 993..1023 the 5-bit difference aliased 32 to 0,
  free read 992, and the sender overfilled the peer's FIFO (HW 0x9370 bit 0,
  three times at gap 0xff under DMA back-pressure, 197 words of a record
  overwritten). With word precision O <= 992 holds inductively and no
  aliasing is possible. Usable buffer stays 992 words. The sim now runs the
  HW credit (31 units; it was 4) and a 40 us DMA block reaches the
  aliasing state as a regression.
- At window open ('h17), historical description: `outstanding = sent_units - drained_units` (mod 32),
  `free = (31 - outstanding) x 32` words. 'h00 sends only while free is
  non-zero, clamps the packet to it, and decrements per word sent.

Why this is exact and needs no margin: one sender per FIFO, so the sender's
in-flight words are already in `sent`; a report can only lag, and a lagging
report under-states drained, which is the safe direction. Floor rounding of
both counters means real outstanding is at most 31 words above 32 x
outstanding_units, and the sender stops at 31 units, so the FIFO never reaches
1024 and the mod-32 difference never aliases. Usable buffer: 992 words per
source. With a slow DMA consumer the peers' TX stalls, their DDR channels fill,
and finally their ROC `tready` drops. Nothing is lost anywhere in that chain.

Staging is credit-limited too (2026-09-08): the DDR read arbiter fetches a
burst for a channel only while `staged + 64 <= credit` of that destination
(`tx_fetch_ok_act` for the active window from the live 'h17 result,
`tx_fetch_ok_pf` for the prefetch destination from its last-read drained count,
a lower bound). Staged words are therefore always covered by credit, so a
partial-subevent residue in a staging FIFO can always be sent in its owner's
next window and a starved peer can never pin one of the two staging FIFOs
(the sim showed two starved residues pinning both and stalling every other
destination for 40 µs). When staging stops for credit the HEADER shrink guard
sizes the packet to what is staged; the rest waits in DDR.

`TX_SPACE_CREDIT_UNITS` is 31 in hardware and 4 (128 words) in `EVBSimMode`, so
the 6-DTC testbench can exercise the stall: it blocks DTC_0's DMA for 20 µs
(40-60 µs), expects every peer to stop at 4 units outstanding with no FIFO
write-while-full, and checks sent == drained + in-FIFO for every pair at the
end of the run (END-OF-RUN SUMMARY, "RX-buffer credit" line).

Assumptions: both counters restart at zero on SoftReset, so all DTCs must be
reset together (the routing scheme already requires this). A data packet the
RX rejects at its header (illegal destination, source offset > 31) leaks its
word count of credit until the next reset; those are configuration errors.

Invariant: `we_on_full_err` (0x9370 EVBERROR bit 2) must never set. It did on
2026-09-08 (1000-event 2-node run) under the previous occupancy-based report:
the sender converted it as 64-word units of a 2048-word FIFO (the FIFO had
since been cut to 1024) and the report excluded in-flight words, so the peer
FIFO filled to 1025 words and words were lost mid-subevent (software saw a
5-word subevent 40 followed by a 43-word subevent 64 from the same source).
