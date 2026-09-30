# EVB3 Software Status Registers

How to read event-building counters and per-DTC 10GbE statistics from the DTC.

## Configuration registers

| Address  | Field     | Purpose                                           |
|----------|-----------|---------------------------------------------------|
| `0x9154` | `[7:0]`   | This DTC's node ID (`SelfDTC_ip`)                 |
| `0x9158` | `[15:8]`  | `EVBStartNode` — base node for per-DTC offset math|
| `0x9158` | `[7:0]`   | `EVBNumNodes` (clamped 1–128)                     |
| `0x9158` | `[31:16]` | `EVB_TxDeadTime` — txgbeclk clocks at each destination switch |
| `0x915C` | `[31:16]` | `EVB_IdlePacketWords` — idle packet payload, 8-byte words (frame = 20 + 8 x words bytes). Keep >= 12: data packets are padded to 12 words (2026-09-17) so the shortest frame is 16 beats and two frame starts through a gap-compressing switch are never closer than the 12-beat RX stats pass; a shorter idle frame would re-open that collision |
| `0x915C` | `[15:8]`  | `EVB_InterpacketGap` — txgbeclk clocks between frames (protocol min 12 = 0x0c) |
| `0x9170` | `[15:0]`  | `EVB_IdleBurstCount` (2026-09-17) — idle packets per destination window when there is no data; 0/1 = one (legacy, reset value), N = up to N gap-spaced idles until the window closes. Link-test knob, see "Pure-idle link test" below |

`DTC_offset = node_MAC − EVBStartNode`, range 0–31.

---

## A. Global pipeline word counters (direct read)

16-bit WRAP-AROUND counters (since 2026-09-10; saturating before, which broke the zero-sum past ~1285 events of 51 words), packed two per 32-bit register. Software must compare them modulo 2^16: `(output - self - bufmgr) & 0xFFFF == 0`, `(ddr_to_tx - gbe_rx) & 0xFFFF == 0`, etc. The BRAM per-DTC stats are NOT changed and still saturate. Reset on soft/hard
reset. These are aggregate totals — not per-DTC.

