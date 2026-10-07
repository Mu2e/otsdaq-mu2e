# otsdaq-mu2e

The core otsdaq plugin package of the Mu2e TDAQ Suite.

The generic otsdaq supervisors load the Mu2e-specific code from this package at
run time:

- **Front-end interfaces** for the CFO timing master (`CFOFrontEndInterface`)
  and the DTCs (`DTCFrontEndInterface`). They provide 111 FE macros that are
  callable from the otsdaq web GUI and through the MacroMaker.
- **ROC interfaces** (`ROCCoreVInterface`, `ROCPolarFireCoreInterface`,
  `ROCDTCHardwareEmulated`). The DTC creates them for its links, and every
  subsystem ROC plugin derives from them.
- **`DBRunInfo`**, the run-info plugin. It takes run numbers from PostgreSQL
  and records run conditions and transitions there.
- **`DTCInterfaceTable`**, the slow-controls table plugin that generates the
  EPICS PV list.
- **`DQMMu2eHistoConsumer`** and the `HistoSender`/`HistoReceiver` ROOT-object
  transport for the online DQM.
- **`tools/`**: the FPGA-programming, flash and PCIe-reset scripts used in the
  shifter firmware procedure, together with installation and
  configuration-snapshot scripts.

The eight `otsdaq-mu2e-<subsystem>` packages build on this one.

## Documentation

| What | Where |
|---|---|
| What the package exposes and what loads it: plugins by type, every FE macro with its arguments, the run-info database tables, environment variables and network endpoints | [`doc/INTERFACES.md`](doc/INTERFACES.md) |
| Components and how they fit together | [`doc/ARCHITECTURE.md`](doc/ARCHITECTURE.md) |
| Firmware and PCIe tools | `man/man1/program_all_FPGA_AL9.sh.1`, `reset_PCIe_AL9.sh.1`, `program_flash_all_FPGA_AL9.sh.1`, `boot_from_flash_all_DTCs_AL9.sh.1`, `check_firmware_version.sh.1`, `check_status.sh.1` |
| Index of all 52 files in `tools/` | `man/man7/otsdaq-mu2e-tools.7` |
| EventBuildingMode configure phases | [`doc/event_building_mode_configure.md`](doc/event_building_mode_configure.md) |
| Older notes from 2023 (MRB/UPS era; partly stale) | [`doc/otsdaq_mu2e.org`](doc/otsdaq_mu2e.org) and the other `.org` files |
| Review findings not yet filed | [`doc/ISSUES-PENDING.md`](doc/ISSUES-PENDING.md) |

To read a man page from a checkout:

```bash
man -l man/man1/reset_PCIe_AL9.sh.1        # Linux (man-db)
man ./man/man1/reset_PCIe_AL9.sh.1         # macOS
```

Read `reset_PCIe_AL9.sh(1)` before you use its `NOKILL` flag. If any process
holds `/dev/mu2e*`, `NOKILL` makes the reset loop forever.

## Building

The package is a cetmodules (>= 3.19.02) CMake project
(`project(otsdaq-mu2e VERSION 11.03.00)`). It is normally built inside a
Spack development area of the Mu2e TDAQ Suite, together with its dependencies:

| Dependency | Minimum version (`CMakeLists.txt`) |
|---|---|
| otsdaq | 2.06.08 |
| otsdaq-components | 2.06.08 |
| artdaq-mu2e, which brings in artdaq-core-mu2e and mu2e-pcie-utils | 1.05.02 |
| PostgreSQL (libpq headers) | 13.2 |
| nlohmann_json, ROOT | any found |

To create a development area, run `tools/mu2e-quick-spack-start.sh` (a link to
`mu2e-quick-spack-start_v0.28.sh`) in an empty directory. Its options are
listed by `-h`:

```bash
mkdir ~/ots_spack && cd ~/ots_spack
/path/to/otsdaq-mu2e/tools/mu2e-quick-spack-start.sh -h
/path/to/otsdaq-mu2e/tools/mu2e-quick-spack-start.sh --develop --all-packages
```

These environment variables change the build:

