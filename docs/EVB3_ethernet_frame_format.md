# EVB3 10GbE Frame Format (for a Wireshark dissector)

What a DTC puts on the 10 Gb Ethernet link between event-builder nodes, byte by byte, so a
Wireshark dissector (Lua or C) can be written from this page alone. Written for a network
engineer; no FPGA knowledge assumed. Firmware source of truth: `EVB3.v`, TX states 'h04 and
'h05 (MAC/count words), 'h1C (header word 3), 'h06 (data payload), 'h13 (idle payload),
'h07/'h14 (FCS).

The question this answers: **are the DTC packets documented well enough to build a dissector
from?** Yes, below. Everything a capture shows is either a plain Ethernet frame with a custom
body made of 8-byte words (this document), or the switch's own traffic (ARP, LLDP, STP),
which the DTCs ignore.

> **History note:** a 2026-09-28 development layout placed the retransmission fields inside
> the MAC address bytes (offsets 0-1, 6-7, 9-10). That was caught 2026-09-30 while writing this
> document and fixed the same day by moving the fields to a third 8-byte header word
> (Section 3.4). No bitfile with the bad layout ever ran on hardware. Bitfiles built before
> 2026-09-28 have neither the fields nor header word 3: their payload starts at offset 16 and
> the byte-count field is `8N + 2` (Section 9 tells the two layouts apart).

## 1. What the link is

- 10GBASE-R, 64b/66b, one lane, full duplex, through a commercial switch (FS S5850) or a
  direct cable. Standard Ethernet framing: preamble and SFD, 6-byte destination MAC, 6-byte
  source MAC, body, 4-byte FCS (standard CRC-32).
- **There is no EtherType and no IP.** The two bytes where an EtherType would sit hold the
  DTC byte-count field, whose value equals the Ethernet payload length (Section 3.2).
  Wireshark will therefore show every frame as IEEE 802.3 with a length field and try to
  decode the next two bytes as LLC. That is expected; the dissector must hook in as a
  heuristic on the Ethernet layer keyed on the MAC prefix (Section 8), not on an EtherType.
- The DTC builds frames one 64-bit word per clock, so everything after the 12 MAC bytes is
  a whole number of 8-byte words plus the 4 header bytes at offsets 12-15.
- Frame length without FCS = `24 + 8 * N` bytes, N = payload words: 16 bytes of MAC, length,
  seq and flags, 8 bytes of header word 3, then the payload. Minimum N = 12 (a data frame
  with fewer words is zero-padded to 12 on the wire, Section 5), so the shortest frame is
  120 bytes + FCS. Maximum N = 186 (`MAX_PACKET_BYTES` = 1492), 1512 bytes + FCS.

## 2. Addressing

MAC address scheme, both directions, six bytes as they appear on the wire (first byte
first):

```
00 : 00 : PP : 00 : 00 : NN
```

| byte | meaning |
|---|---|
| `PP` | partition ID. All DTCs in one event-building group share it. Bench value `0x99`. |
| `NN` | node number = base node + offset, offset 0..31. Bench values `0x80` (DTC_0) and `0x81` (DTC_1). |
| the other four | zero |

A DTC sends only to peers in its own partition. Software sets `PP`, `NN`, the base node and
the node count (registers `0x9154`, `0x9158`; see
[EVB3_software_status_registers.md](EVB3_software_status_registers.md)).

The switch forwards by exact 6-byte lookup, so the destination MAC a DTC sends must equal
the peer's source MAC byte for byte. On the bench the two addresses are entered as static
MAC-table entries (`00:00:99:00:00:80` on port eth-0-31, `00:00:99:00:00:81` on eth-0-32).

## 3. Frame layout

