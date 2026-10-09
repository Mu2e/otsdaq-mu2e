# EVB3 Hardware-to-Software Register Map

Register addresses and bitfields for the "EVB Status" software macro, with
corresponding DTC library accessors.

---

## EVB Link Status

| Field     | Register | Bits          | Software Accessor                                  |
|-----------|----------|---------------|----------------------------------------------------|
| TX Enable | `0x9114` | Link 7 TX bit | `ReadLinkEnabled(DTC_Link_EVB)` → `.TransmitEnable`|
| RX Enable | `0x9114` | Link 7 RX bit | `ReadLinkEnabled(DTC_Link_EVB)` → `.ReceiveEnable` |
| CDR Lock  | `0x9140` | Link 7 bit    | `ReadSERDESCDRLock(DTC_Link_EVB)`                  |

---

## EVB Configuration

### Register `0x9154`

| Bits      | Field             | Software Accessor            |
|-----------|-------------------|------------------------------|
| `[31:24]` | DTC ID            | `ReadDTCID()`                |
| `[23:16]` | EVB Mode          | `ReadEVBMode()`              |
| `[15:8]`  | Partition ID      | `ReadEVBLocalParitionID()`   |
| `[7:0]`   | Local MAC Address | `ReadEVBLocalMACAddress()`   |

### Register `0x9158`

| Bits      | Field           | Software Accessor                    |
|-----------|-----------------|--------------------------------------|
| `[31:16]` | Dead Time       | `ReadEVBDeadTime()`                  |
| `[15:8]`  | Start Node      | `ReadEVBStartNode()`                 |
| `[7:0]`   | Num Dest Nodes  | `ReadEVBNumberOfDestinationNodes()`  |

---

## Pipeline Word Counters

16-bit saturating counters, packed two per 32-bit register. Reset on
soft/hard reset. Aggregate totals (not per-DTC).

| Register | `[31:16]`            | `[15:0]`              | Accessor (high / low)                                    |
|----------|----------------------|-----------------------|----------------------------------------------------------|
| `0x9200` | Self-transfer words  | ROC input words       | `ReadEVBSelfTransferWords()` / `ReadEVBROCInputWords()`  |
| `0x9204` | DDR→TX words         | DDR FIFO write words  | `ReadEVBDDR2TXWords()` / `ReadEVBDDRFIFOWriteWords()`   |
| `0x9208` | DMA output words     | Buffer mgr out words  | `ReadEVBDMAOutputWords()` / `ReadEVBOutputBufferManagerWords()` |
| `0x920C` | *(zero)*             | GBE RX words          | — / `ReadEVBGBERXWords()`                                |

---

## Per-DTC BRAM Stats (indirect via `0x9160`)

Write the 9-bit BRAM address, then read back from the same register.

**Procedure:**
1. Write `(type[3:0] << 5) | dtcOffset[4:0]` to `0x9160`
2. Wait (read is gated by TX FSM port sharing)
3. Read `0x9160` → 32-bit stat value

**Software accessor:** `ReadEVBStats(DTC_EVBStatsType type, uint8_t dtcOffset)`

`dtcOffset` ranges `0` to `NumNodes − 1`. Status display shows absolute
address as `StartNode + offset`.

### BRAM type encoding

| Type value | Name              | Organized by    |
|:----------:|-------------------|:---------------:|
| `0x0`      | RxCount           | source          |
| `0x1`      | RxLastSeqTag      | source          |
| `0x2`      | RxMissingPktCnt   | source          |
| `0x3`      | RxByteCount       | source          |
| `0x4`      | RxLastPktArrival  | source          |
| `0x5`      | TxLastSeqTag      | **destination** |
| `0x6`      | TravelTime        | source          |
| `0x7`      | TxIdleCount       | **destination** |
| `0x8`      | RxIdleCount       | source          |

---

## 10GbE SERDES

| Field                | Register | Bits         | Software Accessor                    |
|----------------------|----------|--------------|--------------------------------------|
| RX Packet Error Count| `0x9590` | full 32-bit  | `ReadEVBSERDESRXPacketErrorCounter()`|

Not clearable by software — resets only on hard/soft reset.
