# otsdaq-mu2e — interfaces

This page lists what otsdaq-mu2e exposes to the rest of the DAQ, and what loads
or calls it. It is the primary deliverable of the Tier B review of 2026-10-06,
at commit `828cfe24` on branch `mu2e-sept-mega-review` (cut from `develop`).

**Evidence rules.** Sources are the `CMakeLists.txt` files, the public headers,
the `DEFINE_OTS_*` registration lines, `tools/`, `doc/`, and
`otsdaq-mu2e-config` for how plugins are selected. Implementation files were
opened only to resolve a symbol named in a header. The FE macro tables were
extracted mechanically from the `registerFEMacroFunction(...)` calls after
comments had been stripped, so commented-out registrations are excluded.

otsdaq core (`GatewaySupervisor`, `FESupervisor`, `RunInfoVInterface`) is not
checked out in this tree. Any statement about what otsdaq core does with a
plugin is marked *(otsdaq core, not verified here)*.

Source paths are relative to `otsdaq-mu2e/otsdaq-mu2e/` unless they start with
`tools/`, `doc/`, `test/` or a package name.

---

## 1. Build products

`project(otsdaq-mu2e VERSION 11.03.00)` (`CMakeLists.txt:13`), built with
cetmodules. The package's external dependencies are `otsdaq` 2.06.08,
`otsdaq-components` 2.06.08, `artdaq-mu2e` 1.05.02 and PostgreSQL 13.2
(`CMakeLists.txt:70-73`). The `mu2e-pcie-utils::DTCInterface`,
`mu2e-pcie-utils::CFOInterface` and `artdaq-core-mu2e::Overlays` targets arrive
through `artdaq-mu2e`'s `EXPORT`.

### 1.1 Link libraries other packages use

| CMake target | Contents | Defined at | Linked by |
|---|---|---|---|
| `otsdaq-mu2e::ROCCore` | `ROCCoreVInterface`, the base class of every ROC plugin | `ROCCore/CMakeLists.txt:2` | `otsdaq-mu2e-crv/otsdaq-mu2e-crv/FEInterfaces/CMakeLists.txt:4`, `otsdaq-mu2e-extmon/…/FEInterfaces/CMakeLists.txt:3`, `otsdaq-mu2e-stm/…/FEInterfaces/CMakeLists.txt:4`, `otsdaq-mu2e-sync/…/FEInterfaces/CMakeLists.txt:7` |
| `otsdaq-mu2e::ROCPolarFireCoreInterface` | PolarFire ROC implementation | `FEInterfaces/CMakeLists.txt:3` | `otsdaq-mu2e-calorimeter/…/FEInterfaces/CMakeLists.txt:18`, `otsdaq-mu2e-tracker/…/FEInterfaces/CMakeLists.txt:7`, `otsdaq-mu2e-sync/…/FEInterfaces/CMakeLists.txt:8` |
| `otsdaq-mu2e::ArtModules` | `HistoSender`, `HistoReceiver` | `ArtModules/CMakeLists.txt:1` | `otsdaq-mu2e-dqm/…/ArtModules/CMakeLists.txt:10` (and 8 more lines there), `otsdaq-mu2e-crv/…/ArtModules/CMakeLists.txt:17`, `otsdaq-mu2e-sync/…/ArtModules/CMakeLists.txt:6` |
| `otsdaq-mu2e::CFOandDTCCore` | `CFOandDTCCoreVInterface` | `CFOandDTCCore/CMakeLists.txt:2` | this package |
| `otsdaq-mu2e::DTCFrontEndInterface`, `::CFOFrontEndInterface` | FE implementations (also linked by `test/`) | `FEInterfaces/CMakeLists.txt:7`, `:22` | `test/CMakeLists.txt` |

The subsystem packages pin very different minimum versions:
`find_package(otsdaq-mu2e 1.02.00 REQUIRED)` in seven of them, and
`8.00.00 … EXPORT` in `otsdaq-mu2e-tracker/CMakeLists.txt:73`.

### 1.2 Executables and scripts

`test/CMakeLists.txt` builds five hand-run hardware drivers:
`DTCFrontEndInterface_TestMain`, `CFOFrontEndInterface_TestMain`,
`DTCFrontEndInterface_ExtCFOMain`, `DTCFrontEndInterface_HWEventBuilding` and
`DCS_test`. It also builds the wrapper `test_DCS.sh`. None of them is
registered as a test.

`tools/CMakeLists.txt:2-6` puts four snapshot scripts on `PATH`. The whole
`tools/` directory is copied and installed as a directory
(`CMakeLists.txt:87-88`). See
[`otsdaq-mu2e-tools(7)`](../man/man7/otsdaq-mu2e-tools.7).

---

## 2. Registered plugins, by type

These are all the registration macros in the package (`grep -rn
'DEFINE_OTS_\|DEFINE_ARTDAQ_\|DEFINE_ART_'`). There are **no** artdaq or art
plugins. `ArtModules/` contains plain libraries.

