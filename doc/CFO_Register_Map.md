# CFO Register Map

Register map for the CFO (Command Fanout Controller) firmware, as implemented in
[`YearMonDD_HH_VV.V/CFO/src/register_map.v`](YearMonDD_HH_VV.V/CFO/src/register_map.v).

Addresses are byte offsets into the CFO PCIe register space (AXI target of the
NWL DMA Back-End). All registers are 32 bits wide.

**Purpose key:**
- *Reserved* — address not decoded by the firmware (reads return 0, writes are ignored).
- *Deprecated* — register definition and/or logic is commented out or no longer used in the current firmware.

## Design Version / Status (0x9000)

| Address | Register Name | Register Purpose |
|---------|---------------|------------------|
| 0x9000 | DESIGN_VERSION1 | Firmware design version (date code, `FIRMWAREVERSION1`). |
| 0x9004 | DESIGN_VERSION2 | Firmware design version (`FIRMWAREVERSION2`). |
| 0x9008 | DESIGN_STATUS | DDR3/MIG status: bit 0 = calibration done, bit 1 = AXI interconnect/MIG shim reset (active low, self-setting after ~9 clocks), bits 5:2 = DDR3 FIFO empty flags. |
| 0x900C | VIVADO_VERSION | Vivado tool version used to build the firmware. (Formerly MON_TX_PCIE_BYTE_COUNT — Deprecated.) |
| 0x9010 | DEVICE_TEMP | FPGA die temperature from XADC (12-bit raw value). (Formerly MON_RX_PCIE_BYTE_COUNT — Deprecated.) |
| 0x9014 | DEVICE_VCCINT | FPGA VCCINT voltage from XADC. (Formerly MON_TX_PCIE_PAYLOAD_COUNT — Deprecated.) |
| 0x9018 | DEVICE_VCCAUX | FPGA VCCAUX voltage from XADC. (Formerly MON_RX_PCIE_PAYLOAD_COUNT — Deprecated.) |
| 0x901C | DEVICE_VCCBRAM | FPGA VCCBRAM voltage from XADC. (Formerly MON_INIT_FC_CPLD — Deprecated.) |
| 0x9020 | XADC_ALARM | Latched XADC alarm flags (bits 1:0); any write clears. (Formerly MON_INIT_FC_CPLH — Deprecated.) |
| 0x9024 | TIME_ALIVE | Time-alive counter since power-up (1 ms resolution). (Formerly MON_INIT_FC_NPD — Deprecated.) |
| 0x9028 | MON_INIT_FC_NPH | Deprecated — PCIe initial flow-control credit monitor (commented out). |
| 0x902C | MON_INIT_FC_PD | Deprecated — PCIe initial flow-control credit monitor (commented out). |
| 0x9030 | SCRATCH_REGISTER | Reserved — address defined but not implemented in read/write decode (reads return 0). (Formerly MON_INIT_FC_PH — Deprecated.) |
| 0x9034–0x90FC | — | Reserved |

## CFO Control and SERDES Raw-Data Registers (0x9100)

