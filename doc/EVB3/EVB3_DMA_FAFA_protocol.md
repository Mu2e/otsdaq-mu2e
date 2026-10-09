# EVB3 PCIe DMA Transfer Protocol: FAFA Chunk Framing (Software Guide)

This document specifies how the EVB3 event builder frames data on the PCIe DMA
(C2S channel 0) stream and how software should extract it. For firmware
internals (counters, clock domains, buffer manager design), see
[EVB3_counters_and_protocol.md](EVB3_counters_and_protocol.md).

## Overview

All event data reaching PCIe — whether from this DTC's own ROCs ("local") or
received from other DTCs over 10GbE ("remote") — flows through the EVB2 buffer
manager, which frames it into **chunks**. Each chunk is preceded by a **FAFA
header word** that tells software the source DTC and the exact size of the data
that follows. Multiple chunks are **bundled** into one DMA transfer while
traffic is flowing; every DMA transfer ends with a single all-ones **filler
word** that carries `tlast` to close the DMA.

```
one DMA buffer:
+--------+-----------------+--------+-----------------+     +-------------------+
| FAFA 1 | chunk 1 data    | FAFA 2 | chunk 2 data    | ... | 0xFFFF..FF filler |
| header | (wc1 words)     | header | (wc2 words)     |     | (tlast, close)    |
+--------+-----------------+--------+-----------------+     +-------------------+
```

## FAFA header word format

Every chunk begins with one 64-bit protocol word:

| Bits    | Field         | Meaning                                              |
|---------|---------------|------------------------------------------------------|
| [63:48] | `16'hFAFA`    | magic                                                |
| [47:24] | `24'h0`       | reserved                                             |
| [23:8]  | `chunk_wc`    | exact count of 8-byte data words following, this chunk |
| [7:0]   | `chunk_src`   | source DTC IP (last octet)                           |

