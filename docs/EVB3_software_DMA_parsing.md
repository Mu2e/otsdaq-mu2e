# EVB3 Software DMA Parsing: Current Understanding and Proposed Event-Completion Model

Software-side companion to [EVB3_DMA_FAFA_protocol.md](EVB3_DMA_FAFA_protocol.md).
That document specifies what the firmware puts on the PCIe DMA stream. This one
records what software has *observed* on the 2-DTC bench, how the current parser
(`DTCLib::DTC::GetEVBDataAsEvents`, mu2e-pcie-utils `dtcInterfaceLib/DTC.cpp`)
handles it, and the event-completion / timeout model we propose to implement.
It is written to be discussed with the firmware side; every claim is marked as
either **observed** (seen in a DMA dump) or **assumed** (needs confirmation).

Bench: DTC_0 (MAC 0x80) and DTC_1 (MAC 0x81), `EVBStartNode` = 0x80, N = 2
destination nodes, 6 ROC links per DTC (ROC0 real payload, ROC1–5 header-only).
Firmware under test: 26.09.03.18 and its 2026-09-04 successor.

---

## 1. What a record looks like on the DMA (observed)

After stripping the FAFA chunk header, each per-source reassembled stream is a
sequence of **records**. One record = one DTC subevent for one event window tag.

```
word  byte   content
 0    +0     firmware record header word      bits[15:0] = total record bytes (incl. this word)
 1    +8     DTC_SubEventHeader qword 0       inclusive_subevent_byte_count[24:0], event_tag_low[63:32]
 2    +16    DTC_SubEventHeader qword 1       event_tag_high[15:0], num_rocs[23:16], event_mode[63:24]
 3    +24    DTC_SubEventHeader qword 2       dtc_mac, partition_id, evb_mode, source_dtc_id, link subsystems
 4    +32    DTC_SubEventHeader qword 3       link0..5_status, subevent_format_version, emtdc
 5    +40    DTC_SubEventHeader qword 4       link4/5 DRP rx latency
 6    +48    DTC_SubEventHeader qword 5       link0..3 DRP rx latency
 7    +56    ROC0 DataHeader packet word 0
 8    +64    ROC0 DataHeader packet word 1
 9..  +72..  ROC0 payload (32 words on the bench)
      ...    ROC1..5 DataHeader packets (2 words each, header-only on the bench)
```

Observed values on the bench: record header `0x198` (408 bytes) and subevent
inclusive count `0x190` (400 bytes). **Observed relation: record header bytes =
subevent inclusive count + 8.** This is 51 words for 0x198. Software enforces
`record_bytes == inclusive_count + 8` as a consistency check. The total record
length does not imply a chunk length: records can span multiple FAFA chunks.

The record header word is present on **both** the local (self) path and the
remote (10GbE) path. Software skips it uniformly for all sources. (Observed: an
earlier attempt to skip it only for the local source broke remote parsing.)

`subevent_format_version` is 1 and matches `CURRENT_SUBEVENT_FORMAT_VERSION`.

---

## 2. FAFA chunking as observed

- **Local chunks on the earlier bench firmware** arrived as exactly one record
  per chunk (`wc = 51`), one chunk per DMA buffer. This is an observation of
  small records, not a software requirement; current firmware can split self
  records across chunks and DMA transfers.
- **Remote chunks** arrive fragmented. Typical sequence for one 51-word record:
  `wc = 3, 8/9, 12/13, 15/16, 17/18`, i.e. the source FIFO is serviced as data
  trickles in over 10GbE. Chunk boundaries fall anywhere inside a record,
  including inside the subevent header. Several records' worth of remote chunks
  can land in one DMA buffer (observed: 1224 bytes = 3 records in one buffer on
  DTC_0).
- Chunks from different sources interleave freely within and across DMA buffers.
- Word counts always balance: `wc_output_stream = wc_self_transfer + wc_bufmgr_output`
  held on every run, including runs with corrupted payload (see §5). **The FAFA
  framing and the `wc_*` counters cannot detect the corruption class we are
  seeing; only content parsing does.**

Software therefore keeps one reassembly buffer per `chunk_src`
(`evbPerSourceReassembly_`), appends chunk payload in arrival order, and
extracts records only when a full `record_bytes` is present. This part of the
design matches the protocol doc and has worked on every run.

### Capped local/remote chunks: software compatibility

The updated buffer manager caps both kinds of chunk at `DMA_max_words - 2`
(FAFA header and close filler overhead). Local continuation chunks retain
`src = self`; remote FIFO remainders become subsequent chunks. Bit 6 of
`0x9370` is retired and reads 0.