| Plugin class | otsdaq plugin type (CMake) | Registration | Base class | Selected by (configuration column) |
|---|---|---|---|---|
| `CFOFrontEndInterface` | `otsdaq::FEInterface` (`FEInterfaces/CMakeLists.txt:32`) | `DEFINE_OTS_INTERFACE` `FEInterfaces/CFOFrontEndInterface_interface.cc:9` | `CFOandDTCCoreVInterface` → `FEVInterface` | `FE_INTERFACE_PLUGIN_NAME` (FE interface table) |
| `DTCFrontEndInterface` | `otsdaq::FEInterface` (`:15`) | `DEFINE_OTS_INTERFACE` `FEInterfaces/DTCFrontEndInterface_interface.cc:9` | `CFOandDTCCoreVInterface` | `FE_INTERFACE_PLUGIN_NAME` |
| `CAPTANSignalGenerator` | `otsdaq::FEInterface` (`:34`) | `DEFINE_OTS_INTERFACE` `FEInterfaces/CAPTANSignalGenerator_interface.cc:911` | `FEVInterface`, `OtsUDPHardware`, `OtsUDPFirmwareDataGen` | `FE_INTERFACE_PLUGIN_NAME` |
| `ROCPolarFireCoreInterface` | `otsdaq::FEInterface` (`:4`) | `DEFINE_OTS_INTERFACE` `FEInterfaces/ROCPolarFireCoreInterface_interface.cc:6` | `ROCCoreVInterface` → `FEVInterface` | `ROC_INTERFACE_PLUGIN_NAME` (ROC interface table, under a DTC) |
| `ROCDTCHardwareEmulated` | `otsdaq::FEInterface` (`:5`) | `DEFINE_OTS_INTERFACE` `FEInterfaces/ROCDTCHardwareEmulated_interface.cc:152` | `ROCCoreVInterface` | `ROC_INTERFACE_PLUGIN_NAME` |
| `DTCInterfaceTable` | `otsdaq::table` (`TablePlugins/CMakeLists.txt:4`) | `DEFINE_OTS_TABLE` `TablePlugins/DTCInterfaceTable_table.cc:302` | `SlowControlsTableBase` | table name `DTCInterfaceTable` |
| `DBRunInfo` | `otsdaq::runInfo` (`RunInfoPlugins/CMakeLists.txt:6`) | `DEFINE_OTS_PROCESSOR` `RunInfoPlugins/DBRunInfo_runInfo.cc:1346` | `RunInfoVInterface` | `RUN_INFO_PLUGIN_TYPE` (state-machine table) |
| `DQMMu2eHistoConsumer` | `otsdaq::dataProcessor` (`DataProcessorPlugins/CMakeLists.txt:5`) | `DEFINE_OTS_PROCESSOR` `DataProcessorPlugins/DQMMu2eHistoConsumer_processor.cc:138` | `DQMHistosConsumerBase`, `Configurable` | `PROCESSOR_PLUGIN_NAME` (data-manager processor table) |

`DBRunInfo` is registered with `DEFINE_OTS_PROCESSOR`, not with a run-info
macro. It is built as an `otsdaq::runInfo` plugin, and the plugin type
supplies the library suffix that the loader searches. *(Whether otsdaq core
expects a dedicated run-info macro: otsdaq core, not verified here.)*
`doc/otsdaq_mu2e.org:80-85` (2023) shows it selected in a configuration
tree, so this registration has loaded in practice.

Two lines carry a duplicate registration that is commented out:
`FEInterfaces/CFOFrontEndInterfaceImpl.cc:6959` and
`FEInterfaces/DTCFrontEndInterfaceImpl.cc:10372`. They are not live.

### 2.1 How each plugin family is loaded

| Plugin family | Loader | Configuration path | Evidence |
|---|---|---|---|
| CFO/DTC/CAPTAN FE | `FESupervisor` *(otsdaq core)* | `FESupervisorTable` → `LinkToFEInterfaceTable` → record `FEInterfacePluginName` | column names in `TablePlugins/DTCInterfaceTable.h:34-39`; `otsdaq-mu2e-config` has 572 `"FE_INTERFACE_PLUGIN_NAME" : "DTCFrontEndInterface"` and 64 `"CFOFrontEndInterface"` rows; `tools/MacroMakerModeFhiclDump_edit.fcl:3` |
| ROC | `DTCFrontEndInterface::createROCs()` through `makeInterface(<ROCInterfacePluginName>, …)` | DTC record → `LinkToROCGroupTable` → `ROCInterfacePluginName` | `FEInterfaces/DTCFrontEndInterface.h:165,183`; `FEInterfaces/DTCFrontEndInterfaceImpl.cc:1176-1210`; `TablePlugins/DTCInterfaceTable.h:49,57` |
| Run info | `GatewaySupervisor` *(otsdaq core)* | `MainContext` → `LinkToApplicationTable` → GatewaySupervisor → `LinkToStateMachineTable` → `<state machine>` → `RunInfoPluginType : DBRunInfo` (the 2023 local-storage tables held `DEFAULT`) | `doc/otsdaq_mu2e.org:80-85`; column at `otsdaq-mu2e-config/Data_trigger/TableInfo/StateMachineTableInfo.xml:19` |
| Table | `ConfigurationManager` *(otsdaq core)* | table `DTCInterfaceTable` | `TablePlugins/DTCInterfaceTable_table.cc:17-19` |
| DQM processor | `DataManager` *(otsdaq core)* | processor record; reads `SaveFile`, `FilePath`, `RadixFileName` | `DataProcessorPlugins/DQMMu2eHistoConsumer_processor.cc:27-33` |

Configuration keys the CFO/DTC code reads from its own record include
`EmulatorMode`, `DeviceIndex` (PCIe index *N* of `/dev/mu2eN`),
`ConfigureClock`, `LocationInChain`, `EmulateCFO`, `LinkToROCGroupTable`
(`TablePlugins/DTCInterfaceTable.h:44-50`), `GlobalOperatingMode`,
`SkipCFOandDTCConfigureSteps` and `GlobalSkipCFOandDTCConfigureSteps`
(`CFOandDTCCore/CFOandDTCCoreVInterface.cc`, `getNode` calls). The ROC record
columns are `Status`, `ROCGroupID`, `ROCInterfacePluginName`, `linkID`,
`EventWindowDelayOffset`, `EmulateInDTCHardware`, `ROCTypeLinkTable` and
`LinkToSlowControlsChannelTable` (`TablePlugins/DTCInterfaceTable.h:53-64`).

### 2.2 Extension points for the subsystem packages

`ROCCoreVInterface` (`ROCCore/ROCCoreVInterface.h:11`) is the contract a
subsystem ROC plugin implements:

- **Pure virtual:** `writeEmulatorRegister` and `readEmulatorRegister`
  (`:71-77`).
- **Virtual with a "TODO" throw or a default body:** `writeROCRegister`,
  `readROCRegister`, `readROCBlock` and `writeROCBlock` (`:51-69`);
  `GetStatus` and `getFirmwareVersion` (`:94-108`); the firmware-inventory
  hooks (`:109-122`); `readInjectedPulseTimestamp`, `writeDelay`, `readDelay`,
  `readDTCLinkLossCounter` and `resetDTCLinkLossCounter` (`:124-150`);
  `isActionDone` (`:152`); the SPI-flash family (`:158-215`); `onDTCReady`
  (`:263`); `emulatorWorkLoop` (`:276`).

Any FE macro a ROC registers is exposed to operators **through the DTC**
(§3.4), not on its own. The ROC is not registered with the FE supervisor.

---

