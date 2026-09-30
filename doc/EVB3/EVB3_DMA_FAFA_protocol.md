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
- **Remote chunk**: a snapshot of the source FIFO's read-side word count at
  service time, capped at `DMA_max_words - 2`. Exactly that many words are
  sent — whatever remains becomes a *new* chunk with its own FAFA header.
- **Record size bound**: a record is one AXIMux DMA transfer. The AXIMux
  splits any aggregate larger than `DMA_max_packetcount = DMA_max_size[15:4]
  - 1` packets into several transfers, each with its own count quadword
  (plain `AXIMuxFromRingsData.v`, the file EVB builds compile; "need to do
  multiple DMAs"), so a record
  is at most `DMA_max_size - 8` bytes and, with the 16-bit register, never
  more than 65,528 B = 8191 words. EVB3's `[15:3]` word fields and a 16-bit
  software mask are therefore exact. A large subevent (e.g. 192 KB) simply
  arrives as multiple records, each starting with a count quadword.

## DMA transfer framing (bundling)

Chunks from any mix of sources are stacked into one DMA transfer (scheme
borrowed from `AXIMuxFromRingsData_bundled.v`; EVB builds compile the plain
`AXIMuxFromRingsData.v`, which does no bundling, so the buffer manager is the
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