Offsets are from the first byte of the destination MAC (Wireshark's Ethernet layer offset
0). The firmware builds the frame as 64-bit words; "byte 7" of a word is the first byte on
the wire and "byte 0" the last. (Evidence: the firmware writes the dest word as
`{00, 00, PP, 00, 00, NN, ..}` and the switch's MAC table shows `00:00:PP:00:00:NN`.)

```
offset  len  field                          firmware word / byte
------  ---  -----------------------------  ---------------------
  0      6   destination MAC                word 1 bytes 7..2
  6      6   source MAC                     word 1 bytes 1,0 + word 2 bytes 7..4
 12      2   byte count (big-endian)        word 2 bytes 3,2
 14      1   sequence number                word 2 byte 1
 15      1   flags                          word 2 byte 0
 16      8   header word 3: retransmission  word 3 (Section 3.4)
 24     8N   payload: N words of 8 bytes    words 4..N+3
 24+8N   4   FCS
```

### 3.1 Header word 1 (offsets 0-7)

| wire offset | word byte | contents |
|---|---|---|
| 0 | 7 | `0x00` (dest MAC byte 0) |
| 1 | 6 | `0x00` (dest MAC byte 1) |
| 2 | 5 | `PP` partition (dest MAC byte 2) |
| 3 | 4 | `0x00` |
| 4 | 3 | `0x00` |
| 5 | 2 | `NN` destination node (dest MAC byte 5) |
| 6 | 1 | `0x00` (source MAC byte 0) |
| 7 | 0 | `0x00` (source MAC byte 1) |

### 3.2 Header word 2 (offsets 8-15)

| wire offset | word byte | contents |
|---|---|---|
| 8 | 7 | `PP` partition (source MAC byte 2) |
| 9 | 6 | `0x00` (source MAC byte 3) |
| 10 | 5 | `0x00` (source MAC byte 4) |
| 11 | 4 | `SS` source node (source MAC byte 5) |
| 12 | 3 | byte count `[15:8]` |
| 13 | 2 | byte count `[7:0]` |
| 14 | 1 | `seq`, 8-bit sequence number, one counter per (source, destination) pair |
| 15 | 0 | flags, Section 3.3 |

**Byte count** (offsets 12-13, big-endian) = `8 * (N + 1) + 2`. The firmware value is
`{0, N+1, 010}`: bit 15 is 0, bits `[14:3]` are the payload word count plus one (the one is
header word 3), bits `[2:0]` are `010` for the seq and flags bytes. The field therefore equals
the number of bytes that follow it up to the FCS, which is exactly the IEEE 802.3 length
definition. Payload words `N = (byte_count >> 3) - 1`.

### 3.3 Flags byte (offset 15)

| bits | name | meaning |
|---|---|---|
| 7:3 | `drained_credit` | how many 32-word units the sender's receive FIFO for *this destination's* stream has handed to the host so far, modulo 32. Flow-control feedback in the reverse direction: the receiver of this frame uses it as its send credit toward this sender. |
| 2 | `has_data` | 1 = data frame (payload is subevent records, Section 5); 0 = idle frame (payload is the idle body, Section 4). |
| 1 | `first` | first frame of a burst (a new subevent starts in this frame). |
| 0 | `last` | last frame of a burst. A single-frame subevent has both set. |

### 3.4 Header word 3: retransmission fields (offsets 16-23)

One 64-bit word, big-endian on the wire (bit 63 = first bit of offset 16):

| bits | offsets | field | meaning |
|---|---|---|---|
| 63:44 | 16, 17, 18 high nibble | `pkt_pos` (20) | stream offset of this frame's first payload word: the number of payload words the sender has sent to this destination before it, mod 2^20, counted from the common reset. An idle frame carries the offset the next data word will have. Describes this frame's own contents. (Until 2026-10-01 this was the sender's DDR address `{burst, word}`; never on hardware.) |
| 43:41 | 18 | 0 | |
| 40 | 18 bit 0 | `resend_req` | feedback about the *reverse* stream: "I am discarding your frames and want you to resend from `ack_pos`". Repeated in every frame until cleared. |
| 39:20 | 19, 20, 21 high nibble | `ack_pos` (20) | feedback: stream offset of the first word of *your* stream I have not stored (= the last stored frame's `pkt_pos` + its payload words). |
| 19:12 | 21 low nibble, 22 high nibble | `last_seq_seen` (8) | feedback: the `seq` of the last frame I had received from *you* when this window started (2026-10-01). Lets you tell whether a repeated `resend_req` was written after your answer reached me. |
| 11:0 | 22 low nibble, 23 | 0 | |

`pkt_pos` describes the frame's own contents. `resend_req` and `ack_pos` describe the reverse
direction (what the sender of this frame has received from the frame's destination); label
them "feedback to peer" in the dissector.

Protocol behaviour, for a conversation view: on a sequence gap the receiver raises
`resend_req` toward that source and discards from the gap-revealing frame on until the resend
arrives; the source re-sends
from `ack_pos` at its next window to that destination. The resend's `pkt_pos` equals the
`ack_pos` that asked for it; when the lost frame was an idle (or nothing had been sent past the
ack yet) the answer is the next frame, data or idle, again with `pkt_pos == ack_pos`. In a
healthy stream every frame's `pkt_pos` equals the previous data frame's `pkt_pos` + payload words
(a dissector can check this per conversation). A request still standing in a header whose
`last_seq_seen` is at or past the answer's `seq` is served again (the answer itself was lost);
one whose echo is behind the answer was written before the answer could arrive and is the same
request, not a new one. Full
rules in [EVB3_counters_and_protocol.md](EVB3_counters_and_protocol.md), section "Packet
Retransmission".

Why a separate word: the first 12 bytes are the MAC addresses. A commercial switch forwards
by exact 6-byte lookup and learns source addresses, so nothing may vary there.

## 4. Idle frame body (`has_data` = 0)

The body is `EVB_IdlePacketWords` copies of one 64-bit word (register `0x915C[31:16]`,
default 12 words = 96 bytes). Read the first word (offsets 24-31) as a 64-bit big-endian
value V:

```
txTime        = (V >> 23) & 0xFFFFFFFF   sender's free-running 32-bit timestamp, 156.25 MHz ticks
words_queued  = (V >>  8) & 0xFFF        payload words the sender has queued for this destination (0 in a pure idle)
seq           =  V        & 0xFF         same sequence number as offset 14
bits 63..55 are zero
```

(The firmware concatenates `{txTime[31:0], 3'b0, words[11:0], seq[7:0]}` = 55 bits into a
64-bit word, hence the odd alignment. The receiving firmware reads `txTime` from bits 63:32
for its "TravelTime" statistic, which does not match the sender's placement; that statistic
is informational only and is on the firmware team's list.)

Only the first word matters; the copies pad the frame to the minimum size.

## 5. Data frame body (`has_data` = 1)

N words of subevent data, copied verbatim from the sender's DDR ring, N from the byte-count
field. If N < 12, zero words follow up to 12 (runt avoidance); those pad words are not in
the byte count and the receiver does not store them. Then FCS.

The payload is a byte-exact slice of the sender's stream of **subevent records** for this
destination. A record may span several frames (`first`/`last` flags) and a frame may hold
the tail of one record and the head of the next, so record boundaries are only visible in
a reassembled per-(source, destination) stream, not in a single frame. The record format,
for that reassembly:

### 5.1 Record = count word + DTC subevent header + ROC data

Every record starts with a **count word**:

```
[15:3]  record_words   total 8-byte words in this record INCLUDING this word
[2:0]   000            (the field is a byte count with low nibble 8; words = field >> 3)
[63:16] not used by the receiver
```

A record head whose low nibble is not 8 is corrupt (the receiving firmware flags it).

Then the **DTC subevent header**, 6 words, per the Mu2e DAQ subevent header figure. Word 0:

```
[63:32] Event Window Tag [31:0]
[31:24] reserved
[23:0]  inclusive subevent byte count (packets x 16)
```

Word 1: `[63:24]` event mode (5 bytes), `[23:16]` number of ROCs, `[15:0]` Event Window Tag
`[47:32]`. Words 2-5: subsystem / DTC ID / partition / EVB mode, per-link status bytes, Linux
time, per-link DRP latencies (exact positions in the comment block near line 490 of
`AXIMuxFromRingsData.v`; not needed for frame-level dissection).

Then ROC **Data Header Packets** and their data in the standard Mu2e 16-byte packet format
(type 0x5 at bits `[23:20]` of the packet's first word, byte count at `[15:0]`, Event Window
Tag across `[63:48]` and the next word).

**What the receiving DTC does with it:** strips header word 3, stores the payload words per
source in a FIFO without looking inside, and hands them to the host DMA framed by FAFA chunk
headers ([EVB3_DMA_FAFA_protocol.md](EVB3_DMA_FAFA_protocol.md)). FAFA headers are **not** on
the wire; a capture never shows them.

## 6. FCS

Standard Ethernet CRC-32 over offsets 0 through the last body byte, including header word 3
and any runt padding. The firmware emits it in a 64b/66b terminate block as the four data
octets; on the wire it is an ordinary FCS. Wireshark's built-in check applies (enable
"Assume packets have FCS" in the Ethernet protocol preferences if the capture NIC keeps it).

## 7. Timing you will see in a capture

- A sender transmits in **destination windows**: for a fixed number of CFO clock-marker
  periods (a few microseconds) it sends only to one destination, then pauses for a dead
  time (`0x9158[31:16]` clocks at 156.25 MHz), then moves to the next destination. With 6
  DTCs a full rotation is about 33 us in simulation. A capture at one receiver therefore
  shows a burst from one source, a gap, a burst from the next.
- Inter-frame gap: `EVB_InterpacketGap` (`0x915C[15:8]`) idle clocks, default 12.
- Every window carries at least one frame to its destination (an idle if there is no data),
  so each (source, destination) pair produces at least one frame per rotation, and `seq`
  increments by one per frame per pair. A `seq` jump is a lost frame.
- The bench switch drops about 1 in 2000-5000 maximum-size frames with no counter charged.
  That is why the retransmission fields exist and why this document was written: a capture
  at both switch ports, dissected, shows whether a given frame ever left the switch.

## 8. Dissector checklist

Hook: heuristic dissector on `eth`, accept when `eth.dst[0:5]` or `eth.src[0:5]` matches
`00:00:PP:00:00` with `PP` a preference (bench `0x99`). Do not rely on the 802.3 length
path; Wireshark will otherwise hand offsets 14-15 to LLC.

Fields (suggested names, all offsets from the destination MAC):

```
evb.dst_partition   uint8    offset 2
evb.dst_node        uint8    offset 5
evb.src_partition   uint8    offset 8
evb.src_node        uint8    offset 11
evb.byte_count      uint16   offsets 12-13, big-endian
evb.payload_words   computed (byte_count >> 3) - 1        (header word 3 is counted in the field)
evb.seq             uint8    offset 14
evb.credit          uint5    offset 15 bits 7:3
evb.has_data        bool     offset 15 bit 2
evb.first           bool     offset 15 bit 1
evb.last            bool     offset 15 bit 0
-- header word 3, W = 64-bit big-endian at offsets 16-23:
evb.rt.pkt_pos      uint20   (W >> 44) & 0xFFFFF
evb.rt.resend_req   bool     (W >> 40) & 1
evb.rt.ack_pos      uint20   (W >> 20) & 0xFFFFF
evb.rt.last_seq_seen uint8   (W >> 12) & 0xFF         (seq of the last frame received from this frame's destination)
-- idle body (has_data == 0), V = 64-bit big-endian at offsets 24-31:
evb.idle.txtime        uint32  (V >> 23) & 0xFFFFFFFF
evb.idle.words_queued  uint12  (V >> 8) & 0xFFF
evb.idle.seq           uint8   V & 0xFF
-- data body, first frame of a burst only (has_data == 1 and first == 1):
evb.record.words    uint13   offsets 24-31 as big-endian, bits [15:3]
evb.record.ewt_lo   uint32   offsets 32-35 (subevent header word 0 bits 63:32)
```

Expert infos worth adding: partition byte differs between source and destination;
destination node outside the configured range; `seq` gap per (source, destination) pair
(a lost frame); nonzero bytes at offsets 0-1, 3-4, 6-7, 9-10 (a mis-addressed frame; a
switch would flood it); `resend_req` set (informational: the peer is asking for a resend);
frame length not `24 + 8N`; N > 186; FCS bad; `pkt_pos` not equal to the previous data
frame's `pkt_pos` + payload words in the same conversation (a resend or a lost frame).

Conversation key: `(src_node, dst_node)`. Sequence numbers, credit and positions are all
per pair; the reverse pair is a separate conversation.

## 9. Caveats

- Bit positions come from the header-build states in `EVB3.v` and the receive parse, and
  the wire byte order from the switch's learned MAC table matching the firmware's word
  layout with byte 7 first. If a capture disagrees with this document, the capture wins;
  please report the disagreement.
- The link uses two 64b/66b start-block alignments (0x78 and 0x33) that shift the frame by
  4 bytes inside the 66-bit blocks. This is below the PHY; a NIC capture shows ordinary
  frames either way.
- Header word 3 exists only on the retransmission development branch (2026-09-30; sim PASS
  2026-10-01, run 30) and has not been on hardware yet. Every bitfile on the bench so far has no word 3: payload at
  offset 16, byte count `8N + 2`. Both layouts put `010` in the field's low bits, so tell them
  apart by a dissector preference or by the frame length: `16 + 8N` old, `24 + 8N` new, with
  N from the byte-count field.
- The receiving firmware's `txTime` read (Section 4) does not match the sender's field
  placement; the "TravelTime" statistic it feeds is informational and this is being
  reported to the firmware team separately.