Code inspection confirms EVB Buffer Test already supports this contract:
`GetEVBDataAsEvents()` appends every chunk to
`evbPerSourceReassembly_[chunk_src]`, with no local-source special case.
The record parser waits for a full header and then a complete record,
removes only completed records, and preserves trailing bytes for subsequent
DMA reads. FAFA-looking and filler-looking words inside the counted payload
are copied, not interpreted as framing. `evbLocalMac_` is used for the output
event header, not to bypass source reassembly. No parser change is needed for
self chunks beginning mid-record. This is a source audit, not a validation
run against the updated firmware.

The configured DMA limit can be read from `0x9104[31:16]` via
`ReadTriggerDMATransferLength()`; the payload cap is
`floor(DMA_max_bytes / 8) - 2` words. These values are not displayed in EVB
Buffer Test status. The checked-in driver default is
`SET_DTC_MAX_DMA_SIZE = 0xfff8` (65,528 bytes), not a confirmed deployed value.
Read the register on each DTC to determine its actual programmed limit rather
than assuming the observed 695-word oversize flags establish a specific size.

---

## 3. Tag assignment and ordering (observed and assumed)

**Observed.** For tag T, the destination is `EVBStartNode + (T mod N)`. All N
DTCs send their subevent for T to that destination. So destination DTC *d* sees
tags `d, d+N, d+2N, …`, and **each of those tags arrives N times**, once per
source DTC (one local, N−1 remote). Bench traces:

```
DTC_0:  local  src=0x80 EWT=0, 2, 4      remote src=0x81 EWT=0, 2, 4
DTC_1:  local  src=0x81 EWT=1, 3, 5      remote src=0x80 EWT=1, 3, 5
```

**Observed.** Within a single source, tags arrive in increasing order (0, 2, 4
from src 0x80 on DTC_0; 1, 3, 5 from src 0x80 on DTC_1).

**Observed.** Across sources the arrival is intermixed. On DTC_0 the three local
records (tags 0, 2, 4) were all extracted before the first remote record (tag 0)
because local chunks are whole and remote chunks trickle. So the software must
not assume that the two halves of tag 0 are adjacent in the stream, nor that
tag 0 completes before tag 2's first half arrives.

**Assumed, to confirm with firmware.** Per source, tags are strictly increasing
with no gaps other than the destination stride N. If a source ever skips a tag,
that is a lost record, not a reordering. Software will treat a per-source tag
that jumps by more than N as an error.

**Consequence for the current buffer test.** The detached buffer test now
accepts tag T once from each distinct `source_dtc_id`, and advances its expected
tag by N only when all N sources have delivered T. A second delivery of T from
the same source is a mismatch. This replaced two earlier wrong models (advance by
1, advance by N per record) that both produced false mismatches.

---

## 4. Event-completion model (implemented 2026-09-04, not yet run on hardware)

`GetEVBDataAsEvents` previously returned each subevent as soon as its record was
complete, wrapped in a synthetic single-DTC `DTC_Event`. It now builds complete
events in software as follows. Tunables: `DTC::SetEVBEventTimeout()` (default
2000 ms), `evbMaxOpenTags_` (1024). Status: `GetEVBOpenTagCount()`,
`GetEVBEventsReleased()`, `GetEVBNumSources()`; both are shown in the detached
Buffer Test EVB status. `DTC::ResetEVBAssembly()` is called at buffer-test
thread start and restart.

0. **DMA buffer lifetime is one call.** Each call to `GetEVBDataAsEvents` reads
   at most one DMA buffer, copies every FAFA chunk payload out of it into a
   per-source host-memory reassembly buffer (Step 3), and releases the DMA
   buffer when the call exits by any path, normal or exception (RAII guard). No
   pointer into the DMA ring survives the call. The per-source reassembly
   buffers and the per-tag table below are ordinary heap memory, so an event
   whose subevents arrive across hundreds of DMA transfers never ties up any of
   the ~100 ring buffers. Waiting for completion costs host memory only, bounded
   by the open-tag cap in item 5.
1. **Per-tag assembly table.** Keyed by event window tag. Each entry holds the
   subevents received so far (keyed by `source_dtc_id`), the wall-clock time of
   the **first** subevent arrival for that tag, and the expected source count N
   (from `EVBNumNodes`, register `0x9158[7:0]`). Each staged subevent is a copy
   (record header stripped) taken from the reassembly buffer at extraction time.