## 3. FE macros — the surface MacroMaker exposes

### 3.1 Who serves UDP :5000

`mu2edaq-CFOControl` sends UDP datagrams to the otsdaq-utilities
**MacroMaker** supervisor (`MacroMakerSupervisor.cc`,
`RemoteControlWorkLoop`). The listener starts only when both
`OTS_MACROMAKER_UDP_IP` and `OTS_MACROMAKER_UDP_PORT` are set in the
supervisor's environment. Port 5000 is a convention of the client, not a
value this package or `otsdaq-mu2e-config` sets (no file in `mu2edaq-main`
sets `OTS_MACROMAKER_UDP_PORT`). The MacroMaker runs each UDP request as
`NO-USER` with `allUsers: 255` and forwards it over SOAP to the FE supervisor
that owns the target front-end. Evidence: `mu2edaq-CFOControl/docs/INTERFACES.md`
§1, which reviewed `otsdaq-utilities` at `69f05f85`.

otsdaq-mu2e has **no** UDP or TCP listener of its own. A grep for socket and
listener types in `otsdaq-mu2e/` and `test/` finds none. What it contributes is
the set of macro names and argument names below, which the MacroMaker lists
and calls.

All 111 live `registerFEMacroFunction` calls in this package pass
`requiredUserPermissions = 1`, the lowest level. This includes the raw
register writes, the SPI-flash erase/program macros and the resets. The fifth
argument is `requiredUserPermissions`
(`otsdaq-utilities/otsdaq-utilities/MacroMaker/MacroMakerSupervisor.cc:2634`).
See OM-007.

Names are exact, including spaces and capitalisation. Arguments are passed by
the names shown. A blank input means "use the default named in the label".

### 3.2 Shared by the CFO and the DTC

These are registered by `CFOandDTCCoreVInterface::registerCFOandDTCFEMacros()`
(`CFOandDTCCore/CFOandDTCCoreVInterface.h:69`). `CFOFrontEndInterface` calls it
at `FEInterfaces/CFOFrontEndInterfaceImpl.cc:599`, and `DTCFrontEndInterface`
at `FEInterfaces/DTCFrontEndInterfaceImpl.cc:1075`.

| Macro name | Inputs | Outputs | Registered at |
|---|---|---|---|
| `CFO Soft Reset` (on a CFO) / `DTC Soft Reset` (on a DTC) | — | — | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:131` |
| `CFO Hard Reset` / `DTC Hard Reset` | — | — | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:147` |
| `Get Firmware Version` | — | `Firmware Version Date` | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:164` |
| `Flash LEDs` | — | `Status` | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:186` |
| `Get Status` | — | `Status` | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:196` |
| `Get Simple Status` | — | `Status` | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:206` |
| `Check Firefly Temperature` | — | `Temperature` | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:219` |
| `Check Firefly Loss-of-Light` | — | `Link Status` | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:229` |
| `Check FPGA Temperature` | — | `Temperature` | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:240` |
| `Jitter Attenuator Setup` | `Source Clock (0 is from CFO, 1 is from RJ45)`<br>`DoNotSet`<br>`AlsoResetJA` | `Register Write Results` | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:251` |
| `Get Device Index` | — | `Device Index` | `CFOandDTCCore/CFOandDTCCoreVInterface.cc:265` |

### 3.3 `CFOFrontEndInterface`

`CFOFrontEndInterface::registerFEMacros()` (`FEInterfaces/CFOFrontEndInterface.h:115`).