- Local chunks: `chunk_src` = this DTC's IP (`SelfDTC_ip`).
- Remote chunks: `chunk_src` = `EVBBaseNode + source offset` (the sending DTC's IP).

`chunk_wc` is known by the firmware **before** the chunk is sent — it is never a
running count:

- **Local chunk**: derived from the count quadword that is the first word of
  every record/subevent on the ROC stream: `{…, packets[14:0], 4'h8}` from the
  AXIMux, i.e. a byte count that INCLUDES the count word itself (low nibble
  always 8; 0x198 = 408 bytes = 51 words for the 51-word HW subevent), so
  `record_words = field >> 3` with no +1 (corrected 2026-09-08; the old
  "excludes the header, +1" wording was wrong and a +1 hangs CHUNK_DATA).
  Since 2026-09-08 a local record is SPLIT when it does not fit
  `DMA_max_size`: `chunk_wc = min(record_words_remaining, DMA_max_words - 2)`
  and the remainder follows as continuation chunks with the same FAFA framing
  (src = self). A local chunk is therefore no longer guaranteed to start with
  a record header; reassemble the self source exactly like a remote one.
  Since 2026-10-06 a local chunk is ALSO cut at register `0x9178` words
  (`EVB_LocalChunkCap`, reset 1024; 0 = whole record as before):
  `chunk_wc = min(record_words_remaining, 0x9178, DMA_max_words - 2)`, the
  rest follows as continuation chunks exactly as above. So in normal running a
  96 kB self record arrives as ~12 self chunks of 1024 words instead of one of
  8189 and one of 3829. Same framing, same reassembly rule, nothing else to
  change in software. Why: while one self chunk streams, no remote source FIFO
  is drained; a 33 us self chunk made the peer run out of credit for most of
  every self record (2-DTC stand: ~770 MB/s per DTC vs 1.73 GB/s direct mode).
- **Remote chunk**: a snapshot of the source FIFO's read-side word count at
  service time, capped at `DMA_max_words - 2`. Exactly that many words are
  sent — whatever remains becomes a *new* chunk with its own FAFA header.
  Since 2026-10-07 a remote source is granted only once its FIFO holds at
  least register `0x917C[15:0]` words (reset 128), or it has waited
  `0x917C[31:16]` clocks (reset 1024 = ~4 us) with words in it; so remote
  chunks are normally 128 words or more
  instead of whatever had arrived (11-19 words on average before). Same
  framing, same reassembly; just fewer, larger chunks.
- **Record size bound**: a record is one AXIMux DMA transfer. The AXIMux
  splits any aggregate larger than `DMA_max_packetcount = DMA_max_size[15:4]
  - 1` packets into several transfers, each with its own count quadword
  (`AXIMuxFromRingsData.v` in its per-subevent mode, which 0x9114 bit 7 = 1
  selects; "need to do multiple DMAs"), so a record
  is at most `DMA_max_size - 8` bytes and, with the 16-bit register, never
  more than 65,528 B = 8191 words. EVB3's `[15:3]` word fields and a 16-bit
  software mask are therefore exact. A large subevent (e.g. 192 KB) simply
  arrives as multiple records, each starting with a count quadword.
- **Split-record layout (confirmed 2026-10-05 for the software reassembly):**
  the FIRST record of a subevent is `count quadword, subevent header words,
  ROC blocks...` up to its count; every CONTINUATION record is `count quadword,
  the next raw words of the subevent` from exactly where the previous record
  stopped -- no repeated subevent header, no gap, no filler (the AXIMux pauses
  at the max count, closes the transfer, and resumes with the word it was
  holding). The payloads (each record's count minus 8 bytes) add up EXACTLY to
  the subevent's inclusive byte count in header word 0. Worked example from
  calo-14 (6 ROCs x 1000 packets, `DMA_max_size` = 0xFFF8, see the register
  note below): subevent 96,144 B;
  first record count 0xFFE8 = 65,512 B (payload 65,504); second record count
  0x77B8 = 30,648 B (payload 30,640); 65,504 + 30,640 = 96,144. The mux trims
  the first record by `DMA_min_size` when the remainder would otherwise be
  shorter than the minimum DMA, so a first record can also read
  `DMA_max_size - DMA_min_size - 8`; the sum is still exact. Reassembly rule per
  source: append record payloads to the open subevent until the sum equals the
  header's inclusive byte count; the next record starts a new subevent.
- **The two sizes that follow from `0x9104[31:16]` (2026-10-05):** the ring mux
  makes a first record of `((max >> 4) - 1) x 16 + 8` bytes (0xFFE8 for any max
  from 0xFFF0 to 0xFFFF, so the record size alone does not reveal the low
  nibble), and EVB3 caps a chunk at `(max >> 3) - 2` words: 8189 with 0xFFF8,
  8188 with 0xFFF0. With 0xFFF8 a whole 65,512-byte first record (8189 words
  including its count word) goes out as ONE chunk of exactly the cap; with
  0xFFF0 it would be cut into an 8188-word chunk plus a 1-word continuation
  chunk. Software must set this register itself: its firmware reset value is
  0x8000 (first records of 0x7FF8 bytes), and it keeps whatever was last
  written through SoftResets -- only a hard reset (0x9100 bit 0) or a bitfile
  load returns it to 0x8000. calo-14 2026-10-04/05 ran on 0xFFF8 left behind
  by a test program. A chunk header claiming more words than remain in the DMA
  buffer only happens when the stream was stopped mid-run (TX disable or reset);
  the reader may treat it as end of run.

## Two DMA formats, selected by 0x9114 bit 7 (since 2026-10-05)

One bitfile serves both software and hardware event building. With bit 7 = 0
(EVB link off) EVB3 is a bare passthrough and the PCIe data channel carries the
**non-EVB format**: subevent records (count quadword + subevent header + ROC
blocks) stacked back to back in one DMA transfer, closed by one all-ones word
with tlast when the next record would not fit the 0x9104 max, when the stacked
packet count reaches it, or after 200 us with no new record. No FAFA headers.
With bit 7 = 1 the ring mux sends one record + one tlast word per subevent into
EVB3 and software sees the FAFA format described in the rest of this document.
Change bit 7 only with the data path idle (data requests stopped, > 200 us), or
follow it with a SoftReset.

## DMA transfer framing (bundling)

Chunks from any mix of sources are stacked into one DMA transfer (the same
scheme the ring mux uses in its bundled mode; with 0x9114 bit 7 = 1 the ring
mux is in per-subevent mode and does no bundling, so the buffer manager is the
only bundler in the HEB path). The DMA is closed by emitting one all-ones
(`64'hFFFFFFFF_FFFFFFFF`) filler word carrying `tlast` when either:

1. a pending chunk would no longer fit under the `DMA_max_size` byte limit
   (chunks never straddle a DMA boundary), or
2. the **200 µs bundling timeout** expires with buffered data and no new chunk
   available — this closes off a smaller DMA so sparse traffic is not held
   hostage.

No chunk can exceed `DMA_max_size` (since 2026-09-08): a chunk that does not
fit an empty DMA is cut to `DMA_max_words - 2` and the rest follows as further
chunks; the old "sent anyway" grant and its oversize flag are gone. The
firmware requires `DMA_max_size` >= 24 bytes; with the port unconnected (0) the
first non-fitting grant took 0xFFFE words (HW 2026-09-09, fixed in
`compile/DTC.v`). Software sets `DMA_max_size` through 0x9104[31:16].

After reset, the firmware emits one close filler unconditionally to terminate
any DMA left open across the reset; software may see a 1-word all-ones DMA
first and should discard it (the walk algorithm below handles this naturally).

## Software extraction algorithm

For each received DMA buffer:

```
ptr = 0
while ptr < dma_words:
    w = buf[ptr]
    if w[63:48] == 0xFAFA:
        wc  = w[23:8]
        src = w[7:0]
        append buf[ptr+1 .. ptr+wc] to reassembly_stream[src]
        ptr += 1 + wc
    else:
        # not a FAFA header: this is the all-ones DMA-close filler (end of
        # useful data in this DMA) — discard the remainder
        break
```

Key properties software can rely on:

- A DMA buffer always starts with a FAFA header (or the filler, for the
  post-reset close case).
- `chunk_wc` is exact; the word after the last chunk-data word is either the
  next FAFA header or the close filler.
- Chunks from the same `src` arrive in order; appending them per-src exactly
  reconstructs that source's data stream.
- **Chunk boundaries carry no event semantics.** A subevent fragmented across
  ethernet packets arrives as multiple remote chunks; subevent boundaries come
  from the subevent headers *inside* each reassembled per-src stream, not from
  the chunk framing.
- FAFA headers and the close filler are protocol overhead: they are excluded
  from all `wc_*` diagnostic counters (registers 0x9200-0x920C) and are not
  covered by any `chunk_wc`.

## Debug visibility

- `dbg_pcie_transfer_src` / `dbg_pcie_transfer_wc` (EVB3, user_clk) latch the
  `src` and `wc` fields of each FAFA header as it passes the m_axis stage;
  visible in the `evb_pcie_transfer_ila` (ila_119) alongside
  `wc_output_stream` and the `m_axis_is_header` strobe.
- The buffer manager `evb_buf_rd_ila` (ila_51) probe3 exposes the chunk FSM:
  state, `chunk_src`, `chunk_is_local`, `bundled_has_data`, `need_close`,
  `data_out_chunk_last`.