| Variable | Effect | Source |
|---|---|---|
| `OTS_DOXY=DOIT` | also build the Doxygen documentation (`doc/CMakeLists.txt`) | `CMakeLists.txt:96` |
| `USE_ASAN`, `USE_TSAN`, `USE_UBSAN` (> 0) | add the sanitizer, Debug builds only | `CMakeLists.txt:29-50` |
| `USE_GCOV` | coverage flags, Debug builds only | `CMakeLists.txt:51-54` |
| `USE_EXCEPTIONSTACKTRACE` (> 0) | link the artdaq-core exception stack trace | `CMakeLists.txt:58-66` |

The build uses `-Werror` with cetmodules' `VIGILANT` diagnostics.

The run-info plugin reads its database settings from
`OTSDAQ_RUNINFO_DATABASE{,_HOST,_PORT,_USER,_PWD,_SCHEMA}` at run time.
`db_setup_ots.sh.template` lists them. The real values are kept outside git
(`~/.secrets/db_setup_ots.sh`, see `tools/install_ots_ops.sh`). If the schema
variable is unset, the plugin uses `test`; see `doc/INTERFACES.md` §4.

## Doxygen API documentation

The hourly `nightly-ci-dashboard` workflow publishes the CI dashboard and
Doxygen to this repository's GitHub Pages site, https://mu2e.github.io/otsdaq-mu2e/.
These pages answered on 2026-10-06:

* [otsdaq-mu2e](https://mu2e.github.io/otsdaq-mu2e/doxygen/otsdaq-mu2e/index.html)
* [artdaq-core-mu2e](https://mu2e.github.io/otsdaq-mu2e/doxygen/artdaq-core-mu2e/index.html)
* [mu2e-pcie-utils](https://mu2e.github.io/otsdaq-mu2e/doxygen/mu2e-pcie-utils/index.html)
* [artdaq-mu2e](https://mu2e.github.io/otsdaq-mu2e/doxygen/artdaq-mu2e/index.html)

The suite-wide links below are carried over from the shared README template.
On 2026-10-06 every one of them returned HTTP 404 to an unauthenticated
request. `Mu2e/daq-operations` is a private repository, so they may need a
GitHub login, or they may no longer be published.

### Common DAQ Repositories

* [artdaq-core-mu2e](https://mu2e.github.io/otsdaq-mu2e/doxygen/artdaq-core-mu2e): _artdaq_ Fragment data formats
* [mu2e-pcie-utils](https://mu2e.github.io/otsdaq-mu2e/doxygen/mu2e-pcie-utils): PCIe Device Driver and interface library
* [artdaq-mu2e](https://mu2e.github.io/otsdaq-mu2e/doxygen/artdaq-mu2e): Core data acquisition code
* [otsdaq-mu2e](https://mu2e.github.io/otsdaq-mu2e/doxygen/otsdaq-mu2e): Slow controls and Front-End Interface
* [otsdaq-mu2e-dqm](https://mu2e.github.io/otsdaq-mu2e/doxygen/otsdaq-mu2e-dqm): DQM interface and tools
* [mu2e-trig-config](https://mu2e.github.io/otsdaq-mu2e/doxygen/mu2e-trig-config): Trigger path FHiCL generation utilities

### Subsystem-Specific Repositories

* [otsdaq-mu2e-calorimeter](https://mu2e.github.io/otsdaq-mu2e/doxygen/otsdaq-mu2e-calorimeter)
* [otsdaq-mu2e-crv](https://mu2e.github.io/otsdaq-mu2e/doxygen/otsdaq-mu2e-crv)
* [otsdaq-mu2e-extmon](https://mu2e.github.io/otsdaq-mu2e/doxygen/otsdaq-mu2e-extmon)
* [otsdaq-mu2e-stm](https://mu2e.github.io/otsdaq-mu2e/doxygen/otsdaq-mu2e-stm)
* [otsdaq-mu2e-tracker](https://mu2e.github.io/otsdaq-mu2e/doxygen/otsdaq-mu2e-tracker)
* [otsdaq-mu2e-trigger](https://mu2e.github.io/otsdaq-mu2e/doxygen/otsdaq-mu2e-trigger)

## License

Fermilab BSD license; see [`LICENSE`](LICENSE).