| Macro name | Inputs | Outputs | Registered at |
|---|---|---|---|
| `CFO Reset` | — | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:115` |
| `Clock Marker Enable/Disable` | `Enable Clock Markers (Default := true)` | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:126` |
| `CFO Halt` | — | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:137` |
| `CFO Write` | `address`<br>`writeData` | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:148` |
| `Loopback Test` | `Number of Loopback tests (Default := 1)`<br>`Target Link (-1 for all, Default := -1)`<br>`Target ROC (-1 for all, Default := -1)`<br>`Write ROOT file (Default := false)`<br>`ROOT file name (Default := CFO_loopback.root)` | `Response` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:159` |
| `CFO Read` | `address` | `readData` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:191` |
| `Reset Runplan` | — | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:204` |
| `Compile Runplan` | `Input Text File`<br>`Output Binary File` | `Result` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:215` |
| `Set Runplan` | `Binary Run File` | `Result` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:227` |
| `Compile, Set, and Launch On/Off Spill Template Run Plan` | `Enable CFO Run Plan Execution (Default := false)`<br>`Number of 1.4s super cycle repetitions (0 := infinite)`<br>`Starting Event Window Tag (Default or -1 := start from 0 and continue)`<br>`Enable Clock Markers (Default := true)`<br>`Use Detached Buffer Test (Default := false)`<br>`For Detached Buffer Test, Save Binary Data to File (Default: false)`<br>`For Detached Buffer Test, Save Subevent Header to Binary File (Default: false)`<br>`For Detached Buffer Test, Do NOT Reset Counters (Default: false)` | `response` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:240` |
| `Compile, Set, and Launch Fixed-width Event Window Template Run Plan` | `Enable CFO Run Plan Execution (Default := false)`<br>`Fixed-width Event Window Duration (s, ms, us, ns, and clocks allowed) [clocks := 25ns]`<br>`Number of Event Window Markers to generate (0 := infinite)`<br>`Starting Event Window Tag (Default or -1 := start from 0 and continue)`<br>`Event Window Mode (Default := 1)`<br>`Enable Clock Markers (Default := true)`<br>`Use Detached Buffer Test (Default := false)`<br>`For Detached Buffer Test, Save Binary Data to File (Default: false)`<br>`For Detached Buffer Test, Save Subevent Header to Binary File (Default: false)`<br>`For Detached Buffer Test, Do NOT Reset Counters (Default: false)` | `response` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:259` |
| `Launch Runplan` | — | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:281` |
| `Get CFO Counters` | — | `Counters` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:293` |
| `Shared Run Plan Get Status` | — | `Result` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:306` |
| `Shared Run Plan Start` | `Initial Event Mode (Default = 0)`<br>`Initial Event Tag  (Default = 0)`<br>`Run Plan Event Window Duration (s, ms, us, ns, and clocks allowed) [clocks := 25ns] (Default = 1.8 us)` | `Result` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:317` |
| `Shared Run Plan Stop` | — | `Result` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:343` |
| `Shared Run Plan Join` | `Subsystem Name (CRV, Calo, Tracker, STM, ExtMon, Default = Custom)`<br>`Custom Mode Bit Position (0-47, Default = 0)`<br>`Custom Mode Bit Count (1-48, Default = 48)`<br>`Custom Mode Bit Value (Default = 0)`<br>`Duty Cycle (% or M:N on:event ratio, Default = 100%)`<br>`Event Offset in Loop (Default = 0)` | `Result` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:356` |
| `Shared Run Plan Single-shot Join` | `Subsystem Name (CRV, Calo, Tracker, STM, ExtMon, Default = Custom)`<br>`Duty Cycle (% or M:N on:event ratio, Default = 100%)`<br>`Custom Mode Bit Position (0-47, Default = 0)`<br>`Custom Mode Bit Count (1-48, Default = 48)`<br>`Custom Mode Bit Value (Default = 0)`<br>`Single-shot Event Count (Any count allowed, executed in chunks of 1-N/M)` | `Result` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:403` |
| `Shared Run Plan Leave` | `Subsystem Name (CRV, Calo, Tracker, STM, ExtMon, Custom)`<br>`Custom Mode Bit Position (Default = 0)`<br>`Custom Mode Bit Count (Default = 48)` | `Result` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:448` |
| `Configure for Timing Chain` | `StepIndex` | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:465` |
| `Super Orchestration Start` | `Number of Event Window Markers (Default: 10)` | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:476` |
| `Super Orchestration End` | — | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:487` |
| `Super Orchestration` | `Do CRV ROC Reset`<br>`Do Calo ROC Reset`<br>`Do Calo ROC Writes` | `response` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:498` |
| `Buffer Test Detached` | `Command to 0/Status (to read counters, etc.), 1/Start, or 2/Halt (Default: Status)`<br>`Starting Event Window Tag (Default: 0)`<br>`Save Binary Data to File (Default: false)` | `Result` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:512` |
| `Runplan Subrun Config Setup` | `Subrun Event Limit (Default: 0)`<br>`Subrun Prediction Offset (Default: 0)` | — | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:542` |
| `Runplan Subrun Config Read` | — | `Subrun Event Limit`<br>`Subrun Prediction Offset` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:558` |
| `Loopback Topology Discovery` | `Number of Loopback tests per DTC (Default := 100)`<br>`Save ROC Delay Offsets to Table (Default := false)` | `Response` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:573` |
| `Temporary Diagnostic Test` | — | `Response` | `FEInterfaces/CFOFrontEndInterfaceImpl.cc:587` |

### 3.4 `DTCFrontEndInterface`

`DTCFrontEndInterface::registerFEMacros()` (`FEInterfaces/DTCFrontEndInterface.h:166`).
The last two rows are generated at configure time, one per macro of each ROC
the DTC created (§3.5):

- `ROC FEMacro - <name>` is used when all ROCs on the DTC are of one plugin
  type. Its inputs are `Target ROC or Mask (Default = -1 := all ROCs, or
  0x111111 := all)` followed by the ROC macro's inputs. Its outputs are
  `Target ROC` followed by the ROC macro's outputs.
- `Link<linkID>_<ROC UID>_<name>` is used when the ROCs are of mixed types. It
  takes the ROC macro's own arguments.

| Macro name | Inputs | Outputs | Registered at |
|---|---|---|---|
| `ROC Setup` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`Set Link RX/TX Enable (Default := false)`<br>`Set Link Timing Enable (Default := false)`<br>`Set ROC Emulation Enable (Default := false)`<br>`ROC Emulation Type (Default = 0: Internal, 1: Fiber-Loopback, 2: External)`<br>`ROC generated Data Payload fragment packet count (11-bits, Default := 16)`<br>`Block Null Heartbeats to ALL ROCs (Default := false)`<br>`Resequence Non-null Events for ALL ROCs (Default := false)`<br>`Set Auto-Gen DRP per ROC (Default := false)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:144` |
| `ROC Write` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`address`<br>`writeData` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:182` |
| `ROC Read` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`address` | `readData` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:195` |
| `ROC Firmware Inventory` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)` | `Status`<br>`InventoryJSON` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:207` |
| `List Firmware Directory` | `DirectoryPath` | `DirectoryJSON` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:220` |
| `ROC Block Read` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`address`<br>`Number Of 16-bit words to Read (Default := 8)`<br>`incrementAddress (Default := false)` | `readData` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:231` |
| `ROC Block Write` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`address`<br>`writeData (CSV-literal or CSV-filename of 16-bit words, or keyword + parameter 'AUTOGENERATE count')`<br>`incrementAddress (Default = false)`<br>`requestAck (Default = false)` | `Status` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:245` |
| `Headers Format test` | — | `setRegister` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:263` |
| `ROC_Write_ExtRegister` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`block`<br>`address`<br>`writeData` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:275` |
| `ROC_Read_ExtRegister` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`block`<br>`address` | `readData` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:287` |
| `Buffer Test` | `Data are SubEvents (Default: true)`<br>`Number of [Sub]Events (Default: 1)`<br>`Starting Event Window Tag (Default: 0)`<br>`Match Event Tags (Default: false)`<br>`Display Payload at GUI (Default: true)`<br>`Save Binary Data to File (Default: false)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:302` |
| `Pattern Test` | `Data are SubEvents (Default: true)`<br>`Number of [Sub]Events (Default: 1)`<br>`Starting Event Window Tag (Default: 0)`<br>`Match Event Tags (Default: false)`<br>`Display Payload at GUI (Default: true)`<br>`Save Binary Data to File (Default: false)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:327` |
| `DTC_HighRate_DCS_Check` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`loops`<br>`baseAddress`<br>`correctRegisterValue0`<br>`correctRegisterValue1` | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:352` |
| `DTC_HighRate_DCS_Block_Check` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`loops`<br>`baseAddress`<br>`correctRegisterValue0`<br>`correctRegisterValue1` | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:365` |
| `DTC_SendHeartbeatAndDataRequest` | `numberOfRequests`<br>`timestampStart`<br>`useSWCFOEmulator`<br>`rocMask` | `readData` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:378` |
| `DTC Instantiate` | — | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:387` |
| `Buffer Test Detached` | `Command to 0/Status (to read counters, etc.), 1/Start, or 2/Halt (Default: Status)`<br>`Data are SubEvents (Default: true)`<br>`Starting Event Window Tag (Default: 0)`<br>`Match Event Tags (Default: false)`<br>`Save Binary Data to File (Default: false)`<br>`Save Binary Data Filename`<br>`Save Subevent Header to Binary File (Default: false)`<br>`Payload Packet Threshold for Saving Event (Default: 0)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:398` |
| `DTC Write` | `address`<br>`writeData`<br>`Do validation (Default := true)` | `Status` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:428` |
| `DTC Read` | `address` | `readData` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:439` |
| `Event Mode Required Mask Set` | `Event Mode Required Mask (Default := 0)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:448` |
| `Event Mode Required Mask Read` | — | `Event Mode Required Mask` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:459` |
| `Loss-of-Lock Counter Read` | — | `Upstream Rx Lock Loss Count` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:469` |
| `Loss-of-Lock Counter Reset` | — | `Upstream Rx Lock Loss Count` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:481` |
| `Spy Buffer` | — | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:492` |
| `Release All Data DMA Buffers` | — | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:505` |
| `Get DTC Counters` | — | `Protocol Counters`<br>`Performance Counters` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:516` |
| `Get Link Lock Status` | — | `Lock Status` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:530` |
| `Configure DTC for HWDevmode` | — | `Setting the CFO emulated, DCS enabled and the retransmission off` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:539` |
| `Read Rx Diag FIFO` | `LinkIndex` | `Diagnostic RX FIFO` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:550` |
| `Read Tx Diag FIFO` | `LinkIndex` | `Diagnostic TX FIFO` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:563` |
| `Get Link Errors` | `` | `Link Errors` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:576` |
| `Get RTF Interface Status` | — | `RTF Interface Status` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:591` |
| `RTF Marker Offset Apply` | — | `RTF Marker Offset Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:603` |
| `Fix CFO Clock Edge` | — | `Fix CFO Clock Edge Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:615` |
| `EVB High Level Counters` | — | `EVB High Level Counters` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:627` |
| `Configure for Timing Chain` | `StepIndex` | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:644` |
| `Loopback CFO Emulator Test Run` | — | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:653` |
| `Loopback CFO Emulator Multi Test Run` | `numberOfTests`<br>`Write ROOT file (Default := false)`<br>`ROOT file name (Default := loopback.root)` | `Average`<br>`Maximum`<br>`Minimum` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:663` |
| `Loopback Manual Setup` | `setAsPassthrough (Default := false)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:676` |
| `Enable/Disable DTC Link` | `Target Link (Default = -1 := all links)`<br>`Set Link RX/TX Enable (Default := false)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:692` |
| `Reset ALL (CFO/ROC/EVB) DTC Links` | — | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:704` |
| `Reset CFO Link Rx` | — | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:716` |
| `Reset CFO Link Tx` | — | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:725` |
| `Reset CFO Link Rx PLL` | — | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:734` |
| `Reset CFO Link Tx PLL` | — | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:743` |
| `Get DTC ID and Hardware EVB Info` | — | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:786` |
| `Set DTC ID and Hardware EVB Info` | `DTC ID`<br>`EVB Mode`<br>`EVB Partition ID`<br>`EVB Self MAC Address Last Byte`<br>`EVB Dead Time in Cluster`<br>`EVB Number of DTCs in Cluster`<br>`EVB Cluster Base DTC MAC Address` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:796` |
| `EVB Init` | `EVB Number of DTCs in Cluster (Default := 1)`<br>`EVB Cluster Base DTC Address as hostname (e.g. calo-01, trk-04) (Default := auto)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:812` |
| `EVB Status` | — | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:826` |
| `CFO Interface (Emulation) Setup` | `Put DTC in CFO Emulation Mode (Default := false)`<br>`Also setup Jitter Attenuator (Default := false)`<br>`Set Link RX/TX Enable (Default := false)`<br>`Enable Auto-generation of Data Request Packets (Default := false)`<br>`Force External CFO Sample Clock Edge (0 for posedge, 1 for negedge, 2 for auto-find, Default := 0)`<br>`Permanent Offset (-2 to 2, Default := 0)`<br>`IDELAY Tap Value (0-31, Default := -1 do nothing)`<br>`RTF 40MHz Sample Edge (0 for negedge, 1 for posedge, Default := 0)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:840` |
| `CFO Emulator On/Off Spill Emulation Setup` | `Enable CFO Emulator (Default := true)`<br>`Number of 1.4s super cycle repetitions (0 := infinite)`<br>`Starting Event Window Tag (Default or -1 := start from 0 and continue)`<br>`Enable Auto-generation of Data Request Packets (Default := false)`<br>`Enable Clock Markers (Default := false)`<br>`Use Detached Buffer Test (Default := false)`<br>`For Detached Buffer Test, Save Binary Data to File (Default: false)`<br>`For Detached Buffer Test, Save Binary Data Filename`<br>`For Detached Buffer Test, Save Subevent Header to Binary File (Default: false)`<br>`For Detached Buffer Test, Do NOT Reset Counters (Default: false)`<br>`For Detached Buffer Test, Skip-by-32 to Emulate Event Building (Default: false)`<br>`For Detached Buffer Test, Payload Packet Threshold for Saving Event (Default: 0)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:861` |
| `CFO Emulator Fixed-width Event Window Emulation Setup` | `Enable CFO Emulator (Default := true)`<br>`Fixed-width Event Window Duration (s, ms, us, ns, and clocks allowed) [clocks := 25ns]`<br>`Number of Event Window Markers to generate (0 := infinite)`<br>`Starting Event Window Tag (Default or -1 := start from 0 and continue)`<br>`Event Window Mode (Default := 1)`<br>`Enable Auto-generation of Data Request Packets (Default := false)`<br>`Enable Clock Markers (Default := false)`<br>`Use Detached Buffer Test (Default := false)`<br>`For Detached Buffer Test, Save Binary Data to File (Default: false)`<br>`For Detached Buffer Test, Save Binary Data Filename`<br>`For Detached Buffer Test, Save Subevent Header to Binary File (Default: false)`<br>`For Detached Buffer Test, Do NOT Reset Counters (Default: false)`<br>`For Detached Buffer Test, Skip-by-32 to Emulate Event Building (Default: false)`<br>`For Detached Buffer Test, Payload Packet Threshold for Saving Event (Default: 0)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:888` |
| `DTC Software Data Request` | `Event Window Tag` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:918` |
| `DTC Punched Clock` | `Enable (Default := true)` | — | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:928` |
| `Program ROCs` | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)`<br>`Path to Directory map file (Default := do not use)`<br>`Write Directory map to SPI Flash (Default := false)`<br>`Verify Directory map (Default := false)`<br>`Image Index (Default := 0)`<br>`Path to Bitfile (Default := do not write bitfile, only program from Image Index)`<br>`Write Bitfile to SPI Flash (Default := false)`<br>`For Debug, force Write size (Default := do not force)`<br>`Verify with Bitfile Readback (Default := false)`<br>`Do program from Image Index (Default := false)` | `Result` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:938` |
| `Validate DTC Control Registers` | — | `Status` | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:963` |
| `Link<linkID>_<ROC UID>_<ROC macro>` (DTCs whose ROCs are of mixed plugin types) | the ROC macro's inputs | the ROC macro's outputs | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:1023` |
| `ROC FEMacro - <ROC macro>` (all ROCs of one plugin type) | `Target ROC or Mask (Default = -1 := all ROCs, or 0x111111 := all)` + the ROC macro's inputs | `Target ROC` + the ROC macro's outputs | `FEInterfaces/DTCFrontEndInterfaceImpl.cc:1056` |

### 3.5 ROC macros (exposed through the DTC, see §3.4)

`ROCCoreVInterface::registerFEMacros()` (`ROCCore/ROCCoreVInterface.h:218`).
Every ROC plugin that derives from `ROCCore` inherits these, including the
subsystem ROCs:

| Macro name | Inputs | Outputs | Registered at |
|---|---|---|---|
| `Get ROC Status` | — | `Result` | `ROCCore/ROCCoreVInterface.cc:62` |
| `Get ROC Firmware Version` | — | `Result` | `ROCCore/ROCCoreVInterface.cc:68` |

`ROCPolarFireCoreInterface` adds:

| Macro name | Inputs | Outputs | Registered at |
|---|---|---|---|
| `Setup for Pattern Data Taking` | — | — | `FEInterfaces/ROCPolarFireCoreInterfaceImpl.cc:20` |
| `Read SPI Flash Block` | `Start Address`<br>`Number of Bytes` | `Result` | `FEInterfaces/ROCPolarFireCoreInterfaceImpl.cc:27` |
| `Write SPI Flash Directory` | `Path to Directory Map file` | `Result` | `FEInterfaces/ROCPolarFireCoreInterfaceImpl.cc:35` |
| `Erase SPI Flash Block` | `Start Address`<br>`Number of Bytes` | — | `FEInterfaces/ROCPolarFireCoreInterfaceImpl.cc:43` |
| `Force Clear Action Lock` | — | — | `FEInterfaces/ROCPolarFireCoreInterfaceImpl.cc:51` |

`ROCDTCHardwareEmulated` registers none.

### 3.6 `CAPTANSignalGenerator`

| Macro name | Inputs | Outputs | Registered at |
|---|---|---|---|
| `Reset` | — | — | `FEInterfaces/CAPTANSignalGenerator_interface.cc:56` |
| `Get Firmware Version` | — | `Firmware Version Date` | `FEInterfaces/CAPTANSignalGenerator_interface.cc:64` |
| `Get Pulse Period` | — | `Pulse Period (us)`<br>`Low Pulse Width (Clock Cycle := 10 ns)` | `FEInterfaces/CAPTANSignalGenerator_interface.cc:74` |
| `Get Manual Mode` | — | `Manual Mode` | `FEInterfaces/CAPTANSignalGenerator_interface.cc:88` |
| `Set Manual Mode` | `Manual Mode` | — | `FEInterfaces/CAPTANSignalGenerator_interface.cc:100` |
| `Set Burst Mode` | `Burst Count` | — | `FEInterfaces/CAPTANSignalGenerator_interface.cc:112` |
| `Set Pulse Period` | `Trigger Period (s, ms, us, ns, and clocks allowed) [clocks := 10ns]` | — | `FEInterfaces/CAPTANSignalGenerator_interface.cc:124` |

### 3.7 Names that do *not* exist

- **`SetRegister`.** No macro of that name is registered anywhere in this
  package (`grep -rn SetRegister otsdaq-mu2e/` returns nothing). The
  docstring example `--cmd "SetRegister" --inputs "0x9100" "0x1"` at
  `mu2edaq-CFOControl/CFOControl.py:20` therefore fails with "Command not
  found". The register-write macros are `CFO Write` (`address`, `writeData`),
  `DTC Write` (`address`, `writeData`, `Do validation (Default := true)`) and
  `ROC Write`. That review has already staged this finding.
- **Commented out, so not registered:** `Reset PCIe`, `Reset Link Rx`,
  `Shutdown Link Tx`, `Startup Link Tx`, `Shutdown Firefly Tx` and
  `Startup Firefly Tx` (`CFOandDTCCore/CFOandDTCCoreVInterface.cc:176,276,284,293,303,311`),
  and `Test Loopback marker` (`FEInterfaces/CFOFrontEndInterfaceImpl.cc:180`).
  `mu2edaq-CFOControl/docs/INTERFACES.md` §5 lists all seven as registered.
  That list should be corrected from §3.2 and §3.3 here.

---

## 4. Run information — `DBRunInfo` → PostgreSQL

**Writer:** `ots::DBRunInfo` (`RunInfoPlugins/DBRunInfo.h:45`). It is selected
by `RunInfoPluginType = DBRunInfo` in the active state machine's record
(§2.1). It connects with `libpq` (`PQconnectdb`) when it is constructed
(`DBRunInfo_runInfo.cc:15-41`, `:52-66`).

### 4.1 Where

| Setting | Source | Default when unset | Evidence |
|---|---|---|---|
| database | `OTSDAQ_RUNINFO_DATABASE` | `run_info` | `DBRunInfo_runInfo.cc:20-22` |
| host | `OTSDAQ_RUNINFO_DATABASE_HOST` | empty | `:23-25` |
| port | `OTSDAQ_RUNINFO_DATABASE_PORT` | empty | `:26-28` |
| user | `OTSDAQ_RUNINFO_DATABASE_USER` | empty | `:29-31` |
| password | `OTSDAQ_RUNINFO_DATABASE_PWD` | empty | `:32-34` |
| schema (prefixes every table) | `OTSDAQ_RUNINFO_DATABASE_SCHEMA` | **`test`** | `:35-37` (OM-005) |
| detector setup name | `OTSDAQ_RUNINFO_DETECTOR_SETUP` | `default` | `:470-471` (used only by code that is commented out at present) |

Values shipped in `otsdaq-mu2e-config`:

- `Data_{trigger,dqm,cfo,shift}/db_setup_ots.sh:9-14` set database
  `run_info_dev`, host `mu2edaq14-ctrl`, port 5434, user `run_user` and schema
  `test`.
- The top-level `db_setup_ots.sh:29-33` sets `run_info` on `localhost`, schema
  `test`.
- The template here (`db_setup_ots.sh.template:7-12`) uses `FAKE`.

The same files export `OTSDAQ_COMPILE_RUNINFO` and
`OTSDAQ_RUNINFO_DATABASE_RUNTYPE`. No code or CMake in this package reads
either of them: the run type comes from `${OTS.ActiveStateMachine.runAlias}`
(`DBRunInfo_runInfo.cc:833-846`), and `RunInfoPlugins/` is always built.

### 4.2 What is written, per transition

The call sequence comes from the header comment at `DBRunInfo.h:22-44`, and
the SQL from the comment-stripped source.

| Transition (caller: GatewaySupervisor) | Method | SQL (all `<schema>.`-qualified) | Evidence |
|---|---|---|---|
| Configure | `insertConfigureCondition` | none; logs "Do nothing" and returns `(unsigned)-1` | `DBRunInfo.h:52-57` |
| Pre-start | `claimNextRunNumber` | `SELECT id FROM run_type WHERE name = <runAlias>`, then `INSERT INTO run (comment, run_type_id, create_time) … RETURNING run_number`. **The run number is assigned by the database sequence.** | `DBRunInfo_runInfo.cc:845-846`, `:902-913` |
| End of start | `insertRunCondition` | one row per subsystem: `INSERT INTO config (run_number, subsystem, settings, version, create_time) VALUES ($1,$2,$3::jsonb,'1',now)` (parameterised `PQexecParams`). `settings` is the subsystem's field map as JSON. The return value is the run number. | `:347-363`, `:389-390` |
| Stop / pause / resume / halt / error | `updateRunInfo` | `INSERT INTO run_transition (run_number, type_id, transition_time)` with type ids 0–7 (`:1011-1025`); on Stop (type 1) also `INSERT INTO run_end_info (run_number, comment, create_time)` with `PQescapeLiteral` | `:1077-1086`, `:1104-1124` |
| Read | `getRunRecords` | `SELECT … FROM view_run_summary WHERE run_status = 'completed' …` | `:1199-1207` |

The following are not live. `getRunConditionByID` and
`getRunConfigSubsystemInfo` throw "Not implemented" (`DBRunInfo.h:82-94`;
their `.cc` versions are inside `/* … */` at `:1240-1344`). The older
`INSERT INTO config(config_data, type_id, host_name, detector_setup_id)`,
`config_subsystem` and `config_subsystem_data` code is inside a comment block
(`:446-744`).

### 4.3 Readers

`mu2edaq-runlog-db` reads these tables. Its `docs/INTERFACES.md` §3.3 records
the same tables and lines, and its RL-003 and RL-007 cover the sequence and
schema consequences. Its `package.yaml` declares
`depends_on: otsdaq-mu2e via postgres-read`. Whether the gateway's `getRunInfo`
request, used by `mu2edaq-commandline-tools`
(`src/mu2edaq_commandline_tools/otsdaq/client.py:193`), reaches
`DBRunInfo::getRunRecords` is decided in *otsdaq core, not verified here*.

---

## 5. Slow controls (DCS / EPICS)

- `DTCFrontEndInterface` supplies slow-controls channels to the FE
  supervisor: `configureSlowControls`, `resetSlowControlsChannelIterator`,
  `getNextSlowControlsChannel` and `getSlowControlsChannelCount`
  (`FEInterfaces/DTCFrontEndInterface.h:31-34`). `CFOFrontEndInterface`
  overrides `configureSlowControls` only (`FEInterfaces/CFOFrontEndInterface.h:27`).
- `DTCInterfaceTable::slowControlsHandlerConfig` builds the EPICS channel
  list. `setFilePath()` names the PV file
  `$OTSDAQ_EPICS_DATA/<MU2E_OWNER>_otsdaq_dtc-ai.dbg`, falling back to
  `EPICS_CONFIG_PATH/otsdaq_dtc-ai.dbg` (`TablePlugins/DTCInterfaceTable_table.cc:10-14`,
  `:186`). The channel prefix is the FE supervisor's
  `SlowControlsMetricManagerChannelNamePreamble`, or `TDAQ_<MU2E_OWNER>` when
  that is empty or `DEFAULT` (`:75-81`). The direct
  `outputEpicsPVFile(configManager)` call in `init()` is commented out
  (`:48`). The file is written through `SlowControlsTableBase` *(otsdaq core)*.
- `doc/otsdaq_mu2e.org:73-77` documents the EPICS metric settings that give
  the `Mu2e:TDAQ_<user>` channel prefix.

---

## 6. DQM histogram transport

- `HistoSender(serverHost, serverPort)` serialises `TH1` and `TGraph` objects
  and sends them over otsdaq's `TCPSendClient` (`ArtModules/HistoSender.hh:14-22`).
  It is a TCP client. The host and port are chosen by the calling art module
  in the subsystem packages.
- On the receiving side, an otsdaq core data-manager producer (the config uses
  `TCPDataListenerProducer`) fills a buffer that `DQMMu2eHistoConsumer`
  drains. The consumer uses `HistoReceiver::readPacket` to rebuild the objects
  (`ArtModules/HistoReceiver.hh:20-24`) and can save them to
  `FilePath/RadixFileName…` (`DataProcessorPlugins/DQMMu2eHistoConsumer.h:43-47`).

---

## 7. Environment variables read

| Variable | Read by | Purpose |
|---|---|---|
| `OTSDAQ_RUNINFO_DATABASE{,_HOST,_PORT,_USER,_PWD,_SCHEMA}` | `DBRunInfo` | §4.1 |
| `OTSDAQ_RUNINFO_DETECTOR_SETUP` | `DBRunInfo` (code that is commented out at present) | §4.1 |
| `OTS_SCRATCH` | `DBRunInfo::insertRunCondition` | debug dumps under `$OTS_SCRATCH/Logs/` (OM-010) |
| `OTSDAQ_EPICS_DATA`, `MU2E_OWNER` | `DTCInterfaceTable` | PV file path and channel prefix (§5) |
| `OTSDAQ_DATA` | CFO/DTC FE (23 uses) | binary output of the detached buffer tests and other run data, e.g. `FEInterfaces/DTCFrontEndInterfaceImpl.cc:7577` |
| `ARTDAQ_PARTITION`, `HOSTNAME`, `OTS_SOURCE` | CFO/DTC FE | file naming and paths |

---

## 8. Files and devices touched at run time

- `/dev/mu2e<DeviceIndex>`: opened through `mu2edev` and `DTCLib::DTC` or
  `CFOLib::CFO` (`CFOandDTCCore/CFOandDTCCoreVInterface.h:9-13,56-57`).
- `$OTSDAQ_DATA/…`: buffer-test binary files.
- `$OTS_SCRATCH/Logs/runlog_dump.txt` and `failed_json_parse_<field>.txt`
  (OM-010).
- The EPICS PV file (§5).
- The tools (not the plugins) use `/tmp/mu2e.lock` (shared with
  `mu2edaq-operations/scripts/read_dtc_temps.sh:95`), `/sys/bus/pci/…`,
  `/sys/bus/usb/devices/…` and `/home/mu2ehwdev/.Xil/vivado_user_log.txt`.
  See the man pages.

---

## 9. Network endpoints other packages attribute to otsdaq-mu2e

| Endpoint | Actually served by | otsdaq-mu2e's part | Notes |
|---|---|---|---|
| UDP `:5000` used by `mu2edaq-CFOControl` | MacroMaker, otsdaq-utilities (§3.1) | FE macro names and arguments (§3) | Port set only by `OTS_MACROMAKER_UDP_PORT` in the supervisor's environment |
| Gateway HTTPS (`mu2edaq-commandline-tools`: gateway `:3075`, Slow Controls at gateway + 1, TFM `:3047`; LIDs gateway 200, console 260, config 281, slow controls 282, TFM 302, MacroMaker 800 at `src/mu2edaq_commandline_tools/otsdaq/client.py:22-27`) | otsdaq core supervisors in XDAQ contexts defined by `otsdaq-mu2e-config` | none directly. `getFEMacroList` returns §3; `getRunInfo` may reach `DBRunInfo::getRunRecords` (§4.3) | **The ports and LIDs vary by partition.** `otsdaq-mu2e-config/Data_trigger/XDAQConfigurations/ots.xml` puts the gateway (LID 200) in the `:3045` context (`:7-10`), the MacroMaker at **LID 300** (`:47`), and `ARTDAQSupervisor` at **LID 301** in the `:3047` context (`:63-66`). `doc/partitions.org` (2023) lists OTS ports 2015–5019 per partition. This is input for the Wave 2 port table. |
| Run-control UDP for `mu2edaq-shifter-tools/scripts/send-run-control-command.sh` | GatewaySupervisor's UDP state-change listener *(otsdaq core)*, configured by the `GatewaySupervisorTable` columns `EnableStateChangesOverUDP`, `IPAddressForStateChangesOverUDP`, `PortForStateChangesOverUDP` and `EnableAckForStateChangesOverUDP` (`otsdaq-mu2e-config/Data_trigger/TableInfo/GatewaySupervisorTableInfo.xml:10-13`) | none | That script targets `ots_port_offset + port_offset`, the HTTP gateway port (`mu2edaq-shifter-tools/src/mu2edaq_shifter_tools/read_config.py:41`). The UDP listener's port is a separate table column, so the computed target is not, in general, the listener's port. The payload is unimplemented (`send-run-control-command.sh:8`). |
| CAPTAN UDP | this package, as a UDP **client/host** socket bound to `HostIPAddress:HostPort` from the FE record, talking to `InterfaceIPAddress:InterfacePort` | `CAPTANSignalGenerator` (`FEInterfaces/CAPTANSignalGenerator_interface.cc:19-29`) | test-stand hardware |
| DQM TCP | otsdaq core producer | `HistoSender` client (§6) | |

---

## 10. Documentation and CI

- **Doxygen.** `doc/CMakeLists.txt` runs `create_doxygen_documentation(EXCLUDE
  UserWebGUI)` when `OTS_DOXY=DOIT` (`CMakeLists.txt:96-98`).
  `.github/workflows/nightly-ci-dashboard.yml` runs **hourly**
  (`cron: "0 * * * *"`, `:9`). It downloads the `doxygen` artifact of the
  latest successful `mu2e-develop-cpp-ci.yml` run, together with the lcov
  report from `Mu2e/.github`, and deploys to **this repository's** GitHub
  Pages:
  `https://mu2e.github.io/otsdaq-mu2e/doxygen/<pkg>/index.html`. That URL
  returned HTTP 200 for `otsdaq-mu2e`, `artdaq-core-mu2e`, `mu2e-pcie-utils`
  and `artdaq-mu2e` on 2026-10-06. The README's former
  `mu2e.github.io/daq-operations/doxygen/…` links returned 404
  (`Mu2e/daq-operations` is private). See OM-011.
- **Workflows (9).** Build, test, format, tidy and whitespace checks call
  reusable workflows in `Mu2e/.github@stable`. Two more add new issues and PRs
  to org project 6. They ignore `docs/**`, but this package's documentation is
  in `doc/` (OM-012).
- **`doc/`.** It holds twelve `.org` files, all last changed in 2023 (MRB/UPS
  build instructions, the 2023 partition table, teststand hosts
  `mu2edaq07/09/13`), and `event_building_mode_configure.md` (2026-08-17). The
  latter matches the 14-iteration phase constants in
  `CFOandDTCCore/CFOandDTCCoreVInterface.h:85-98` (OM-012).