| Address | Register Name | Register Purpose |
|---------|---------------|------------------|
| 0x9100 | RAWDATA_CONTROL | CFO Control Register: bit 31 = Soft Reset (self-clearing), bit 27 = DDR Write Reset, bit 16 = LED 7, bit 9 = Enable Punched Markers, bit 8 = SERDES Global Reset, bit 3 = Loopback (delay measure) Test Launch, bit 2 = Accelerator RF-0 Marker Input Enable, bit 1 = Embedded Clock Marker Enable, bit 0 = Hard Reset (self-clearing). |
| 0x9104 | RAWDATA_DMA_SIZE | DMA size limits: bits 31:16 = max size, bits 15:0 = min size (clamped in hardware). |
| 0x9108 | RAWDATA_ENABLE_LB_CHECKER0 | Enable loopback checker per SERDES link (3 bits per link). |
| 0x910C | RAWDATA_CHECKER_STATUS0 | Checker/oscillator status; bit 2 = SERDES oscillator IIC error. |
| 0x9110 | — | Reserved |
| 0x9114 | RAWDATA_RING_ENABLE | Ring link enables: bits 7:0 = transmit enable, bits 15:8 = receive enable. |
| 0x9118 | RAWDATA_SERDES_RESET | SERDES resets: bit 31 = gang TX reset, bit 30 = gang RX reset, bits 23:16 = per-link TX reset, bits 15:8 = per-link PLL reset, bits 7:0 = per-link RX reset. |
| 0x911C | RAWDATA_RX_DISP_ERROR | Latched SERDES RX disparity errors (2 bits per link); write 1s to clear. |
| 0x9120 | RAWDATA_RX_NOTINTABLE | Latched SERDES RX not-in-table (8b/10b decode) errors (2 bits per link); write 1s to clear. |
| 0x9124 | RAWDATA_SERDES_UNLOCK | Latched SERDES PLL unlock flags (1 bit per link); write 1s to clear. |
| 0x9128 | RAWDATA_SERDES_PLL_LOCKED | SERDES PLL locked status (live). |
| 0x912C | RAWDATA_TXBUFSTATUS | Deprecated — SERDES TX buffer status (commented out). |
| 0x9130 | RAWDATA_RXBUFSTATUS | Deprecated — SERDES RX buffer status (commented out). |
| 0x9134 | RAWDATA_RXSTATUSREG | SERDES RX status (3 bits per link). |
| 0x9138 | RAWDATA_SERDESRESETDONE | SERDES reset-done status (1 bit per link). |
| 0x913C | RAWDATA_EYESCANDATAERRORREG | Deprecated — SERDES eye-scan data error (commented out). |
| 0x9140 | RAWDATA_RXCDRLOCK | SERDES RX CDR lock status (1 bit per link). |
| 0x9144 | RAWDATA_BEAMONTIMERPRESET | Deprecated — Beam-On timer preset (read/write logic commented out). |
| 0x9148 | ENABLEBEAMONMODE | Bit 0 = enable on-spill run-plan list processing. |
| 0x914C | ENABLEBEAMOFFMODE | Bit 0 = enable off-spill run-plan list processing. |
| 0x9150 | RAWDATA_PKT_SIZE | Ring packet length (bits 15:0). |
| 0x9154 | RUNPLAN_SUBRUN_EVT_LIMIT | Run-plan subrun event limit. (Formerly CLOCK40MHZINTERVAL — Deprecated.) |
| 0x9158 | RUNPLAN_SUBRUN_PRED_OFFSET | Run-plan subrun predecessor offset. |
| 0x915C | — | Reserved |
| 0x9160 | RAWDATA_SERDESREFCLKFREQ | SERDES reference clock frequency setting (default 0x09502F90 = 156.25 MHz). |
| 0x9164 | RAWDATA_SERDESREFCLKCONT | SERDES reference oscillator control; bit 31 = oscillator reset. |
| 0x9168 | RAWDATA_SERDESREFCLKCONFLOW | SERDES oscillator IIC0 interface: bits 31:16 = address, bits 15:8 = write data, bits 7:0 = read data (RO). |
| 0x916C | RAWDATA_SERDESREFCLKCONFHIGH | SERDES oscillator IIC0 control/status: bit 1 = read strobe, bit 0 = write strobe; reads return IIC0 busy status. |
| 0x9170–0x917C | — | Reserved |
| 0x9180 | RAWDATA_TIMESTAMP_PRESET0 | Timestamp preset bits 31:0. |
| 0x9184 | RAWDATA_TIMESTAMP_PRESET1 | Timestamp preset bits 47:32; writing latches the full preset (TimestampPresetEnable strobe). |
| 0x9188 | LINUX_TIME | Linux epoch time set by software (write strobes LinuxTimeSet). |
| 0x918C | RAWDATA_NUMROCS | Number of ROCs per link (4 bits per link). |
| 0x9190 | RAWDATA_FIFOERRORFLAGS0 | Latched TX data packet FIFO full flags (bits 7:0); write 1s to clear. |
| 0x9194–0x9198 | — | Reserved |
| 0x919C | RAWDATA_RX_Buffer_Error0 | Deprecated — RX buffer/packet/CRC error flags (commented out). |
| 0x91A0 | EWEMINTERVALTIME | Deprecated — Event Window Emulator interval time (commented out). |
| 0x91A4 | EVENTWINDOWHOLDOFFTIME | Deprecated — Event window holdoff count preset (commented out). |
| 0x91A8 | EVENTWINDOWTIMEOUT | Deprecated — Event window timeout status/clear (commented out). |
| 0x91AC | EVENTWINDOWTIMEOUTVALUE | Deprecated — Event window timeout value (commented out). |
| 0x91B0 | SLIDECOUNTLOCKVALUEPreLow | Deprecated — Slide count lock value preset low (commented out). |
| 0x91B4 | SLIDECOUNTLOCKVALUEPreHigh | Deprecated — Slide count lock value preset high (commented out). |
| 0x91B8 | SLIDECOUNTLOCKVALUELow | Deprecated — Slide count lock value low (commented out). |
| 0x91BC | SLIDECOUNTLOCKVALUEHigh | Deprecated — Slide count lock value high (commented out). |
| 0x91C0–0x91FC | — | Reserved |

