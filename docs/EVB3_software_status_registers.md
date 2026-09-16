# EVB3 Software Status Registers

How to read event-building counters and per-DTC 10GbE statistics from the DTC.

## Configuration registers

| Address  | Field     | Purpose                                           |
|----------|-----------|---------------------------------------------------|
| `0x9154` | `[7:0]`   | This DTC's node ID (`SelfDTC_ip`)                 |
| `0x9158` | `[15:8]`  | `EVBStartNode` — base node for per-DTC offset math|
| `0x9158` | `[7:0]`   | `EVBNumNodes` (clamped 1–128)                     |

`DTC_offset = node_MAC − EVBStartNode`, range 0–31.

---

## A. Global pipeline word counters (direct read)

16-bit saturating counters, packed two per 32-bit register. Reset on soft/hard
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
| 1 | RX_SEQ_GAP | RX sequence gap -- a packet was lost on the wire (count per source in RxMissingPktCnt) |
| 2 | RX_PKT_REJECTED | An EVB frame addressed to THIS DTC was not stored because its source offset (source MAC byte - start node) is > 31: a peer is misconfigured or the start node is wrong. Frames not addressed to this DTC (another DTC, another partition, non-EVB start) are status bit 26, not an error (changed 2026-09-09: HW showed switch-flooded idle packets from a partition-0x01 DTC to MAC 0x00 on both DTCs) |
| 3 | DDR_WR_UNDERFLOW | DDR write CDC FIFO went empty inside a burst -- corrupt burst |
| 4 | TX_FSM_FAULT | TX FSM undefined state / destination offset > 31 |
| 5 | CREDIT_VIOLATION | sent/drained credit counters disagree by more than the credit (one-sided reset) |
| 6 | LOCAL_BAD_HEADER | the local (self) stream was misaligned: the word at a record boundary was not a count quadword (low nibble 8, non-zero words). Nothing is dropped -- the word goes out as a 1-word chunk and framing re-syncs at the next real header -- but a word was lost or duplicated upstream of the buffer manager, so the affected local record is corrupt. Never observed so far. (Until 2026-09-08 this bit meant chunk > DMA_max_size; it fired on both DTCs in every HW run before 2026-09-09 only because DMA_max_size was unconnected in the HW EVB3 instance and synthesized to 0, not because of any misalignment) |
| 7 | RX_STATS_COLLISION | RX stats pipeline collision -- per-source stats may be wrong, data was stored |
| 8 | DDR_RD_BAD_COUNT | DDR read stream desync: a non-count word arrived where a subevent count quadword was due; it was dropped and the stream re-synced at the next valid count |
| 9 | STAGING_WRITE_FULL | DDR->TX staging FIFO written while full -- words lost, TX then under-runs the declared packet |
| 10 | TX_PAYLOAD_UNDERFLOW | staging FIFO went empty inside a packet payload -- the wire repeated a word |
| 11-15 | reserved | 0 |

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

## C. 10GbE RX packet error count

| Address  | Width  | Counter                                                |
|----------|--------|--------------------------------------------------------|
| `0x9590` | 32-bit | Global 10GbE RX CRC/FCS mismatch errors (not per-DTC) |

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
