# DTC Register Map

Source: `register_map.v`

---

## 0x9000 – 0x90FF: Design Info / System Monitors

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x9000 | DESIGN_VERSION1 | Firmware version word 1: [31:16] CFO/ROC link speeds, [15:0] major/minor version |
| 0x9004 | DESIGN_VERSION2 | Firmware version word 2: build date and ROC configuration |
| 0x9008 | DESIGN_STATUS | Design status: [1] AXI IC MIG shim reset, [0] DDR calibration done |
| 0x900C | VIVADO_VERSION | Vivado build tool version |
| 0x9010 | DEVICE_TEMP | XADC device temperature (12-bit) |
| 0x9014 | DEVICE_VCCINT | XADC VCCint voltage (12-bit) |
| 0x9018 | DEVICE_VCCAUX | XADC VCCaux voltage (12-bit) |
| 0x901C | DEVICE_VCCBRAM | XADC VCCBRAM voltage (12-bit) |
| 0x9020 | XADC_ALARM | XADC alarm flags (latched); write to clear |
| 0x9024 | TIME_ALIVE | Time alive counter (1 ms resolution, 30-bit) |
| 0x9030 | SCRATCH_REGISTER | General-purpose scratch register (RW) |
| 0x9040 | KERNELDRIVERVERSION | Kernel driver version (written by software) |

---