## Marker / Packet Counters (0x9200)

| Address | Register Name | Register Purpose |
|---------|---------------|------------------|
| 0x9200 | RECEIVE_RF0_COUNT | Count of accelerator RF-0 markers received. (Formerly RECEIVE_BYTE_COUNT_0 — Deprecated.) |
| 0x9204–0x921C | RECEIVE_BYTE_COUNT_1..7 | Deprecated — per-link receive byte counters (commented out). |
| 0x9220–0x923C | RECEIVE_PACKET_COUNT_0..7 | Deprecated — per-link receive packet counters (commented out). |
| 0x9240 | TRANSMIT_HBP_COUNT | Count of Heartbeat Packets transmitted. (Formerly TRANSMIT_BYTE_COUNT_0 — Deprecated.) |
| 0x9244 | TRANSMIT_HBP_ONSPILL_COUNT | Count of Heartbeat Packets transmitted while on-spill (RF-0 derived on-spill flag set at time of send); bits 23:0, cleared by Soft Reset. (Formerly TRANSMIT_BYTE_COUNT_1 — Deprecated.) |
| 0x9248 | TRANSMIT_HBP_OFFSPILL_COUNT | Count of Heartbeat Packets transmitted while off-spill (on-spill flag clear at time of send); bits 23:0, cleared by Soft Reset. (Formerly TRANSMIT_BYTE_COUNT_2 — Deprecated.) |
| 0x924C–0x925C | TRANSMIT_BYTE_COUNT_3..7 | Deprecated — per-link transmit byte counters (commented out). |
| 0x9260 | TRANSMIT_EWM_COUNT | Count of Event Window Markers transmitted. (Formerly TRANSMIT_PACKET_COUNT_0 — Deprecated.) |
| 0x9264–0x927C | TRANSMIT_PACKET_COUNT_1..7 | Deprecated — per-link transmit packet counters (commented out). |

## FireFly Oscillator / IIC Registers (0x9280)