2. **Insert.** When a record is extracted from a source stream, look up its tag.
   Create the entry on first arrival and stamp the arrival time. A second
   subevent from a source already present for that tag is an error (duplicate).
3. **Emit.** When an entry holds N subevents, emit one `DTC_Event` containing
   all N to the caller and remove the entry. ("Emit" here is delivery to the
   caller; it has nothing to do with DMA buffer release, which happened in item 0.) Because tags per source are increasing and every
   source contributes to every tag destined here, **complete events are released
   in increasing tag order** even though subevents arrive intermixed. Software
   will assert this: a completed tag lower than the last released tag is an
   error.
4. **Timeout.** On every call, scan open entries. If `now − first_arrival >
   EVB_EVENT_TIMEOUT` for any entry, throw with a diagnostic naming the tag, the
   sources present, the sources missing, and a hex dump of the partial event.
   The detached buffer test's top-level catch then stops the test with that
   message, which is the existing "die on first error" behaviour. **Initial
   value: 2 seconds.** Rationale: on the bench the two halves of a tag arrive
   within milliseconds; 2 s is long enough to survive DMA bundling delays
   (200 µs firmware timeout) and host scheduling, short enough to fail fast.
   To be tuned once real trigger rates are known.
5. **Bounded memory.** The table is bounded by (timeout × event rate). At the
   design rate this is small, but the implementation should cap open entries and
   treat overflow as the same timeout error.

Open question for the firmware side: is there any legitimate case where a
destination DTC receives **fewer** than N subevents for a tag it owns (for
example a source DTC with no enabled ROCs for that window)? If so the firmware
must still send an empty subevent, or software needs a per-source "expected"
mask. Today software assumes exactly N.

---

## 5. Corruption class observed on the remote path (for reference)

Reported separately to the firmware side; summarised here because it drives the
parser's defensive checks.

**Signature.** A single 64-bit word is replaced by a copy of the word
immediately following it. Word count is preserved. Seen only on records that
travelled the 10GbE path, in both directions. Positions seen (record word index,
0 = record header): 2, 7, 8, 10, 21, 34, 40. Roughly 1–2 per 51-word record.
Payload-only hits are invisible to `DTC_SubEvent::SetupSubEvent`, which
validates headers, not payload.

**Parser consequences and the checks now in place** (all in `GetEVBDataAsEvents`):

| Where the replaced word lands | Old behaviour | New behaviour |
|---|---|---|
| record header word or subevent qword 0 (size fields) | size becomes garbage; parser waits forever for "more chunks", thread silent | record/subevent size cross-check fails → dump + throw |
| subevent qword 1 (`num_rocs`, `tag_high`) | garbage tag; ROC0 header check fails with a misleading message | `num_rocs` range check and decoded-header message → dump + throw |
| ROC0 DataHeader word 0 (offset 48) | `SetupSubEvent` loops forever (its catch block never advances `byte_count`), flooding the log at ~1900 iterations/s | pre-check with `DTC_DataHeaderPacket::IsDataHeaderPacket` → dump + throw |
| later ROC header | `SetupSubEvent` recovers, marks corrupt | `IsCorrupt()` → dump + throw |
| payload | undetected | undetected (see below) |

Every throw includes a hex dump of the bad record and of the last good record
from the same source, so the two can be diffed word by word.

**Not covered:** payload corruption. The bench payload is a known incrementing
pattern, so a bench-only payload checker is possible and would give a true
per-word error rate. It is not appropriate for production data and is left as a
test-mode option.

---

## 6. Things software still needs from the firmware side

1. Confirm the record header byte-count definition (§1).
2. Confirm per-source tag ordering is strictly increasing with stride N (§3).
3. Confirm every destination tag always receives exactly N subevents (§4).
4. The observed word-replacement corruption is present through the 2026-09-04
   bitfile; software cannot mask it (§5).

## 7. Related code

- `mu2e-pcie-utils/dtcInterfaceLib/DTC.cpp` — `GetEVBDataAsEvents()`: FAFA walk,
  per-source reassembly, record extraction, all defensive checks and dumps.
- `otsdaq-mu2e/FEInterfaces/DTCFrontEndInterfaceImpl.cc` —
  `detachedBufferTestThread()` EVB branch and `handleDetachedSubevent()`:
  per-source tag acceptance model, mismatch recording, status display.
- `artdaq-core-mu2e/Overlays/DTC_Packets/DTC_SubEvent.cpp` — `SetupSubEvent()`:
  header/ROC parsing; note the non-advancing catch at the top of its while-loop.