## 0x9100 – 0x91FF: DTC Control / SERDES / CFO Emulator

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x9100 | RAWDATA_CONTROL | DTC Control Register (see bit field table below) |
| 0x9104 | RAWDATA_DMA_SIZE | DMA size limits in bytes: [31:16] max size, [15:0] min size. Reset value 0x8000_0040; cleared by a HARD reset (0x9100 bit 0) or a bitfile load only, NOT by SoftReset -- the last value written stays (calo-14 2026-10-04/05 ran on a test program's 0xFFF8). Firmware guards: a max <= min + 16 is replaced by 0x8000; a min < 0x40, or >= max - 16, is replaced by 0x40. The max is a hard cap on every DMA transfer: the ring mux splits a subevent larger than it into several records, each with its own count quadword (first record = ((max >> 4) - 1) x 16 + 8 bytes), and EVB3 caps a chunk at (max >> 3) - 2 words (8189 with 0xFFF8); since 2026-10-05 the bundled ring mux (no-EVB build) closes a DMA before adding a subevent that would not fit, close word included. Before that fix two subevents that each fit could land in one DMA of almost 2 x the max |
| 0x9108 | RAWDATA_ENABLE_LB_CHECKER0 | SERDES loopback enable per link [23:0] |
| 0x910C | RAWDATA_CHECKER_STATUS0 | IIC error/init status for SERDES and DDR oscillator |
| 0x9110 | RAWDATA_ROC_EMULATION_ENABLE | ROC emulation enables: [17:12] external, [11:6] receive behavior, [5:0] internal |
| 0x9114 | RAWDATA_RING_ENABLE | Link interface enables: [25] resequence non-null events, [24] block null HBs to ROC, [15:8] RX enable, [7:0] TX enable. Bit 7 (EVB 10GbE TX) also selects hardware event building AND, since 2026-10-05, the DMA format on the PCIe data channel: bit 7 = 0 -> the ring mux stacks subevents into one DMA transfer closed by an all-ones tlast word (on the 0x9104 max, or 200 us idle), exactly the non-EVB build's format; bit 7 = 1 -> one DMA transfer + one undeclared tlast word per subevent into EVB3, which repacks them as FAFA chunks. Change bit 7 only with data requests stopped and the data path idle for more than 200 us, or follow it with a SoftReset (0x9100 bit 31): an open DMA at the flip would otherwise carry both formats. Clearing it is graceful since 2026-09-15: a frame in progress is finished (FCS + terminate) before the TX idles, so a stop never leaves a bad-FCS frame on the switch. Clearing bit 15 (EVB RX) sweeps the per-source RX stats BRAM and RX sticky latches -- to freeze traffic for a counter read, clear bit 7 only |
| 0x9118 | RAWDATA_SERDES_RESET | SERDES reset controls: [31:16] TX/RX reset, [15:8] PLL reset, [7:0] SERDES reset. Bit 7 = EVB 10GbE link (link 7) reset, connected 2026-09-24 (was a dead input before: EVB Init "link reset attempted" did nothing). Level, not self-clearing by itself: write 1 then 0; a SoftReset (0x9100 bit 31) also zeroes this whole register (bench 2026-09-24). Scope (bench 21:24 on 0xd6092496): restarts the wizard TX+RX start-up FSMs (0x9138 bits 15/31 drop while held), block sync and lock counters; the GT resetdone bits (0x9138 bits 7/23) and the PLL bit (0x9128 bit 7) stay 1 to a register read. TX stops for the FSM sequence, the peer loses 0x9140 bit 7 and regains it within one poll after release, so only between runs. Bits [31:16] and [15:8] do nothing for link 7 |
| 0x911C | RAWDATA_RX_DISP_ERROR | RX disparity error flags (latched); write to clear |
| 0x9120 | RAWDATA_RX_NOTINTABLE | RX not-in-table error flags (latched); write to clear |
| 0x9124 | RAWDATA_SERDES_UNLOCK | SERDES unlock/PLL unlock flags (latched); write to clear |
| 0x9128 | RAWDATA_SERDES_PLL_LOCKED | SERDES PLL lock status |
| 0x912C | RAWDATA_SERDES_CPLLPD | SERDES CPLL power-down control [6:0] |
| 0x9130 | RXCDRLOCKCOMMACOUNT | RX CDR lock comma count threshold [15:0] |
| 0x9134 | RAWDATA_RXSTATUSREG | RX status register (per-link 3-bit status) |
| 0x9138 | RAWDATA_SERDESRESETDONE | SERDES reset-done status |
| 0x913C | RXCDRUNLOCKMASK | RX CDR unlock mask [4:0] |
| 0x9140 | RAWDATA_RXCDRLOCK | RX CDR lock status [7:0] |
| 0x9144 | RAWDATA_OUTPUTFIFOTIMERPRESET | Output FIFO timer preset value |
| 0x9148 | RAWDATA_DATA_HEADER_TO_ROC_DATA_DONE_PRESET | Data header to ROC data-done timer preset |
| ~~0x914C~~ | ~~RAWDATA_DATA_HEADER_TO_ROC_DATA_DONE_TIMEOUT~~ | ~~Removed — errorlatch was commented out, flag never latched. Use RECEIVE_DHTIMEOUT_COUNTn (0xA420–0xA434) instead~~ |
| 0x9150 | RAWDATA_PKT_SIZE | Ring packet length [15:0] |
| 0x9154 | RAWDATA_PART_ID_LOCAL_MAC | Partition ID / Local MAC / DTC ID |
| 0x9158 | RAWDATA_NUM_EVB_NODES | EVB config: [31:16] TX dead time, [15:8] start node, [7:0] number of nodes |
| 0x915C | EVB_PACKET_CONTROL | EVB packet control: [31:16] idle packet words, [15:8] inter-packet gap, [7:0] delay offset |
| 0x9160 | EVB_STATS | EVB source statistics readback (write address [8:0] = {type[3:0], node offset[4:0]} to select, read data [31:0]). Types 0x0-0x8 are stats-BRAM rows; type 0x9 TX_COUNT (total frames sent on the port, all destinations, 2026-09-15) is a single register read the same way (offset ignored) |
| 0x9164 | RAWDATA_SERDESREFCLKCONT | SERDES oscillator control: [31] reset |
| 0x9170 | EVB_IDLE_BURST | EVB idle-packet burst (2026-09-17): [15:0] idle packets sent per destination window when the DTC has no data for that destination. 0 or 1 = one idle packet per window (legacy). N = up to N, each spaced by the 0x915C inter-packet gap and sized by 0x915C[31:16]; the burst stops early when the dead-time window closes. A burst only continues a window that began with an idle: a window that sent data never appends idles (legacy behavior), so with data flowing the knob is inert. Reset value 1. Pure-idle link test knob: with N large the link runs idle frames back-to-back for the whole window (known contents, per-destination sequence, TxCount/RxCount/RxMissingPktCnt parity). |
| 0x9174 | EVB_RESEND_COUNT | (2026-09-28, read-only) EVB3 retransmission counters: [31:16] resends served by this DTC's TX (rewinds), [15:0] resends requested by this DTC's RX (sequence gaps that entered discard). SoftReset clear, 16-bit wrap. addr[2]=1 -> hi bank 0 (< 0x91D4) |
| 0x9178 | EVB_LOCAL_CHUNK_CAP | (2026-10-06, RW) EVB3 longest SELF chunk to the DMA, in 8-byte words: [15:0]. 0 = whole record (the pre-2026-10-06 behavior: one self chunk of up to DMA max - 2 words = 8189 at 0x9104 = 0xFFF8, during which no remote source FIFO is drained, so the peer stops on credit for most of every self record); values 1..31 act as 32. HardReset 0x0400 (1024 words = ~4 us of DMA), survives SoftReset like 0x9170. A self record larger than the cap arrives as several self chunks with the same FAFA framing (src = self); software already reassembles self by source (EVB3_DMA_FAFA_protocol.md). |
| 0x917C | EVB_REMOTE_CHUNK_MIN | (2026-10-07, RW) EVB3 buffer manager: [15:0] fewest 8-byte words a REMOTE source FIFO must hold before it is granted a DMA chunk (0 = any words, the pre-2026-10-07 behavior); [31:16] user_clk clocks (4 ns) a non-empty remote FIFO may wait for that minimum before it is granted anyway (so a record's tail never waits). HardReset 0x0400_0080 (1024 clocks = 4 us, 128 words), survives SoftReset. Why: under a steady 10GbE stream a remote chunk was whatever the FIFO held at the grant (11-19 words on average, 90% under 16), each costing 4 clocks + 1 FAFA word on the DMA, and the self stream parked between every two of them; on the 4G-link stand that turned the 0x9178 cap into a 30% loss (0xd6100794). |
| 0x9180 | EVB_RX_ILLEGAL_DEST_COUNT | (2026-10-13, read-only) [15:0] count of 10GbE RX frames whose destination MAC did not match this DTC's address (non-broadcast only; broadcast is silently ignored). These are frames the switch forwarded to us that belong to another DTC or another partition. SoftReset clear, saturates at 0xFFFF. The sticky flag for the same condition is 0x9370 status bit 26. |
| 0x9168 | RAWDATA_SERDESREFCLKCONFLOW | SERDES oscillator IIC: [31:16] address, [15:8] write data, [7:0] read data |
| 0x916C | RAWDATA_SERDESREFCLKCONFHIGH | SERDES oscillator IIC CSR: [1] read enable, [0] write enable |
| 0x9184 | CFOEM_LOOPBACK_DELAYMEASURE | CFO emulator loopback delay measurement: [31] done, [30:0] measured value |
| 0x9188 | LINUX_TIME | Linux time (written by software for timestamping) |
| 0x918C | RAWDATA_NUMROCS | Reserved (Deprecated — number of ROCs, now a parameter) |
| 0x9190 | RAWDATA_FIFOERRORFLAGS0 | FIFO error flags 0: DRP out, RRP out, timing link input (latched); write to clear |
| 0x9194 | RAWDATA_FIFOERRORFLAGS1 | FIFO error flags 1: link input data, DCS status out, other out (latched); write to clear |
| 0x9198 | RAWDATA_FIFOERRORFLAGS2 | FIFO error flags 2: link input DCS status (latched); write to clear |
| 0x919C | RAWDATA_RX_Buffer_Error0 | RX buffer errors 0: [14:8] packet error, [6:0] CRC error (latched); write to clear |
| 0x91A0 | CFOEMSTARTTIMESTAMPLOW | CFO emulator starting timestamp [31:0] |
| 0x91A4 | CFOEMSTARTTIMESTAMPHIGH | CFO emulator starting timestamp [47:32] |
| 0x91A8 | CFOEMEVENTSTARTINTERVALTIME | CFO emulator event start interval time |
| 0x91AC | CFOEMNUMREQUESTS | CFO emulator number of requests |
| 0x91B0 | ROCEMNUMPACKETS0 | ROC emulator number of packets: [26:16] ROC 1, [10:0] ROC 0 |
| 0x91B4 | ROCEMNUMPACKETS1 | ROC emulator number of packets: [26:16] ROC 3, [10:0] ROC 2 |
| 0x91B8 | ROCEMNUMPACKETS2 | ROC emulator number of packets: [26:16] ROC 5, [10:0] ROC 4 |
| 0x91BC | NUMNULLS | Number of null heartbeats (configurable) |
| 0x91C0 | CFOEVENTMODE0 | CFO emulator event mode [31:0] |
| 0x91C4 | CFOEVENTMODE1 | CFO emulator event mode [47:32] |
| 0x91C8 | CFO_EVENT_MODE_REQUIRED_MASK | CFO event mode required mask (non-zero forces null HB if mode bits don't match) |
| 0x91CC | RAWDATA_RX_Buffer_Error1 | RX buffer errors 1: [6:0] packet count error (latched); write to clear |
| 0x91D0 | DETECTOR_EMULATION_LOOP_COUNT | Detector emulation loop count |
| 0x91D4 | DETECTOR_EMULATION_DMA_DELAY_COUNT | Detector emulation DMA delay count |
| 0x91D8 | DETECTOR_EMULATION_CONTROL_0 | Detector emulation control: [31] reset pattern checker, [1] enable set, [0] mode |
| 0x91DC | DETECTOR_EMULATION_CONTROL_1 | Detector emulation control 1: [1] enable clear |
| 0x91E0 | DETECTOR_EMULATION_START | Detector emulation start address |
| 0x91E4 | DETECTOR_EMULATION_END | Detector emulation end address |
| 0x91E8 | CFOEMDRPDELAYTIME | CFO emulator DRP delay time |
| 0x91EC | MAXENETPAYLOADSIZE | Max Ethernet payload size [13:0] |
| 0x91F0 | EVB_TX_HIST_READ | (2026-10-13, read-only) TX packet history BRAM readback. Each read returns one entry and advances the pointer. Returns 0xFFFFFFFF when all entries have been read. Bit layout: [28] first packet of subevent, [27] last packet, [26] has data (vs null), [25:21] destination (5-bit), [20:13] sequence number, [12:0] packet size in 64-bit words. Read 0x925C first to check frozen + count, then read this register count+1 times (first read loads the output, subsequent reads return entries 0..count-1). **Walk once per run:** the read pointer resets only on SoftReset, so a second walk returns 0xFFFFFFFF until the next SoftReset. **Ring:** after 1024 packets the count holds at 1023 and entry 0 is the oldest surviving packet, not the first packet of the run; the entry just before the freeze is the LAST one read, so walk to the end and look backwards from there. If frozen = 0 at the end of a run, no retransmission was requested and the log holds the most recent packets. |
| 0x91F4 | CFOEMCLOCK40MHZINTERVAL | CFO emulator 40 MHz clock marker interval |
| 0x91F8 | CFOMarkerEnables | CFO 40 MHz clock marker enables [5:0] |
| 0x91FC | ROCFINISHTHRESHOLD | ROC finish threshold [7:0] |

---

### DTC Control Register (0x9100) Bit Field

| Bit | Mode | Default | Description |
|-----|------|---------|-------------|
| 31 | WO | 0 | DTC Soft Reset (Self-clearing) |
| 30 | RW | 0 | CFO Emulation Enable |
| 29 | RW | 0 | Reserved (Formerly CFO Emulation Enable Continuous) |
| 28 | RW | 0 | CFO Link Not-in-Loopback Output Control |
| 27 | RW | 0 | Reset DDR Write Address |
| 26 | RW | 0 | Reserved (Formerly Reset DDR Read Address) |
| 25 | RW | 0 | Reserved (DDR Interface Reset) |
| 24 | RW | 0 | CFO Emulator DRP Enable |
| 23 | RW | 0 | DTC Autogenerate DRP Enable (OR override — enables auto-DR on all ROCs; use 0x9114[21:16] for per-ROC control) |
| 22 | RW | 0 | Kill ROC on 10x Timeouts |
| 21 | RW | 0 | Reserved (Formerly local EVB Source Buffer Reset) |
| 20 | RW | 0 | Reserved (Formerly EVB DMA Buffer Reset) |
| 19 | RW | 0 | Down LED 0 |
| 18 | RW | 0 | Up LED 1 |
| 17 | RW | 0 | Up LED 0 |
| 16 | RW | 0 | LED 7 |
| 15 | RW | 0 | CFO Emulation Mode |
| 14 | RW | 0 | Reserved (Formerly EVB Debug Count Enable) |
| 13 | RW | 0 | Reserved (Formerly Trigger Filter Enable) |
| 12 | RW | 0 | DRP Prefetch Enable |
| 11 | RW | 0 | Hardware EVB Buffer Reset |
| 10 | RW | 0 | Drop Subevent Data to Emulate Hardware Event Building |
| 9 | RW | 0 | Punch Enable on RJ-45 Output |
| 8 | RW | 0 | SERDES Global Reset |
| 7 | RW | 0 | RTF Punched Clock Edge Select (0=negedge, 1=posedge) |
| 6 | RW | 0 | Do Force External CFO Sample Edge |
| 5 | RW | 0 | Force External CFO Sample Edge Select |
| 4 | RW | 0 | Fanout Clock Input Select |
| 3 | RW | 0 | CFO Emulator Loopback Test Launch Control |
| 2 | RW | 0 | DCS Enable |
| 1 | RW | 0 | Reserved (Formerly SERDES Comma Align) |
| 0 | WO | 0 | DTC Hard Reset (Self-clearing) |

---

### Link Interface Enable Register (0x9114) Bit Field

| Bit | Mode | Default | Description |
|-----|------|---------|-------------|
| 31:26 | RO | 0 | Reserved |
| 25 | RW | 0 | Resequence Non-Null Events (with bit 24): Event Window Tags sent to ROCs are renumbered from 0, one per SENT heartbeat (non-null or close-out null). Set BEFORE heartbeats start, then SoftReset; switching it on during a run drops the DTC's latest-tag bound below the next tag to request and auto data requests stop until a reset (sim 2026-10-01). |
| 24 | RW | 0 | Block Null Heartbeats to ROC. Assumes the ROC runs without a shared run plan: the FIRST null heartbeat after non-null heartbeat(s) is still sent (packet and Event Window Marker) so the ROC can close its last event; the nulls that follow are blocked, packet and marker. With bit 25 set the sent close-out null takes the next resequenced tag. (Rule since 2026-09-25; before, every null was blocked and a heartbeat without its marker put the marker blocking permanently off by one.) |
| 23:22 | RO | 0 | Reserved |
| 21 | RW | 0 | DRP Auto-Generate Enable ROC 5 |
| 20 | RW | 0 | DRP Auto-Generate Enable ROC 4 |
| 19 | RW | 0 | DRP Auto-Generate Enable ROC 3 |
| 18 | RW | 0 | DRP Auto-Generate Enable ROC 2 |
| 17 | RW | 0 | DRP Auto-Generate Enable ROC 1 |
| 16 | RW | 0 | DRP Auto-Generate Enable ROC 0 |
| 15:8 | RW | 0 | Link Interface RX Enable [7:0] (one bit per link) |
| 7:0 | RW | 0 | Link Interface TX Enable [7:0] (one bit per link) |

Note: Per-ROC DRP auto-generate enable bits [21:16] are OR'd with the global DTC Autogenerate DRP Enable at 0x9100[23]. Set 0x9100[23]=1 to enable auto-DR on all ROCs (backwards compatible). Set 0x9100[23]=0 and use bits [21:16] for individual ROC control.

---

## 0x9200 – 0x92FF: Oscillator IIC / PRBS / Mode Lookup

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x9200 | EVB_WORD_COUNT_0 | EVB debug word counters (data words only, excl. FAFA headers): [31:16] wc_self_transfer, [15:0] wc_roc_input |
| 0x9204 | EVB_WORD_COUNT_1 | EVB debug word counters: [31:16] wc_ddr_to_tx, [15:0] wc_gbe_ddr_fifo |
| 0x9208 | EVB_WORD_COUNT_2 | EVB debug word counters: [31:16] wc_output_stream, [15:0] wc_bufmgr_output |
| 0x920C | EVB_WORD_COUNT_3 | EVB debug word counters: [15:0] wc_gbe_rx (10GbE RX data words into buffer manager) |
| 0x9210 | EVB_STALL_TX_CREDIT | (2026-10-06, read-only) EVB3 stall-time counter: user_clk clocks (4 ns) the 10GbE TX sat idle with words for the current destination but no RX credit from it (0x9370 bit 18 level). 32-bit wrap, SoftReset clear. Ratio = this / 0x9228. |
| 0x9214 | EVB_STALL_TX_WIRE | (2026-10-06, read-only) EVB3 stall-time counter: user_clk clocks a frame was on the 10GbE wire (TX wire monitor). Wire use = this / 0x9228. |
| 0x9218 | EVB_STALL_BM_LOCAL | (2026-10-06, read-only) EVB3 stall-time counter: user_clk clocks the buffer manager was streaming a SELF chunk to the DMA (no remote source FIFO is drained meanwhile). |
| 0x921C | EVB_STALL_BM_REMOTE_WAIT | (2026-10-06, read-only) EVB3 stall-time counter: user_clk clocks some remote source FIFO held words while no remote chunk was being served (remote data waiting behind a self chunk or the arbiter). |
| 0x9220 | EVB_STALL_DDR_READ | (2026-10-06, read-only) EVB3 stall-time counter: user_clk clocks a DDR read burst was in flight (the TX side's DDR read path; one burst at a time today). |
| 0x9224 | EVB_STALL_ROC_HELD | (2026-10-06, read-only) EVB3 stall-time counter: user_clk clocks the ROC input stream into EVB3 was valid and not ready (0x9370 bit 16 level): back-pressure into the ring mux. |
| 0x9228 | EVB_STALL_TIMEBASE | (2026-10-06, read-only) EVB3 stall-time counters' time base: user_clk clocks (4 ns) since SoftReset, 32-bit wrap (17 s). Divide the six counters above by this for duty ratios; read all seven close together. |
| 0x922C | EVB_STALL_ROC_HELD_SELF | (2026-10-07, read-only) EVB3 stall-time counter: user_clk clocks the ROC input was held WHILE A SELF subevent was streaming to the buffer manager (the DMA side is the holder). 0x922C + 0x9230 + 0x9234 + 0x9238 = 0x9224. |
| 0x9230 | EVB_STALL_ROC_HELD_REMOTE | (2026-10-07, read-only) EVB3 stall-time counter: user_clk clocks the ROC input was held WHILE A REMOTE subevent was streaming to the DDR write path (the CDC FIFO / DDR write is the holder). |
| 0x9234 | EVB_STALL_ROC_HELD_DRAIN | (2026-10-07, read-only) EVB3 stall-time counter: user_clk clocks the ROC input was held BETWEEN subevents waiting for the DDR write path to drain the previous remote subevent. |
| 0x9238 | EVB_STALL_ROC_HELD_OTHER | (2026-10-07, read-only) EVB3 stall-time counter: user_clk clocks the ROC input was held for any other reason (destination not yet decoded, self throttle bit 17, the idle beat between subevents). |
| 0x923C | EVB_STALL_DDR_WRITE | (2026-10-07, read-only) EVB3 stall-time counter: user_clk clocks a DDR write burst was in flight (the subevent-to-DDR write state machine inside a 32-word burst). |
| 0x9240 | EVB_STALL_DMA_BP | (2026-10-07, read-only) EVB3 stall-time counter: user_clk clocks EVB3's output register held a word the PCIe DMA engine would not take (m_axis_tvalid and not axi_str_c2s0_tready) = the exact share behind 0x9370 bit 23, which is only polled. 32-bit wrap, SoftReset clear; share = this / 0x9228. |
| 0x9244 | EVB_STALL_DMA_BP_MAX | (2026-10-07, read-only) longest single DMA hold since SoftReset, in user_clk clocks (4 ns): a value of 65536 or more means the engine refused for 262 us or longer at least once. |
| 0x9248 | EVB_DMA_WORDS_OUT | (2026-10-07, read-only) 64-bit words the PCIe engine accepted from EVB3 (every word: data, FAFA headers, DMA-close fillers), 32-bit wrap. Delivered rate = delta(0x9248) x 8 B / (delta(0x9228) x 4 ns). |
| 0x925C | EVB_TX_HIST_STATUS | (2026-10-13, read-only) TX packet history status. [31] frozen (1 = a retransmission was requested, log is frozen), [30:26] destination of the first retransmission request, [25:18] peer's last-acknowledged sequence number (the packet before the drop), [17:10] reserved (0), [9:0] number of entries written (0–1023). Check this before reading 0x91F0. SoftReset clears. |
| 0x9280 | RAWDATA_FF0FREQ | Firefly 0 reference clock frequency |
| 0x9284 | RAWDATA_FF0CONT | Firefly 0 oscillator control: [31] reset |
| 0x9288 | RAWDATA_FF0CONFLOW | Firefly 0 IIC: [31:16] address, [15:8] write data, [7:0] read data |
| 0x928C | RAWDATA_FF0CONFHIGH | Firefly 0 IIC CSR: [1] read enable, [0] write enable |
| 0x9290 | RAWDATA_FF1FREQ | Firefly 1 reference clock frequency |
| 0x9294 | RAWDATA_FF1CONT | Firefly 1 oscillator control: [31] reset |
| 0x9298 | RAWDATA_FF1CONFLOW | Firefly 1 IIC: [31:16] address, [15:8] write data, [7:0] read data |
| 0x929C | RAWDATA_FF1CONFHIGH | Firefly 1 IIC CSR: [1] read enable, [0] write enable |
| 0x92A0 | RAWDATA_FF2FREQ | Firefly 2 reference clock frequency |
| 0x92A4 | RAWDATA_FF2CONT | Firefly 2 oscillator control: [31] reset |
| 0x92A8 | RAWDATA_FF2CONFLOW | Firefly 2 IIC: [31:16] address, [15:8] write data, [7:0] read data |
| 0x92AC | RAWDATA_FF2CONFHIGH | Firefly 2 IIC CSR: [1] read enable, [0] write enable |
| 0x92B0 | TXPRBSCTL | TX PRBS control |
| 0x92B4 | RXPRBSCTL | RX PRBS control; read also returns [31:25] rxprbserr |
| 0x92C0 | DTCMODELOOKUPCONTROL | DTC mode lookup table control: [16] and [2:0] |
| 0x92D0 | SERDESTXRXPD | SERDES TX/RX power-down control [15:0] |
| 0x92F0 | DDR_TEST | DDR memory test: [8] complete, [7] error latch, [0] enable |

---

## 0x9300 – 0x93FF: SERDES Config / Retransmit / Errors / Unlock Counters

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x9300 | SERDESTXRXINVERT | SERDES TX/RX polarity invert: [14:8] TX, [6:0] RX |
| 0x9308 | JACSR | Jitter Attenuator CSR: [5:4] input select, [0] reset; read also returns [11:8] LOS, [7] LOL |
| 0x9314 | RAWDATA_SFPCONT | SFP oscillator control: [31] reset |
| 0x9318 | RAWDATA_SFPCONFLOW | SFP oscillator IIC: [31:16] address, [15:8] write data, [7:0] read data |
| 0x931C | RAWDATA_SFPCONFHIGH | SFP oscillator IIC CSR: [1] read enable, [0] write enable |
| 0x9320 | RETRANSMITCOUNT0 | Deprecated — Retransmit count ROC 0 |
| 0x9324 | RETRANSMITCOUNT1 | Deprecated — Retransmit count ROC 1 |
| 0x9328 | RETRANSMITCOUNT2 | Deprecated — Retransmit count ROC 2 |
| 0x932C | RETRANSMITCOUNT3 | Deprecated — Retransmit count ROC 3 |
| 0x9330 | RETRANSMITCOUNT4 | Deprecated — Retransmit count ROC 4 |
| 0x9334 | RETRANSMITCOUNT5 | Deprecated — Retransmit count ROC 5 |
| 0x9340 | MISSED_CFO_PACKET_COUNT_0 | Missed CFO packet count ROC 0; write to clear |
| 0x9344 | MISSED_CFO_PACKET_COUNT_1 | Missed CFO packet count ROC 1; write to clear |
| 0x9348 | MISSED_CFO_PACKET_COUNT_2 | Missed CFO packet count ROC 2; write to clear |
| 0x934C | MISSED_CFO_PACKET_COUNT_3 | Missed CFO packet count ROC 3; write to clear |
| 0x9350 | MISSED_CFO_PACKET_COUNT_4 | Missed CFO packet count ROC 4; write to clear |
| 0x9354 | MISSED_CFO_PACKET_COUNT_5 | Missed CFO packet count ROC 5; write to clear |
| 0x9360 | LOCAL_EVENT_DROP_COUNT | Local event drop count; write to clear |
| 0x9364 | SUB_EVENT_RECEIVE_TIMER_PRESET | Sub-event receive timer preset |
| 0x9368 | EVBPRBS | EVB PRBS control/status: [31] error latched (write to clear), [14:12] and [10:8] PRBS select, [1:0] enable |
| 0x9370 | EVBERROR | EVB3 error/status: [15:0] sticky error flags, cleared ONLY by SoftReset (write has no effect); [31:16] live status. Bits: [0] RX buffer write-while-full (words lost) [1] RX seq gap (packet lost) [2] EVB frame addressed to us rejected (source offset > 31) [3] DDR write CDC underflow (corrupt burst) [4] TX FSM fault, or (2026-09-19) a TX frame still on the wire after its destination window closed (overlaps the next sender at the switch) [5] credit violation [6] local stream misaligned (non-header at record boundary; passed as 1-word chunk) [7] RX stats collision [8] DDR read stream desync (bad count quadword dropped) [9] staging FIFO write-while-full [10] TX payload underflow; [11] TX frame malformed on the wire (sender-side monitor, 2026-09-15), or (2026-10-06) wire data words != header byte count / bad byte-count field; [12] RX frame wire length != declared byte count (2026-09-16); [13] RX FCS mismatch on a 0x78-framed frame (2026-09-16; 0x33 frames not checked); [14] ROC-input record: subevent header tag != first ROC fragment tag (2026-09-16); [15] ROC-input record: beats != count quadword or header bytes < count (2026-09-16); [16] ROC held [17] self-throttle [18] credit throttle [19] DDR almost full [20] DDR CDC FIFO full [21] staging no slot [22] RX buffer >= 3/4 [23] DMA backpressure [24] frontier valid [25] DDR calib done [26] frame not for us seen (non-EVB start, or dest MAC DTC byte / partition byte != ours and not broadcast: switch flooding, other partition), sticky informational (2026-09-09). Was unconnected + latch commented out before 2026-09-08. [31:27] (2026-09-24) RX FCS-bad frame count since SoftReset / RX enable, saturating at 31 (was reserved 0): tells whether bit 13 fired once at startup or keeps firing; 2026-09-28: status bit 27 = RESEND_ACTIVE (live), FCS-bad count moved to [31:28] (4 bits) |
| 0x9374 | VFIFOSERDESERROR | VFIFO SERDES error flags (latched); write to clear |
| 0x9378 | VFIFOPCIERROR | VFIFO PCI error flags (latched); write to clear |
| 0x9380 | RINGERROR0 | Ring status and errors, link 0 (see bit map below) |
| 0x9384 | RINGERROR1 | Ring status and errors, link 1 |
| 0x9388 | RINGERROR2 | Ring status and errors, link 2 |
| 0x938C | RINGERROR3 | Ring status and errors, link 3 |
| 0x9390 | RINGERROR4 | Ring status and errors, link 4 |
| 0x9394 | RINGERROR5 | Ring status and errors, link 5 |
| 0x9398 | RINGERROR6 | Ring status and errors, link 6; write to configure ring settings |
| 0x939C | RINGERROR7 | Ring status and errors, link 7 |

### 0x9380-0x939C RINGERROR bit map (per link, ROC links 0-5)

Layout: `[31:16] = status` (bit 20 = external ROC emulator error since 2026-10-02), `[15] = internal emulator error`, `[14:0] = link errors`

| Bit(s) | Name | Description |
|--------|------|-------------|
| 0 | tx_s2c_overflow | S2C FIFO overflow (tx) |
| 1 | tx_invalid_s2c | Invalid S2C packet received (tx) |
| 2 | tx_dcs_pending | DCS pending stuck (tx) |
| 3 | tx_ewm_hbp_mismatch | EWM/HBP count mismatch (tx): sent HBPs lead sent EWMs by more than 1, OR (2026-09-25) a heartbeat arrived from the CFO before the previous heartbeat's Event Window Marker did (marker lost upstream, or CFO/link enable toggled between heartbeat and marker) |
| 4 | pll_unstable | SERDES PLL lock lost |
| 5 | rxcdr_unstable | SERDES RX CDR lock lost |
| 6 | rx_overflow | RX FIFO overflow |
| 7 | rx_invalid_packet | Invalid packet type received |
| 8 | rx_dcs_timeout | DCS or DRP response timeout |
| 9 | rx_dcs_c2s_overflow | DCS C2S FIFO overflow |
| 10 | tx_cfo_tag_def | CFO event tag definition error (tx) |
| 11 | tx_hbp_overflow | Downstream HBP-latch overrun (tx) |
| 12 | rx_out_of_band | ROC data arrived with no pending DCS/DRP (late response after timeout) |
| 13 | rx_buffer_not_emptied | Double-buffer residue purged on read-done (deadlock precursor) |
| 14 | tx_stray_ewm | Event Window Marker thrown away because no heartbeat had been seen on this link since reset (a SoftReset, link enable or CFO start landed between a heartbeat and its marker). Sticky; the marker never reaches the ROC. Added 2026-10-02; this bit was ext_emulator_error, now at bit 20 |
| 15 | int_emulator_error | Internal ROC emulator error |
| [19:16] | tx_fsm | Tx handling FSM state |
| 20 | ext_emulator_error | External ROC emulator error (moved from bit 14 on 2026-10-02) |
| [31:21] | — | Reserved (zero) |

Bits [13:0] are latched `_ever` flags, cleared by link reset.

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x93A0 | FFCSR | Firefly CSR: read {5'b0, present[2:0], 5'b0, interrupt[2:0], 5'b0, select[2:0], 5'b0, reset[2:0]} = present at [26:24] (bit 25 = Firefly 1 = EVB link RX), interrupt [18:16], select [10:8], reset [2:0]; write [10:8] select, [2:0] reset (read layout corrected 2026-09-24 from register_map.v, HW read 0x03030000) |
| 0x93A4 | SFPCSR | SFP CSR: read [31] present, [30:16] status, write [15:0] control |
| 0x93B0 | RXCDRUNLOCKCOUNT0 | RX CDR unlock counter, link 0; write to clear |
| 0x93B4 | RXCDRUNLOCKCOUNT1 | RX CDR unlock counter, link 1; write to clear |
| 0x93B8 | RXCDRUNLOCKCOUNT2 | RX CDR unlock counter, link 2; write to clear |
| 0x93BC | RXCDRUNLOCKCOUNT3 | RX CDR unlock counter, link 3; write to clear |
| 0x93C0 | RXCDRUNLOCKCOUNT4 | RX CDR unlock counter, link 4; write to clear |
| 0x93C4 | RXCDRUNLOCKCOUNT5 | RX CDR unlock counter, link 5; write to clear |
| 0x93C8 | RXCDRUNLOCKCOUNT6 | RX CDR unlock counter, link 6 (CFO); write to clear |
| 0x93CC | JA_LOLCOUNT | Jitter attenuator loss-of-lock count; write to clear |
| 0x93D0 | CFOLINKEVENSTARTERRORCOUNT | CFO link event start error count; write to clear |
| 0x93D4 | CFOLINKCLOCK40MHZERRORCOUNT | CFO link 40 MHz clock marker error count; write to clear |
| 0x93D8 | DUMPSUBEVENTCOUNTSERDES | Dump subevent count (SERDES side); write to clear |
| 0x93DC | DUMPFRAGMENTCOUNTPCI | Dump fragment count (PCI side); write to clear |
| 0x93E0 | RTF_HIST_IDELAY | RTF histogram bins + IDELAY readback (read-only, see below) |
| 0x93F8 | SOFTWARE_DATAREQUEST_LO | Software data request event window tag [31:0]; write triggers tag write enable |
| 0x93FC | SOFTWARE_DATAREQUEST_HI | Software data request event window tag [47:32] |

### 0x93E0 RTF_HIST_IDELAY (read-only)

RTF punched clock histogram bins and IDELAY tap readback.

| Bit(s) | Field | Description |
|--------|-------|-------------|
| [31:27] | idelay_tap_count_value | Current IDELAYE2 tap count (~78 ps/tap) |
| [26] | idelay_rdy | IDELAYCTRL ready |
| [25:23] | rtf_hist_bin[4] | Histogram bin 4 count (0-7) |
| [22:20] | rtf_hist_bin[3] | Histogram bin 3 count (0-7) |
| [19:17] | rtf_hist_bin[2] | Histogram bin 2 count (0-7) |
| [16:14] | rtf_hist_bin[1] | Histogram bin 1 count (0-7) |
| [13:11] | rtf_hist_bin[0] | Histogram bin 0 count (0-7) |
| [10] | rtf_hist_saturated | A bin has reached count 7 |
| [9:7] | rtf_hist_saturated_bin | Which bin saturated (0-4; 7 = none yet) |
| [6:0] | — | Reserved (reads 0) |

---

## 0x9400 – 0x94FF: FPGA PROM / ICAPE / Diagnostics / Sequence Error

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x9400 | PROM_WR_ADDR | FPGA PROM write data and request |
| 0x9404 | PROM_RD_STAT | FPGA PROM read status: [31:16] SR, [1] FIFO full, [0] ready |
| 0x9408 | ICAPE_ADDR | ICAPE interface: write data/trigger; read [1] FIFO full, [0] FIFO empty |
| 0x9410 | SLOW_OPTICAL | Slow optical link diagnostic: [5:4] SMA input OK error, [3:0] RX OK error; write to clear |
| 0x9414 | SEQUENCEERRORINDUCEENABLES | Sequence error induce enables: [13:8] and [5:0] per-link |
| 0x9418 | SEQUENCEERRORINDUCEPACKET0 | Sequence error induce packet, ROC 0 |
| 0x941C | SEQUENCEERRORINDUCEPACKET1 | Sequence error induce packet, ROC 1 |
| 0x9420 | SEQUENCEERRORINDUCEPACKET2 | Sequence error induce packet, ROC 2 |
| 0x9424 | SEQUENCEERRORINDUCEPACKET3 | Sequence error induce packet, ROC 3 |
| 0x9428 | SEQUENCEERRORINDUCEPACKET4 | Sequence error induce packet, ROC 4 |
| 0x942C | SEQUENCEERRORINDUCEPACKET5 | Sequence error induce packet, ROC 5 |
| 0x9490 | DATAPENDINGDIAGNOSTICTIMER0 | Data pending diagnostic timer FIFO, ROC 0; read pops FIFO, write resets |
| 0x9494 | DATAPENDINGDIAGNOSTICTIMER1 | Data pending diagnostic timer FIFO, ROC 1; read pops FIFO, write resets |
| 0x9498 | DATAPENDINGDIAGNOSTICTIMER2 | Data pending diagnostic timer FIFO, ROC 2; read pops FIFO, write resets (2026-10-14: the RTL had 0x9495, unaligned and never decoded, so ROC 2's timer was unreadable on every bitfile before this fix) |
| 0x949C | DATAPENDINGDIAGNOSTICTIMER3 | Data pending diagnostic timer FIFO, ROC 3; read pops FIFO, write resets |
| 0x94A0 | DATAPENDINGDIAGNOSTICTIMER4 | Data pending diagnostic timer FIFO, ROC 4; read pops FIFO, write resets |
| 0x94A4 | DATAPENDINGDIAGNOSTICTIMER5 | Data pending diagnostic timer FIFO, ROC 5; read pops FIFO, write resets |

---

## 0x9500 – 0x95FF: Link Error Counters

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x9500 | RXNOTINTABLEERRORCOUNT0 | RX not-in-table error count, link 0 |
| 0x9504 | RXNOTINTABLEERRORCOUNT1 | RX not-in-table error count, link 1 |
| 0x9508 | RXNOTINTABLEERRORCOUNT2 | RX not-in-table error count, link 2 |
| 0x950C | RXNOTINTABLEERRORCOUNT3 | RX not-in-table error count, link 3 |
| 0x9510 | RXNOTINTABLEERRORCOUNT4 | RX not-in-table error count, link 4 |
| 0x9514 | RXNOTINTABLEERRORCOUNT5 | RX not-in-table error count, link 5 |
| 0x9518 | RXNOTINTABLEERRORCOUNT6 | RX not-in-table error count, link 6 (CFO) |
| 0x9590 | TENGBERXPACKETERRORCOUNT | 10 GbE RX packet error count. Legacy EVB only: in EVB3 builds the source wire is undriven (always reads 0; EVB3 has no RX FCS check) |
| 0x95A0 | JA_LOSCOUNT_0 | Jitter attenuator loss-of-signal count 0; write to clear |
| 0x95A4 | JA_LOSCOUNT_1 | Jitter attenuator loss-of-signal count 1; write to clear |

---

## 0x9600 – 0x96FF: Secret Register / ROC Emulation / Packet Counters / Diagnostics

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x9600 | DOUBLESECRETREGISTER | Double secret register (general-purpose RW) |
| 0x9610 | RAWDATA_ROC_EMULATION_INTERPACKET_DEL0 | ROC emulation inter-packet delay [4*6-1:0] for ROCs 0–5 |
| 0x9630 | TRANSMIT_DR_COUNT0 | Transmitted data request count, ROC 0 |
| 0x9634 | TRANSMIT_DR_COUNT1 | Transmitted data request count, ROC 1 |
| 0x9638 | TRANSMIT_DR_COUNT2 | Transmitted data request count, ROC 2 |
| 0x963C | TRANSMIT_DR_COUNT3 | Transmitted data request count, ROC 3 |
| 0x9640 | TRANSMIT_DR_COUNT4 | Transmitted data request count, ROC 4 |
| 0x9644 | TRANSMIT_DR_COUNT5 | Transmitted data request count, ROC 5 |
| 0x9648 | TRANSMIT_CFO_ClkM_COUNT | Transmitted CFO 40 MHz clock marker count |
| 0x9650 | TRANSMIT_HB_COUNT0 | Transmitted heartbeat count, ROC 0: heartbeat PACKETS actually sent on the link (with 0x9114[24] set: non-null + the one close-out null per non-null run; blocked nulls are not counted) |
| 0x9654 | TRANSMIT_HB_COUNT1 | Transmitted heartbeat count, ROC 1 |
| 0x9658 | TRANSMIT_HB_COUNT2 | Transmitted heartbeat count, ROC 2 |
| 0x965C | TRANSMIT_HB_COUNT3 | Transmitted heartbeat count, ROC 3 |
| 0x9660 | TRANSMIT_HB_COUNT4 | Transmitted heartbeat count, ROC 4 |
| 0x9664 | TRANSMIT_HB_COUNT5 | Transmitted heartbeat count, ROC 5 |
| 0x9668 | TRANSMIT_HB_COUNT6 | Heartbeat packets seen on the CFO link (all, null or not, before any blocking); the ROC links' 0x9650.. can only be <= this |
| 0x966C | CFO_HB_FORWARDED_COUNT | CFO heartbeats forwarded to ROCs (Heartbeat_sig) |
| 0x9670 | RECEIVE_DH_COUNT0 | Received data header count, ROC 0 |
| 0x9674 | RECEIVE_DH_COUNT1 | Received data header count, ROC 1 |
| 0x9678 | RECEIVE_DH_COUNT2 | Received data header count, ROC 2 |
| 0x967C | RECEIVE_DH_COUNT3 | Received data header count, ROC 3 |
| 0x9680 | RECEIVE_DH_COUNT4 | Received data header count, ROC 4 |
| 0x9684 | RECEIVE_DH_COUNT5 | Received data header count, ROC 5 |
| 0x9688 | CFO_CDC_DIAG | CFO CDC diagnostics: [31:16] parity mismatch count, [15:0] batch slip count |
| 0x9690 | RECEIVE_DP_COUNT0 | Received data packet count, ROC 0 |
| 0x9694 | RECEIVE_DP_COUNT1 | Received data packet count, ROC 1 |
| 0x9698 | RECEIVE_DP_COUNT2 | Received data packet count, ROC 2 |
| 0x969C | RECEIVE_DP_COUNT3 | Received data packet count, ROC 3 |
| 0x96A0 | RECEIVE_DP_COUNT4 | Received data packet count, ROC 4 |
| 0x96A4 | RECEIVE_DP_COUNT5 | Received data packet count, ROC 5 |
| 0x96A8 | CFO_HB_CRCFAIL_COUNT | CFO heartbeats dropped on CRC mismatch |
| 0x96AC | CFO_HB_PARSEINCOMPLETE_COUNT | CFO heartbeats whose parse never completed |
| 0x96B0 | EVBDIAGNOSTICPACKETFIFO0 | EVB diagnostic packet FIFO data [31:0]; read pops FIFO, write resets |
| 0x96B4 | EVBDIAGNOSTICPACKETFIFO1 | EVB diagnostic packet FIFO data [63:32]; read pops FIFO |

---

## 0x96D0 – 0x971F: Link Diagnostic FIFOs

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x96D0 | RXDATADIAGFIFO0 | RX data diagnostic FIFO, link 0; read pops FIFO, write resets |
| 0x96D4 | RXDATADIAGFIFO1 | RX data diagnostic FIFO, link 1; read pops FIFO, write resets |
| 0x96D8 | RXDATADIAGFIFO2 | RX data diagnostic FIFO, link 2; read pops FIFO, write resets |
| 0x96DC | RXDATADIAGFIFO3 | RX data diagnostic FIFO, link 3; read pops FIFO, write resets |
| 0x96E0 | RXDATADIAGFIFO4 | RX data diagnostic FIFO, link 4; read pops FIFO, write resets |
| 0x96E4 | RXDATADIAGFIFO5 | RX data diagnostic FIFO, link 5; read pops FIFO, write resets |
| 0x96E8 | TXDATADIAGFIFO0 | TX data diagnostic FIFO, link 0; read pops FIFO, write resets |
| 0x96EC | TXDATADIAGFIFO1 | TX data diagnostic FIFO, link 1; read pops FIFO, write resets |
| 0x96F0 | TXDATADIAGFIFO2 | TX data diagnostic FIFO, link 2; read pops FIFO, write resets |
| 0x96F4 | TXDATADIAGFIFO3 | TX data diagnostic FIFO, link 3; read pops FIFO, write resets |
| 0x96F8 | TXDATADIAGFIFO4 | TX data diagnostic FIFO, link 4; read pops FIFO, write resets |
| 0x96FC | TXDATADIAGFIFO5 | TX data diagnostic FIFO, link 5; read pops FIFO, write resets |
| 0x9700 | RXDATADIAGFIFO6 | RX data diagnostic FIFO, link 6 (CFO); read pops FIFO, write resets |
| 0x9708 | TXDATADIAGFIFO6 | TX data diagnostic FIFO, link 6 (CFO); read pops FIFO, write resets |
| 0x9710 | C2SDIAGFIFO0 | C2S (card-to-system) diagnostic FIFO [31:0]; read pops FIFO, write resets |
| 0x9714 | C2SDIAGFIFO1 | C2S diagnostic FIFO [63:32]; read pops FIFO |

---

## 0x9720 – 0x974F: Bandwidth Monitors

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x9720 | RXBYTESPERSEC0 | RX bytes per second, link 0 |
| 0x9724 | RXBYTESPERSEC1 | RX bytes per second, link 1 |
| 0x9728 | RXBYTESPERSEC2 | RX bytes per second, link 2 |
| 0x972C | RXBYTESPERSEC3 | RX bytes per second, link 3 |
| 0x9730 | RXBYTESPERSEC4 | RX bytes per second, link 4 |
| 0x9734 | RXBYTESPERSEC5 | RX bytes per second, link 5 |
| 0x9740 | VFSERDESINBYTESPERSEC | VFIFO SERDES AXI input bytes per second |
| 0x9744 | VFSERDESOUTBYTESPERSEC | VFIFO SERDES AXI output bytes per second |
| 0x9748 | VFPCIINBYTESPERSEC | VFIFO PCI AXI input bytes per second |
| 0x974C | VFPCIOUTBYTESPERSEC | VFIFO PCI AXI output bytes per second |

---

## 0xA400 – 0xA4FF: Extended Counters (EWM / DH Timeout / Null HB)

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0xA400 | TRANSMIT_EWM_COUNT0 | Transmitted event window marker count, ROC 0: markers actually sent (a blocked null's marker is not counted). Equals 0x9650 when the CFO is quiet; a poll taken while a heartbeat is in flight (marker ~850 ns behind the packet) reads one less. Since 2026-10-02 no marker is sent until a heartbeat has been seen after a reset, so a SoftReset while the CFO runs leaves this at 0 (before, one stray marker per link) |
| 0xA404 | TRANSMIT_EWM_COUNT1 | Transmitted event window marker count, ROC 1 |
| 0xA408 | TRANSMIT_EWM_COUNT2 | Transmitted event window marker count, ROC 2 |
| 0xA40C | TRANSMIT_EWM_COUNT3 | Transmitted event window marker count, ROC 3 |
| 0xA410 | TRANSMIT_EWM_COUNT4 | Transmitted event window marker count, ROC 4 |
| 0xA414 | TRANSMIT_EWM_COUNT5 | Transmitted event window marker count, ROC 5 |
| 0xA418 | TRANSMIT_EWM_COUNT6 | Event window markers seen on the CFO link (all). 0x9668 - 0xA418 = heartbeats whose marker never arrived (run stop between heartbeat and marker, or a heartbeat still waiting for its marker at the poll) |
| 0xA420 | RECEIVE_DHTIMEOUT_COUNT0 | Received data header timeout count, ROC 0 |
| 0xA424 | RECEIVE_DHTIMEOUT_COUNT1 | Received data header timeout count, ROC 1 |
| 0xA428 | RECEIVE_DHTIMEOUT_COUNT2 | Received data header timeout count, ROC 2 |
| 0xA42C | RECEIVE_DHTIMEOUT_COUNT3 | Received data header timeout count, ROC 3 |
| 0xA430 | RECEIVE_DHTIMEOUT_COUNT4 | Received data header timeout count, ROC 4 |
| 0xA434 | RECEIVE_DHTIMEOUT_COUNT5 | Received data header timeout count, ROC 5 |
| 0xA440 | TRANSMIT_NULL_HB_COUNT0 | Null heartbeats received from the CFO for ROC 0, blocked or sent (NOT the number of null packets on the wire; see 0xA460 for suppressed and the identities below) |
| 0xA444 | TRANSMIT_NULL_HB_COUNT1 | Null heartbeats received from the CFO, ROC 1 (blocked or sent) |
| 0xA448 | TRANSMIT_NULL_HB_COUNT2 | Null heartbeats received from the CFO, ROC 2 (blocked or sent) |
| 0xA44C | TRANSMIT_NULL_HB_COUNT3 | Null heartbeats received from the CFO, ROC 3 (blocked or sent) |
| 0xA450 | TRANSMIT_NULL_HB_COUNT4 | Null heartbeats received from the CFO, ROC 4 (blocked or sent) |
| 0xA454 | TRANSMIT_NULL_HB_COUNT5 | Null heartbeats received from the CFO, ROC 5 (blocked or sent) |
| 0xA460 | TRANSMIT_BLOCKED_HB_COUNT0 | Null heartbeats SUPPRESSED for ROC 0: packet and Event Window Marker both blocked by 0x9114[24] (added 2026-10-02) |
| 0xA464 | TRANSMIT_BLOCKED_HB_COUNT1 | Null heartbeats suppressed, ROC 1 |
| 0xA468 | TRANSMIT_BLOCKED_HB_COUNT2 | Null heartbeats suppressed, ROC 2 |
| 0xA46C | TRANSMIT_BLOCKED_HB_COUNT3 | Null heartbeats suppressed, ROC 3 |
| 0xA470 | TRANSMIT_BLOCKED_HB_COUNT4 | Null heartbeats suppressed, ROC 4 |
| 0xA474 | TRANSMIT_BLOCKED_HB_COUNT5 | Null heartbeats suppressed, ROC 5 |
| 0xA480 | TRANSMIT_NONNULL_HB_COUNT0 | Non-null heartbeats sent to ROC 0 (added 2026-10-02; replaces software's 0x9668 - 0xA440 subtraction, which is invalid across two sample FIFOs) |
| 0xA484 | TRANSMIT_NONNULL_HB_COUNT1 | Non-null heartbeats sent, ROC 1 |
| 0xA488 | TRANSMIT_NONNULL_HB_COUNT2 | Non-null heartbeats sent, ROC 2 |
| 0xA48C | TRANSMIT_NONNULL_HB_COUNT3 | Non-null heartbeats sent, ROC 3 |
| 0xA490 | TRANSMIT_NONNULL_HB_COUNT4 | Non-null heartbeats sent, ROC 4 |
| 0xA494 | TRANSMIT_NONNULL_HB_COUNT5 | Non-null heartbeats sent, ROC 5 |

Per-link heartbeat counter identities (all 16-bit, wrap at 65536; 0xA440/0xA460/0xA480 for one link are sampled in the SAME instant, so their arithmetic is exact at any time):

| Identity | Meaning |
|---|---|
| 0x9650 (packets sent) = 0xA480 (non-null) + (0xA440 (null received) - 0xA460 (suppressed)) | every packet is either non-null or a close-out null |
| 0xA440 - 0xA460 | close-out nulls sent (one per run of non-null heartbeats when 0x9114[24] is set) |
| 0xA400 (markers sent) == 0x9650 | on a quiet link; during traffic the marker trails its packet by ~850 ns, so a poll can read one less |
| 0x9668 (CFO heartbeats) >= 0x9650 | the ROC link can only send what the CFO delivered |

Do NOT subtract counters that come through different sample paths (e.g. 0x9668 on the CFO link minus 0xA440 on a ROC link): each is refreshed by its own slow-monitor FIFO at its own moment and wraps on its own, so the difference is meaningless during traffic (hardware 2026-10-02: 61229 - 61112 read as 4424 in software). Use 0xA480 for non-null directly.

Hardware example (Stm02, 2026-10-02, bits 24+25 set, 5 events sent): 0x9650 = 0xA400 = 6 = 5 non-null + 1 close-out null; 0xA440 kept counting the idle nulls (11712) while nothing was sent.

---|---|---|
| 0x9668 CFO HB | 88 | 56 non-null + 32 null heartbeats delivered by the CFO |
| 0xA418 CFO EWM | 86 | 88 minus one heartbeat that lost its marker (CFO stopped between heartbeat and marker) minus the last heartbeat still waiting for its marker at the poll |
| 0xA440 link 0 null | 32 | all 32 nulls seen (7 sent as close-outs, 25 blocked) |
| 0x9650 link 0 HB | 63 | 56 non-null + 7 close-out nulls on the wire |
| 0xA400 link 0 EWM | 63 | one marker per packet sent |

So "non-null" is 0x9668 minus 0xA440 (56), and the ROC link sends that plus one close-out per non-null run. The three counters are not expected to be equal to each other; the equalities to check are 0x9650 == 0xA400 (quiet link) and 0x9650 == (0x9668 - 0xA440) + number of non-null runs. All register copies are refreshed through a slow-monitor FIFO, so a poll during traffic can read any one of them one sample behind.

---

## 0xB000 – 0xB0FF: DTC Mode Lookup Table

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0xB000–0xB0FF | DTC_MODE_LOOKUP_BASE | DTC mode lookup table (256 bytes, 8-bit entries). Access via low/high 32-bit word halves. |

---

## Deprecated Registers

These registers have parameter declarations but are commented out in the RTL and are not active in read or write decode logic.

| Address | Register Name | Purpose |
|---------|---------------|---------|
| 0x900C | MON_TX_PCIE_BYTE_COUNT | Deprecated — PCIe TX byte count |
| 0x9010 | MON_RX_PCIE_BYTE_COUNT | Deprecated — PCIe RX byte count |
| 0x9014 | MON_TX_PCIE_PAYLOAD_COUNT | Deprecated — PCIe TX payload count |
| 0x9018 | MON_RX_PCIE_PAYLOAD_COUNT | Deprecated — PCIe RX payload count |
| 0x901C–0x9030 | MON_INIT_FC_* | Deprecated — PCIe flow control initial credits |
| 0x924C–0x9278 | RECEIVE/TRANSMIT_BYTE/PACKET_COUNT_* | Deprecated — Per-link byte/packet counters (0x9200–0x920C reused for EVB_WORD_COUNT_0–3 on 2026-08-27, 0x9210–0x9248 for the EVB stall-time counters on 2026-10-06/07) |
| 0x9280–0x92A4 | RAWDATA_M/S_AXIS_T* | Deprecated — AXI stream destination/ID registers |
| 0x9300–0x933C | START/END_ADDR/BURST_SIZE_P0–P3 | Deprecated — Virtual FIFO controller partition addresses |
| 0x9430–0x948C | DDR_GAS_GAUGE0/1_0–11 | Deprecated — DDR gas gauge registers |
| 0x94B0–0x94C4 | ROCEMINDUCETIMEOUTERROR0–5 | Deprecated — ROC emulation induce timeout error |
| 0x94D0–0x94E4 | ROCEMINDUCEEXTRAWORDERROR0–5 | Deprecated — ROC emulation induce extra word error |
| 0x9520–0x9538 | RXDISPARITYERRORCOUNT0–6 | Deprecated — RX disparity error counts per link |
| 0x9540–0x9558 | RXPRBSERRORCOUNT0–6 | Deprecated — RX PRBS error counts per link |
| 0x9560–0x957C | RXCRCERRORCOUNT0–6, RXCRCERRORCONTROL | Deprecated — RX CRC error counts and control |
| 0x93E0–0x93F0 | DCSRESPONSETIME, DCSRESPONSETIME2–4, FSMLOCKUPTIME | Deprecated — DCS response time and FSM lockup time |

---

## Reserved Address Gaps

Addresses within the 0x9000–0xB0FF range that are not listed above return 0x00000000 on read and generate a write error flag on write.

---

## RTL note: adding a read decode (register_map.v banks)

Invisible to software, but critical when **editing register_map.v**: the read
path is split for timing into 8 parallel case blocks, and a new register's
decode entry MUST go in the one block the output mux actually selects — an
entry in any other block silently reads 0 (no error).

1. **lo/hi half:** `addr[2]=0` → a `reg_rd_data_pre_lo[n]` case (drives read
   data [31:0]); `addr[2]=1` → a `reg_rd_data_pre_hi[n]` case (drives [63:32]).
2. **Bank by address range** (selection mux at register_map.v:~3314):

   | Bank | lo case (`reg_rd_addr_lo[n]`) | hi case (`reg_rd_addr_hi[n]`) |
   |:----:|-------------------------------|-------------------------------|
   | 0 | addr < 0x91D0 | addr < 0x91D4 |
   | 1 | 0x91D0 ≤ addr < 0x9408 | 0x91D4 ≤ addr < 0x93B4 |
   | 2 | 0x9408 ≤ addr < 0x9690 | 0x93B4 ≤ addr < 0x9674 |
   | 3 | addr ≥ 0x9690 | addr ≥ 0x9674 |

Example: 0x9200/0x9208 → lo bank 1; 0x9204/0x920C → hi bank 1. Found the hard
way 2026-08-27: EVB_WORD_COUNT_0..3 were decoded in lo bank 0 and always read
as 0 while the counters counted fine in ILA.