| Address | Register Name | Register Purpose |
|---------|---------------|------------------|
| 0x9280 | RAWDATA_FF0FREQ | FireFly 0 reference clock frequency setting. |
| 0x9284 | RAWDATA_FF0CONT | FireFly 0 oscillator control; bit 31 = oscillator reset. |
| 0x9288 | RAWDATA_FF0CONFLOW | FireFly 0 IIC2 interface: bits 31:16 = address, bits 15:8 = write data, bits 7:0 = read data (RO). |
| 0x928C | RAWDATA_FF0CONFHIGH | FireFly 0 IIC2 control/status: bit 1 = read strobe, bit 0 = write strobe; reads return IIC2 busy status. |
| 0x9290 | RAWDATA_FF1FREQ | FireFly 1 reference clock frequency setting. |
| 0x9294 | RAWDATA_FF1CONT | FireFly 1 oscillator control; bit 31 = oscillator reset. |
| 0x9298 | RAWDATA_FF1CONFLOW | FireFly 1 IIC3 interface: address / write data / read data. |
| 0x929C | RAWDATA_FF1CONFHIGH | FireFly 1 IIC3 control/status: read/write strobes and busy status. |
| 0x92A0 | RAWDATA_FF2FREQ | FireFly 2 reference clock frequency setting. |
| 0x92A4 | RAWDATA_FF2CONT | FireFly 2 oscillator control; bit 31 = oscillator reset. |
| 0x92A8 | RAWDATA_FF2CONFLOW | FireFly 2 IIC4 interface: address / write data / read data. |
| 0x92AC | RAWDATA_FF2CONFHIGH | FireFly 2 IIC4 control/status: read/write strobes and busy status. |
| 0x92B0–0x92CC | — | Reserved |
| 0x92D0 | SERDESTXRXPD | SERDES GT TX/RX power-down enables (bits 15:0). |
| 0x92D4–0x92FC | — | Reserved |

## DMA / Run Plan / PRBS / Delay Measure (0x9300)

| Address | Register Name | Register Purpose |
|---------|---------------|------------------|
| 0x9300 | DMAS2CADDRESS | DMA System-to-Card (S2C) address. |
| 0x9304 | DMAC2SADDRESS | DMA Card-to-System (C2S) address. |
| 0x9308 | DMAC2SBYTECOUNT | DMA C2S byte count; writing starts the C2S DMA transfer. |
| 0x930C | BEAMONBASE | On-spill (beam-on) run-plan base address. |
| 0x9310 | BEAMOFFBASE | Off-spill (beam-off) run-plan base address. |
| 0x9314 | RUNPLAN_BRAM_ADDRESS | Run-plan BRAM address; writing strobes the address into the run-plan interface. |
| 0x9318 | RUNPLAN_BRAM_DATA | Run-plan BRAM data; write stores data (write strobe), read returns BRAM data (read strobe). |
| 0x931C | RUNPLAN_EVENT_MODE0 | Current Event Window Mode monitor, bits 31:0 (RO). |
| 0x9320 | RUNPLAN_EVENT_MODE1 | Current Event Window Mode monitor, bits 47:32 (RO). |
| 0x9324 | RUNPLAN_EVENT_TAG0 | Current Event Window Tag monitor, bits 31:0 (RO). |
| 0x9328 | RUNPLAN_EVENT_TAG1 | Current Event Window Tag monitor, bits 47:32 (RO). |
| 0x932C | — | Reserved |
| 0x9330–0x934C | SERDES_PRBS_CONT0..7 | Per-link SERDES PRBS control/status: bit 31 = RX PRBS error (RO), bits 14:12 = TX PRBS select, bits 10:8 = RX PRBS select, bit 1 = TX PRBS force error, bit 0 = RX PRBS counter reset. One register per link (0–7). |
| 0x9350–0x935C | — | Reserved |
| 0x9360–0x937C | DELAY_MEASURE_VALUE0..7 | Measured loopback delay for links 0–7, ROC 0: bit 31 = done flag, bits 30:0 = measured delay value (RO). |
| 0x9380 | DELAY_MEASURE_CSR | Delay-measure control/status: bits 31:24 = link mask for launch debug output, bits 19:16 = count exponent, bits 7:0 = per-link done flags (RO). |
| 0x9384–0x938C | — | Reserved |
| 0x9390 | RAWDATA_M_AXIS_TDEST0 | AXI-Stream master TDEST monitor, bits 31:0 (RO). |
| 0x9394 | RAWDATA_M_AXIS_TDEST1 | AXI-Stream master TDEST monitor, bits 41:32 (RO). |
| 0x9398 | RAWDATA_S_AXIS_TDEST | AXI-Stream slave TDEST select (bits 5:0). |
| 0x939C | — | Reserved |
| 0x93A0 | FFCSR | FireFly control/status: present (RO), interrupt (RO), select, and reset bits for the three FireFly modules. |
| 0x93A4 | SFPCSR | SFP control/status: bit 31 = present (RO), bits 17:16 = status (RO), bits 1:0 = control. |
| 0x93A8–0x93FC | — | Reserved |