| Address  | `[31:16]`          | `[15:0]`            | Notes |
|----------|--------------------|---------------------|-------|
| `0x9200` | `wc_self_transfer` | `wc_roc_input`      | self-transfer = local data words through buffer-manager output; roc_input excludes every tlast terminator word (reset close filler AND the mux's undeclared per-subevent extra word) |
| `0x9204` | `wc_ddr_to_tx`     | `wc_gbe_ddr_fifo`   | ddr_to_tx is in txgbeclk domain; pad-free (DDR burst padding is stripped before TX and never transmitted) |
| `0x9208` | `wc_output_stream` | `wc_bufmgr_output`  | output_stream = self_transfer + bufmgr_output |
| `0x920C` | *(zero)*           | `wc_gbe_rx`         | in rxgbeclk domain |

All counters exclude FAFA protocol headers and terminator/filler words, and count
subevents at exactly their declared size (the count word's `{agg,4'h8}` byte count):
the mux's undeclared extra tlast word is dropped at EVB ingest under HEB, and DDR
burst pad words are stripped before the 10GbE TX (2026-08-30).

**Zero-sum checks (all exact, run after run):**
- `wc_roc_input = wc_self_transfer + wc_gbe_ddr_fifo`
- `wc_gbe_ddr_fifo = wc_ddr_to_tx`
- On the receiving DTC: `wc_gbe_rx = sender's wc_ddr_to_tx` (= `wc_bufmgr_output`)
- `wc_output_stream = wc_self_transfer + wc_bufmgr_output` (= `wc_roc_input` with symmetric DTCs)

---

## B. Per-DTC counters (indirect via `0x9160`)

A dual-port BRAM stores 7 counter types × up to 32 DTC entries, accessed
through write-then-read on a single register.

### Read procedure

1. **Write** the 9-bit BRAM address to `0x9160`:
   - bits `[8:5]` = `BRAM_TYPE` (see table below)
   - bits `[4:0]` = `DTC_offset` (0–31)
2. **Wait** a few PCIe clock cycles. The BRAM read port is shared with the TX
   FSM: an address mux hands the port to software whenever the TX FSM is not
   actively using it (most cycles, even mid-packet), and a capture gate ensures
   only software-address data is latched — never TX-address data. If the TX FSM
   holds the port continuously (back-to-back BRAM operations), the returned value
   is the last correctly captured one (stale but valid, never garbage).
3. **Read** `0x9160` — returns the 32-bit BRAM value.

> **Read-freshness note (corrected 2026-09-05).** The BRAM port used for these
> reads is shared with the EVB transmit FSM. The value returned is always a
> genuine BRAM word, never a mix, but it is the word for the address the host
> wrote *only after the port has been free for ~26 ns since that write*. Until
> firmware 26.09.05, the transmit header state held the port for the whole
> time it waited for a data packet to be staged from DDR (microseconds), so a
> host read landing in that window returned the value of the **previously**
> read address (software observed this on every cell of a sweep taken during
> data traffic; idle-only sweeps were clean because idle packets never wait).
> Firmware now releases the port during that wait; the remaining holds are a
> few tens of ns. Software's "write address, read twice, keep the second"
> (`DTC_Registers.cpp:2461`) is a sound belt-and-braces and stays.

### BRAM_TYPE encoding

| Type                  | Value | Organized by    | What it stores                                    |
|-----------------------|:-----:|:---------------:|---------------------------------------------------|
| `RECV_COUNT`          | `0x0` | source          | RX packet count from that DTC                     |
| `RXSEQ_AND_RXSPACE`   | `0x1` | source          | `{time, drained[4:0], rx_seq[7:0]}` -- drained = cumulative words THAT source has drained of this DTC's data from its 1024-word RX FIFO, 32-word units mod 32 (send credit; TX free = (31 - (sent - drained)) x 32 words) |
| `DROP_COUNT`          | `0x2` | source          | Dropped packets (detected via RX sequence gaps)   |
| `BYTE_COUNT`          | `0x3` | source          | Cumulative RX payload bytes from that DTC         |
| `LAST_TIME`           | `0x4` | source          | Last-activity timestamp (`rx_clkMarker_count`)    |
| `TXSEQ`               | `0x5` | **destination** | Last TX sequence number to that DTC               |
| `TRAVEL_TIME`         | `0x6` | source          | Packet travel time through the switch             |
| `TX_IDLE_COUNT`       | `0x7` | **destination** | Idle/empty packets sent to that DTC               |
| `RX_IDLE_COUNT`       | `0x8` | source          | Idle/empty packets received from that DTC         |
| `TX_COUNT`            | `0x9` | **total** (one value, any offset) | EVERY frame this DTC sent on its 10GbE port, all destinations, idle + data, 32-bit; added 2026-09-15. A single register (the twin of the switch's per-port ingress packet counter), not a BRAM row; read through 0x9160 like the others (`(0x9 << 5) | 0`, the offset is ignored). Cleared by SoftReset only (TX disable does not clear it). Wire-loss check: sender `TxCount` == switch ingress packets on its port == switch egress packets on the peer's port == sum of the peer's `RxCount` rows (one row per source); the link that differs is where frames vanish |

### Examples

Read RX packet count from DTC offset 3:
```
write 0x9160 = (0x0 << 5) | 3 = 0x003
wait
read  0x9160  → 32-bit receive count
```

Read drop count from DTC offset 5:
```
write 0x9160 = (0x2 << 5) | 5 = 0x045
wait
read  0x9160  → 32-bit drop count
```

Read TX sequence number to destination offset 10:
```
write 0x9160 = (0x5 << 5) | 10 = 0x0AA
wait
read  0x9160  → 32-bit TX seq
```

Read TX idle packets sent to destination offset 2:
```
write 0x9160 = (0x7 << 5) | 2 = 0x0E2
wait
read  0x9160  → 32-bit TX idle count
```

Read RX idle packets received from source offset 4:
```
write 0x9160 = (0x8 << 5) | 4 = 0x104
wait
read  0x9160  → 32-bit RX idle count
```

### Sweep pattern

To read all RX packet counts for N DTCs:
```
for offset in 0 .. N-1:
    write 0x9160 = (BRAM_TYPE << 5) | offset
    wait
    count[offset] = read 0x9160
```

---

## B2. EVB error / status register `0x9370` (EVBERROR)

Added 2026-09-08 (before that the EVB3 error output was unconnected in the
EVB3 build and the register latch was commented out, so 0x9370 always read 0).

`[15:0]` are **sticky error flags, cleared only by SoftReset** (a write to
0x9370 has no effect; toggling the EVB link Rx/Tx enable also clears the
RX/TX-side flags because their domain resets include it). Every one of them
means data was lost or corrupted, or the protocol state is untrustworthy:

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | RX_BUF_WRITE_FULL | RX source buffer written while full -- words lost (should never happen with the drained-credit throttle) |
| 1 | RX_SEQ_GAP | RX sequence gap -- a packet was lost on the wire (count per source in RxMissingPktCnt). CAVEAT: on bitfiles built 2026-09-15 (14:00 through the evening) this bit and every Rx-type BRAM row are garbage regardless of bit 7 -- the re-ordered stats pass froze on gearbox bubbles while the BRAM read pipeline did not, shifting every readback by one row (fixed 2026-09-15 evening: the pass now runs through bubbles). On earlier bitfiles the same rows were corrupted only when bit 7 was set (frame overlap). Word-count parity (sender DDR->TX words vs receiver GBE Rx words) remains the authoritative loss check on every bitfile |
| 2 | RX_PKT_REJECTED | An EVB frame addressed to THIS DTC was not stored because its source offset (source MAC byte - start node) is > 31: a peer is misconfigured or the start node is wrong. Frames not addressed to this DTC (another DTC, another partition, non-EVB start) are status bit 26, not an error (changed 2026-09-09: HW showed switch-flooded idle packets from a partition-0x01 DTC to MAC 0x00 on both DTCs) |
| 3 | DDR_WR_UNDERFLOW | DDR write CDC FIFO went empty inside a burst (corrupt burst), or (2026-09-23) a word was written into the CDC FIFO while it was full and the FIFO dropped it. The second case was the 60-word loss of 2026-09-23 (record keeps its declared length, next record's head fills the tail, receiver sets bit 8); fixed in the build after 0xd60922a0, this bit is the tripwire that it stays fixed |
| 4 | TX_FSM_FAULT | TX FSM undefined state / destination offset > 31; since 2026-09-19 also TX WINDOW OVERRUN: one of this DTC's frames was still on the wire when its destination window closed, so it overlapped the next sender's window to that receiver. Through a switch the two frames collide at the output port and one is dropped uncounted (TxCount == switch-in, switch-out == in - 1, 0 switch errors). Sender-side flag: look for it on the DTC whose frame vanished, not the receiver. The TX now measures the remaining window in clocks (not words sent) before starting a frame, so this must stay clear |
| 5 | CREDIT_VIOLATION | sent/drained credit counters disagree by more than the credit (one-sided reset) |
| 6 | LOCAL_BAD_HEADER | the local (self) stream was misaligned: the word at a record boundary was not a count quadword (low nibble 8, non-zero words). Nothing is dropped -- the word goes out as a 1-word chunk and framing re-syncs at the next real header -- but a word was lost or duplicated upstream of the buffer manager, so the affected local record is corrupt. Never observed so far. (Until 2026-09-08 this bit meant chunk > DMA_max_size; it fired on both DTCs in every HW run before 2026-09-09 only because DMA_max_size was unconnected in the HW EVB3 instance and synthesized to 0, not because of any misalignment) |
| 7 | RX_STATS_COLLISION | RX stats pipeline collision -- per-source stats may be wrong, data was stored. HW-proven 2026-09-15: the switch re-spaces queued frames to the minimum Ethernet gap, so a short frame followed immediately by another frame overlapped the old 23-beat bookkeeping pass. FIXED the same day by re-ordering the pass to 12 beats, shorter than the 17-beat physical minimum between frame starts; the detector stays as the guard. If it sets on a bitfile built after 2026-09-15, the spacing assumption is broken and the stats rows plus bit 1 are untrustworthy for that run; data path unaffected either way |
| 8 | DDR_RD_BAD_COUNT | DDR read stream desync: a non-count word arrived where a subevent count quadword was due; it was dropped and the stream re-synced at the next valid count |
| 9 | STAGING_WRITE_FULL | DDR->TX staging FIFO written while full -- words lost, TX then under-runs the declared packet |
| 10 | TX_PAYLOAD_UNDERFLOW | staging FIFO went empty inside a packet payload -- the wire repeated a word |
| 11 | TX_FRAME_MALFORMED | the TX wire monitor saw a frame leave without a legal shape (start not followed by terminate, terminate without start, idle inside a frame, data outside a frame, or more than 200 blocks). Added 2026-09-15 for the lost-first-fragment hunt: the sender-side twin of the receiver's sequence-gap bit |
| 12 | RX_FRAME_SIZE | an accepted EVB frame's data words on the wire did not match its header byte count: fewer than declared (truncated on the wire, split by the switch, corrupt length) or more than the declared count plus legal min-frame padding (merged frames, corrupt length). Added 2026-09-16; 4-byte-shifted (0x33) frames are held to exactly one more data beat than 0x78 frames. The store already stops at the declared count, so a long frame loses nothing; a short frame is real loss and this is the flag for it. Not yet run in hardware (first bitfile pending the 2026-09-16 sim run 12). ILA: `evb_rx_ila` trigger `rx_frame_size_mismatch == 1` |
| 13 | RX_FCS_BAD | an accepted 0x78-framed EVB frame arrived with an FCS that does not match the CRC-32 of its words (same span the sender covers: destination word through the last payload/pad word). Added 2026-09-16 (the RX CRC engine had been commented out for years; 0x9590 is dead in EVB3 builds). The frame was already stored when this fires, so it flags corruption after the fact. 4-byte-shifted (0x33) frames are not checked. Not yet run in hardware (first bitfile pending the 2026-09-16 sim run 12). ILA: `evb_rx_ila` trigger `rx_fcs_bad == 1` | **2026-09-22:** on builds 0xd6091797 through 0xd6092293 this bit is a false alarm: when a frame's last data word lands on a 10GbE gearbox bubble, the checker missed that frame's terminate block, clocked it as data and never cleared, so the NEXT frame compared wrong (the wire FCS was correct in every ILA capture). About 1 frame in 33 poisons the next one. Fixed in the build after 0xd6092293. Software keeps those five builds on its exception list; from the next build on, bit 13 means a real FCS mismatch.
| 14 | ROC_TAG_SLIP | at EVB3's ROC input (the AXI stream from the AXIMux, before the self/remote split) a single-record subevent's header word 0 carried an EWT[15:0] different from the EWT[15:0] in its first ROC fragment word (beat 7 of the record). Added 2026-09-16 for the header/data one-window slip seen on calo-12 (header N+1 over data N, 8 slips in 15 100k runs, both DTCs, both gaps): if this bit is set on the DTC whose record software rejected, the slip was already present when the record left the RingController/AXIMux; if it is clear there, the slip happened inside EVB3. Not checked for split (multi-record) subevents. Not yet run in hardware. ILA: `evb_user_axi_ila` trigger `roc_chk_tag_slip == 1` |
| 15 | ROC_RECORD_SHAPE | at EVB3's ROC input a record's accepted beats (excluding the DMA-close word) did not equal its count quadword, or its header word 0 declared fewer bytes than the count quadword. Added 2026-09-16 for the 64-word deficit that accompanied 6 of the 8 slips (wc_roc_input / wc_self_transfer ending exactly 64 words short of whole records). Not yet run in hardware. ILA: `evb_user_axi_ila` triggers `roc_chk_len_bad == 1` / `roc_chk_hdr_bad == 1` |

`[31:16]` are **live status levels**, with one exception: bit 26 is sticky
since SoftReset (informational). Status bits are back-pressure points, not errors --
back-pressure propagates upstream and is flagged there):

| Bit | Name | Meaning |
|-----|------|---------|
| 16 | ROC_HELD | ROC input tvalid && !tready |
| 17 | SELF_THROTTLE | self-subevent throttle holding a subevent |
| 18 | CREDIT_THROTTLE | current window has data but the destination has no credit |
| 19 | DDR_ALMOST_FULL | DDR channel almost-full back-pressure |
| 20 | DDR_CDC_FULL | DDR write CDC FIFO full |
| 21 | STAGING_NO_SLOT | HEADER waiting: both staging FIFOs owned by other destinations |
| 22 | RX_BUF_HIGH | any RX source buffer >= 3/4 full |
| 23 | DMA_BACKPRESSURE | m_axis_tvalid && !axi_str_c2s0_tready |
| 24 | FRONTIER_VALID | peer frontier known (self-throttle armed) |
| 25 | DDR_CALIB_DONE | DDR calibration complete |
| 26 | FOREIGN_FRAME | sticky since SoftReset (informational): a frame not for this DTC arrived and was ignored -- unknown start alignment (switch LLDP/STP and the like), OR destination MAC not ours: DTC byte (byte 5) != our MAC byte or partition byte (byte 2, 0x9154[15:8]) != our partition, and not broadcast 0xFF (switch flooding of unknown-unicast, other partitions sharing the switch). A frame is accepted only when BOTH bytes match (partition check added 2026-09-09). Expected on shared switches; nothing of ours is lost |
| 27-31 | reserved | 0 |

Software: read after every run; any nonzero `[15:0]` invalidates the run's
data. Do not attempt to clear by writing.

### B3. EVB Status check -- what software should test (2026-09-16)

One read of `0x9370` per DTC, at run start (after SoftReset) and after every run
or on a periodic poll. Three classes of bit:

**1. ERROR -- run is bad. `0x9370[15:0] != 0`** (mask `0x0000_FFFF`; was
`0x3FFF` until bits 14-15 were added 2026-09-16). Report the DTC, the hex value
and the set bit names; mark the run's data invalid. Every bit means loss,
corruption or an untrustworthy protocol state; none is recoverable without a
SoftReset on all DTCs. Bits 0-15 as tabled above. Suggested severity grouping
for the message:

| Group | Bits | What it tells the operator |
|-------|------|----------------------------|
| Data lost | 0, 1, 9, 12 | words or a whole packet are missing from a subevent |
| Data corrupt | 3, 6, 8, 10, 11, 13, 14, 15 | a subevent's words are wrong or mis-framed (14: header tag != first ROC fragment tag; 15: record length or header byte count wrong) |
| Protocol / config | 2, 4, 5, 7 | peer misconfigured, credit counters desynced, FSM fault, stats rows untrustworthy (bit 7: bit 1 and the Rx BRAM rows must be ignored for that run) |

**2. MUST-BE-SET status -- readiness. Check before starting a run:**

| Bit | Name | Expected | If not |
|-----|------|----------|--------|
| 25 | DDR_CALIB_DONE | 1 | DDR not calibrated: nothing can be buffered; do not start |
| 24 | FRONTIER_VALID | 1 once the first peer packet has arrived (0 is normal before traffic) | after traffic started, 0 means no peer frame has ever been accepted: link, MAC, partition or start-node misconfiguration -- see bit 26 |

**3. INFORMATIONAL status -- log, never fail on.** Live back-pressure levels;
they flicker under load and only matter if one is stuck high while the event
counters stop advancing (then it names the blocked stage):

| Bit | Name | Stuck-high meaning |
|-----|------|--------------------|
| 16 | ROC_HELD | ROC input held: EVB back-pressuring the ring controller (downstream is one of 17-23) |
| 17 | SELF_THROTTLE | this DTC is more than 1024 tags ahead of its slowest peer -- a peer is not sending |
| 18 | CREDIT_THROTTLE | destination has no RX credit -- the peer's DMA (software) is not draining |
| 19 | DDR_ALMOST_FULL | DDR channel almost full |
| 20 | DDR_CDC_FULL | DDR write CDC FIFO full |
| 21 | STAGING_NO_SLOT | both staging FIFOs owned by other destinations |
| 22 | RX_BUF_HIGH | an RX source buffer >= 3/4 full -- THIS DTC's DMA is not draining |
| 23 | DMA_BACKPRESSURE | PCIe DMA not ready -- software is not reading fast enough |
| 26 | FOREIGN_FRAME | sticky: frames not for us were seen (shared switch, other partition) -- normal on a shared switch; suspicious on a direct cable |

Complement the register with the word-count zero-sum (section A) at the end of
each run: sender `ddr_to_tx` summed over DTCs must equal receiver `gbe_rx`
summed over DTCs, and per DTC `roc_input == self + gbe_ddr_fifo` and
`output == self + bufmgr`. Parity is the authoritative loss check; `0x9370`
tells which stage. Both are exact only with all DTCs SoftReset together before
the first event (counters wrap at 2^16 since 2026-09-10, compare mod 65536).

Pseudo-code:

```
v = read(0x9370)
if (v & 0xFFFF)            -> ERROR: run invalid, print bit names, require SoftReset
if !(v & (1<<25))          -> ERROR: DDR not calibrated, do not start
if running && !(v & (1<<24)) -> WARN: no peer traffic ever accepted
log((v >> 16) & 0x7FF)     -> informational levels (bits 16-26)
```

## C. 10GbE RX packet error count

| Address  | Width  | Counter                                                |
|----------|--------|--------------------------------------------------------|
| `0x9590` | 32-bit | Global 10GbE RX CRC/FCS mismatch errors (not per-DTC). **DEAD in EVB3 builds** (2026-09-14): the counter is driven only by the legacy EVB instance; in the EVB3 branch the wire is undriven and the register always reads 0. EVB3 does not verify the FCS of received frames at all (its RX CRC block is commented out), so a 0 here is not evidence of a clean link. Software should display it as N/A for EVB3 firmware |

Not clearable by software in the current RTL — resets only on hard/soft reset.

---

## D. BRAM debug signals (simulation / ILA)

Debug registers that latch BRAM access signals for observability.
Transaction-level signals are always present; `EVBSimMode`-only signals
are marked below.

### Port A — write side (`rxgbeclk`)

| Signal                       | Width | Description                                   |
|------------------------------|:-----:|-----------------------------------------------|
| `dbg_stats_bram_wr_we`      |   1   | Latched `statsBRAM_we` — pulses on each write |
| `dbg_stats_bram_wr_addr`    |   9   | Full BRAM address of last write               |
| `dbg_stats_bram_wr_data`    |  32   | Lower 32 bits of data written                 |
| `dbg_stats_bram_wr_type`    |   4   | `BRAM_TYPE` field extracted from address       |
| `dbg_stats_bram_wr_node`    |   5   | Node-offset field extracted from address       |

### Persistent per-node stat latches (always present — chipscope-visible)

Updated on every BRAM write that targets the matching type+node.

| Signal                       | Clock      | Indexed by   | Description                                    |
|------------------------------|------------|:------------:|------------------------------------------------|
| `dbg_rxIdleCnt[node]`       | `rxgbeclk` | source node  | RX idle packet count (from `BRAM_TYPE_RX_IDLE_COUNT`) |
| `dbg_rxCount[node]`         | `rxgbeclk` | source node  | RX packet count (from `BRAM_TYPE_RECV_COUNT`)  |
| `dbg_txIdleCnt[node]`       | `txgbeclk` | dest node    | TX idle packet count (from `BRAM_TYPE_TX_IDLE_COUNT`) |
| `dbg_txSeq[node]`           | `txgbeclk` | dest node    | TX sequence (from `BRAM_TYPE_TXSEQ`)           |

Arrays are `[MAX_NUM_OF_DTCS-1:0]` (32 entries). With 2 DTCs (base=0x80),
node offsets are 0 and 1.

### Chipscope ILAs for idle counts

| ILA instance             | Clock      | probe0 (64-bit)                         | probe4 (64-bit)                     |
|--------------------------|------------|----------------------------------------|-------------------------------------|
| `evb_tx_idle_cnt_ila`   | `txgbeclk` | `{txIdleCnt[1], txIdleCnt[0]}`         | `{txSeq[1], txSeq[0]}`             |
| `evb_rx_idle_cnt_ila`   | `rxgbeclk` | `{rxIdleCnt[1], rxIdleCnt[0]}`         | `{rxCount[1], rxCount[0]}`         |

In chipscope, add these ILAs and set the radix to unsigned decimal. The
values update live as the TX/RX FSMs write to the BRAM.

### Port B — TX write side (`txgbeclk`, `EVBSimMode`)

| Signal                          | Width | Description                                    |
|---------------------------------|:-----:|------------------------------------------------|
| `dbg_stats_bram_tx_wr_we`      |   1   | Latched `statsBRAM_tx_we`                      |
| `dbg_stats_bram_tx_wr_addr`    |   9   | Full BRAM address of last TX-side write        |
| `dbg_stats_bram_tx_wr_data`    |  32   | Lower 32 bits of TX-side data written          |
| `dbg_stats_bram_tx_wr_type`    |   4   | `BRAM_TYPE` field from TX write address        |

### Port B — software read side (`txgbeclk`, `EVBSimMode`)

| Signal                       | Width | Description                                       |
|------------------------------|:-----:|---------------------------------------------------|
| `dbg_stats_bram_rd_valid`    |   1   | High when read data is from user address (not TX FSM) |
| `dbg_stats_bram_rd_addr`     |   9   | Latched `EVBSourceStats_raddr_reg`                |
| `dbg_stats_bram_rd_data`     |  32   | Latched `statsBRAM_rdata` when valid              |
| `dbg_stats_bram_rd_type`     |   4   | `BRAM_TYPE` field from read address               |

---

## Pure-idle link test (2026-09-17)

Idle packets exercise exactly the hops under study (TX FSM, 10GbE, optional
switch, RX parser, stats BRAM) with nothing else in the loop: no ROC
emulator, RingController, AXIMux, DDR, buffer manager, DMA or software
parser. Every field is predictable (destination, source, length, per-
destination sequence, fixed payload) and the receiver already checks size
(bit 12), FCS (bit 13) and sequence (bit 1, RxMissingPktCnt). It does NOT
exercise the credit protocol, the RX source buffer or the header/data slip.

Knobs (all `0x91xx`): `0x9170` idle packets per window, `0x915C[31:16]`
idle payload words, `0x915C[15:8]` inter-packet gap, `0x9158[31:16]` dead
time, TX window length in CFO markers (build constant `EVB_TX_WINDOW_N`),
CFO-emulator marker period.

Procedure:
1. SoftReset both DTCs (clears `0x9370`, counters, sequence numbers). EVB
   link and HEB enabled; start no run. Idle packets flow on their own.
2. Write the point (`0x9170`, `0x915C`, `0x9158`) and clear the switch
   counters at the same time.
3. Run for a fixed time (at ~1 Mpps a minute is ~60 M frames per direction).
4. Freeze with TX disable only (`0x9114` bit 7 clear) on both DTCs. Never
   RX disable (bit 15 sweeps the stats BRAM).
5. Read per DTC: TxCount (type 0x9), TxIdleCount[dest], TxLastSeqTag[dest],
   RxCount[src], RxIdleCount[src], RxMissingPktCnt[src], RxByteCount[src],
   `0x9370`. Read the switch per-port in/out frames, CRC and discards.
6. Parity chain per direction; each equality localizes a hop:
   TxCount(A) = TxIdleCount(A->B) = switch in (A's port) = switch out (B's
   port) = RxCount(B<-A) = RxIdleCount(B<-A), RxMissingPktCnt = 0, bits
   1/7/11/12/13 clear. RxCount/RxByteCount must be read RAW, not in K.
7. Sweeps: payload words at fixed rate (length selectivity; the calo-14
   loss was 186-word frames), then rate at fixed size (`0x9170`, gap, dead
   time, marker period) for the maximum clean packet and byte rate. Repeat
   each point through the switch and on the direct cable.

Rate arithmetic: one idle frame occupies (2 + words + 2) beats + gap
beats of txgbeclk (156.25 MHz). With 186 words and gap 12: ~202 beats ->
~0.77 Mpps -> 1.19 GB/s of frame; a window of N markers holds
N x marker_period / 202 frames, so set `0x9170` at or above that to run the
whole window back-to-back.

## RTL source references

| File | What to find there |
|------|--------------------|
| `EVB3.v:261–268`        | `BRAM_TYPE` localparams                          |
| `EVB3.v:306–317`        | Word counter declarations and `EVBWordCount` assigns |
| `EVB3.v:590–605`        | BRAM port-B readback gating (TX FSM sharing)     |
| `EVB3.v:830–982`        | RX FSM per-source BRAM writes                    |
| `EVB3.v:1151–1221`      | Word counter increment logic                     |
| `EVB3.v:1340–1604`      | TX FSM per-destination BRAM access               |
| `register_map.v:2140`   | `EVB_STATS` write decode (address latch)         |
| `register_map.v:~3380`  | `EVB_STATS` read decode                          |
| `register_map.v:~3437, ~3790` | `EVB_WORD_COUNT_0..3` read decode           |
| `EVB.v:1478–1505`       | `rxPacketErrorCount` logic                       |
| `register_map.v:3612`   | `TENGBERXPACKETERRORCOUNT` read decode           |

## Self-subevent throttle (2026-09-04)

Firmware bounds how far the local (self) subevent stream can run ahead of the
peers: a self subevent whose EWT[15:0] leads the slowest peer's latest received
tag by more than 1024 is held (ROC tready low) until the peers catch up. Software
staging memory per DTC is therefore bounded by 1024 x max subevent (192 KB) =
192 MB regardless of the number of DTCs. No register; fixed window. A dead peer
stalls this DTC after 1024 events by design (counters freeze). Details in
[EVB3_counters_and_protocol.md](EVB3_counters_and_protocol.md), "Self-Subevent
Throttle".
