# DTC Performance Counter Registers — software naming translation

Reference for mapping the software counter displays (otsdaq FEMacros like
"Get DTC Counters" / "DTC Read") to firmware register addresses. Software
address definitions live in `DTC_Registers.h:276-345`; firmware localparams
in [register_map.v](YearMonDD_HH_VV.V/Aldec/design1/src/register_map.v)
(~lines 854-910).

## Key translation rule

**Link 6 is the CFO link.** Counters indexed 0-5 are the ROC links and are
displayed under "ROC Link Counters [Links 0-5]". The link-6 variant of a
counter (where one exists) is displayed by software with a "CFO" name, e.g.:

| Software display name              | Register | Firmware localparam     |
|------------------------------------|----------|-------------------------|
| CFO Event Window Marker (EWM) Count| `0xA418` | `TRANSMIT_EWM_COUNT6`   |
| CFO Heartbeat Packet (HBP) Count   | `0x9668` | `TRANSMIT_HB_COUNT6`    |

These count in the **CFO link region** (marker/packet traffic on link 6),
NOT on the top-level `EventWindowStart` wire into the EVB — a nonzero
0xA418 does not by itself prove markers reach EVB3's marker CDC.

## Per-ROC-link counters (Links 0-5)

| Counter               | Link 0 | Link 1 | Link 2 | Link 3 | Link 4 | Link 5 | Firmware localparam base |
|-----------------------|--------|--------|--------|--------|--------|--------|--------------------------|
| TX EWM Count          | 0xA400 | 0xA404 | 0xA408 | 0xA40C | 0xA410 | 0xA414 | `TRANSMIT_EWM_COUNTn`    |
| TX Data Request Count | 0x9630 | 0x9634 | 0x9638 | 0x963C | 0x9640 | 0x9644 | `TRANSMIT_DR_COUNTn`     |
| TX Heartbeat Count    | 0x9650 | 0x9654 | 0x9658 | 0x965C | 0x9660 | 0x9664 | `TRANSMIT_HB_COUNTn`     |
| TX Null HBP Count     | 0xA440 | 0xA444 | 0xA448 | 0xA44C | 0xA450 | 0xA454 | `TRANSMIT_NULL_HB_COUNTn`|
| RX Data Header Count  | 0x9670 | 0x9674 | 0x9678 | 0x967C | 0x9680 | 0x9684 | `RECEIVE_DH_COUNTn`      |
| RX Data Packet Count  | 0x9690 | 0x9694 | 0x9698 | 0x969C | 0x96A0 | 0x96A4 | `RECEIVE_DP_COUNTn`      |
| RX DataHdr Timeout Cnt| 0xA420 | 0xA424 | 0xA428 | 0xA42C | 0xA430 | 0xA434 | `RECEIVE_DHTIMEOUT_COUNTn`|
| RX Wrong-Tag DH Dropped (2026-11-04) | 0xA4A0 | 0xA4A4 | 0xA4A8 | 0xA4AC | 0xA4B0 | 0xA4B4 | `RECEIVE_DROPPED_DH_COUNTn` |
| RX Wrong-Tag DP Dropped (2026-11-04) | 0xA4C0 | 0xA4C4 | 0xA4C8 | 0xA4CC | 0xA4D0 | 0xA4D4 | `RECEIVE_DROPPED_DP_COUNTn` |

Since 2026-11-04 a ROC Data Header whose Event Window Tag is not the one the
Data Request asked for is dropped together with its payload packets (not
stored under the requested tag as before); the request stays pending with its
timeout running. "RX Data Header Count" (0x9670..) therefore counts STORED
headers; received = 0x9670 + 0xA4A0 per link. RINGERROR (0x9380..) bit 21 is
the sticky "a wrong-tag answer was dropped" flag, SoftReset clear. 0xA4C0.. also
counts payload packets that arrived with no accepted header for the current
request (the late tail of an earlier answer); bit 22 is that sticky flag.

**"TX non-Null HBP Diff" is not a hardware register** — software computes
`ReadTXHeartbeatPacketCount() - ReadTXNullHeartbeatCount()`, truncated to
16 bits.

## CFO-link-specific counters (no 0-5 sibling)

| Register | Firmware localparam                | What it counts                                        |
|----------|------------------------------------|-------------------------------------------------------|
| `0x9648` | `TRANSMIT_CFO_ClkM_COUNT`          | CFO 40MHz clock markers (sw: "CFO Rx Clock Markers")   |
| `0x966C` | `CFO_HB_FORWARDED_COUNT`           | Heartbeats forwarded to ROCs (`Heartbeat_sig`)         |
| `0x9688` | `CFO_CDC_DIAG`                     | [31:16] parity mismatch cnt, [15:0] batch slip cnt     |
| `0x96A8` | `CFO_HB_CRCFAIL_COUNT`             | Heartbeats dropped on CRC mismatch                     |
| `0x96AC` | `CFO_HB_PARSEINCOMPLETE_COUNT`     | Heartbeats whose parse never completed                 |

## Related EVB configuration registers (see also [DTC_Register_Map.md](DTC_Register_Map.md))

| Register | Name                     | Fields                                                            |
|----------|--------------------------|-------------------------------------------------------------------|
| `0x9154` | RAWDATA_PART_ID_LOCAL_MAC| Partition ID / Local MAC / DTC ID; byte [7:0] = self EVB address   |
| `0x9158` | RAWDATA_NUM_EVB_NODES    | [31:16] TX dead time, [15:8] start (base) node, [7:0] num nodes    |
| `0x915C` | EVB_PACKET_CONTROL       | [31:16] idle packet words, [15:8] inter-packet gap, [7:0] delay offset (feeds EVB3 `loopbackCalibratedOffset` marker delay) |
| `0x9160` | EVB_STATS                | Indirect stats BRAM access — see [EVB3_software_status_registers.md](EVB3_software_status_registers.md) |