## FPGA PROM Programming / ICAPE (0x9400)

| Address | Register Name | Register Purpose |
|---------|---------------|------------------|
| 0x9400 | PROM_WR_ADDR | PROM programming write data; writing issues a PROM write request. |
| 0x9404 | PROM_RD_STAT | PROM status: bits 31:16 = status register, bit 1 = write FIFO full, bit 0 = PROM ready (RO). |
| 0x9408 | ICAPE_ADDR | ICAPE configuration port: write pushes a 32-bit word to ICAPE; reads return FIFO full/empty status. |
| 0x940C–0x945C | — | Reserved |

## Delay Measure Values per ROC (0x9460 – 0x987C)

Measured loopback delay per link, repeated for ROC depths 1–5
(bit 31 = done flag, bits 30:0 = measured value, all RO).
Note: this address range is intentionally out of numeric order relative to the
blocks below (see comment in `register_map.v`).

| Address | Register Name | Register Purpose |
|---------|---------------|------------------|
| 0x9460–0x947C | DELAY_MEASURE_VALUE0R1..7R1 | Measured delay for links 0–7, ROC 1. |
| 0x9560–0x957C | DELAY_MEASURE_VALUE0R2..7R2 | Measured delay for links 0–7, ROC 2. |
| 0x9660–0x967C | DELAY_MEASURE_VALUE0R3..7R3 | Measured delay for links 0–7, ROC 3. |
| 0x9760–0x977C | DELAY_MEASURE_VALUE0R4..7R4 | Measured delay for links 0–7, ROC 4. |
| 0x9860–0x987C | DELAY_MEASURE_VALUE0R5..7R5 | Measured delay for links 0–7, ROC 5. |

## Jitter Attenuator (0x9500)

| Address | Register Name | Register Purpose |
|---------|---------------|------------------|
| 0x9500 | JACSR | Jitter attenuator control/status: bits 14:12 = loss-of-signal inputs (RO), bit 11 = loss-of-lock (RO), bits 5:4 = input select, bit 0 = JA reset. |
| 0x9504 | JA_LOLCOUNT | Jitter attenuator loss-of-lock event counter; any write clears. |
| 0x9508 | JA_LOSCOUNT_0 | Jitter attenuator loss-of-signal event counter, input 0; any write clears. |
| 0x950C | JA_LOSCOUNT_1 | Jitter attenuator loss-of-signal event counter, input 1; any write clears. |
| 0x9510–0x955C | — | Reserved |

## SERDES Diagnostic FIFOs (0x96C0)

| Address | Register Name | Register Purpose |
|---------|---------------|------------------|
| 0x96C0–0x96DC | RXDATADIAGFIFO0..7 | RX data diagnostic FIFO for links 0–7: read pops one word from the FIFO; any write resets the FIFO. |
| 0x96E0–0x96FC | TXDATADIAGFIFO0..7 | TX data diagnostic FIFO for links 0–7: read pops one word from the FIFO; any write resets the FIFO. |

---

*Generated from `register_map.v` on the `rrivera/MC2connectionsToRTFworking` branch. Addresses not listed above are Reserved.*
