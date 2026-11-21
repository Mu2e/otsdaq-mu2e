# Mu2e tutorial development notes

Development notes, not for the published pages and not served (this file is outside `mu2e-tutorials/`, the folder
that is deployed). The published Mu2e tutorials state what was done and carry no notes, "not tested" remarks or
walkthrough remarks. Keep them here instead. Both tutorials are `draft` in `mu2e-tutorials/tutorials.json`.

## Adding a ROC DCS Register to EPICS (`adding-a-roc-register-to-slow-controls`)

Not covered (kept here, not in the page): creating a channel from scratch, alarms and thresholds, real hardware, the Running state. Written 2026-11-16 from a real run in the HWDev web GUI on mu2e-cfo-01, using a software-emulated DTC and ROC on calo-14. The front-end context calo_14_FEContext, the emulated caloDTC26, and ROC caloROC156 with a fake temperature register at address 0x3E8 (1000). Confirmed: Configure with tutorial_calo14_emulated alias, EPICS PV file generated with tutorialTemperature channel, Macro Maker reads show random walk. Dashboard screenshots could not be automated (cross-origin iframe between ports 3155 and 3159).

## Saving and Recalling a Mu2e Snapshot (`saving-and-recalling-a-mu2e-snapshot`)

written 2026-11-17 from reading the scripts in otsdaq-mu2e/tools (create_mu2e_ots_snapshot.sh, reset_mu2e_ots_snapshot.sh, get_mu2e_snapshot_data.sh, get_mu2e_snapshot_database.sh, create_mu2e_ops_snapshot.sh, reset_mu2e_ops_snapshot.sh). NOT RUN: the scripts overwrite USER_DATA and the databases, kill ots, and write to the shared snapshot area, so no step was executed and there are no screenshots. Not tested: every command, the --list output, the .bak folders, what the ots GUI shows after a recall, the ops variants. Status stays draft until someone runs them in a scratch ots area.

## Removed from the page bodies (2026-11-17)

- Snapshot tutorial, title slide: "Not yet run step by step."
- Snapshot tutorial, "Start ots and Check the Result": a sentence saying the GUI after a recall was not tested.
- Snapshot tutorial, "Things to Remember": "We did not run any command in this tutorial." (now plain advice to try it in a scratch area).
- ROC tutorial, "What We Did Not Cover": replaced by "Where to Go for More". Not covered: creating a channel from scratch, alarms and thresholds, real hardware, the Running state.

## Mu2e Subsystems in otsdaq (written 2026-11-17, draft)

Source: `setup_ots.sh` (per-subsystem block) and `Data_<subsystem>/ServiceData/ActiveTableGroups.cfg` of the ops install. Nothing was run in the GUI.

Still to add, after a look at a live Mu2e instance (needs the web login in `OTS_WEB_USER`/`OTS_WEB_PASSWORD`, not set in the agent shell):
- User permissions at Mu2e: which instances have security on, account levels in use, the `RequiredPermissionLevel` of the Mu2e icons. Link to the generic User Management tutorial.
- Desktop icon convention at Mu2e: folders, captions, images, `ots:` icons. Link to the generic Changing/Adding Icons tutorial.
- How the shift instance ties the other instances together (not claimed in the page).

Questions for the owner:
- What does the prefix `MC2` in the group names stand for? What do `HWDev` and `tem` stand for? (not expanded in the page)
- Is the `RequiredPermissionLevel` convention fixed (for example 10 for operators, 100 for experts)?

## How the Iterator Is Used in Mu2e (placeholder, 2026-11-17)

Two slides only. Needs: which Iterator plans Mu2e keeps, who runs them, on which instance.

## Multiple Subsystems in otsdaq (generic site, written 2026-11-17, draft)

Written from `otsdaq/ai_skills/ots-subsystems.md` and the Gateway code. The skill's 2026-11-11 run saw a top level and a `sunrock` subsystem, status Initial. Not run for this page: Configure and Start of a subsystem from the top level, the relaunch after the path got user and host, the subsystem folder on the top-level desktop, a second subsystem. No screenshots. Stays `draft` until those are run in the GUI.

## Update 2026-11-17 (evening): login at the top level, tunnels, port translation

Written from the Gateway code (`GatewaySupervisor.cc`: init(), translateURLForRequestOrigin, translateRemoteIconStringForRequestOrigin), `otsdaq/tools/ots` (printMainURL) and `otsdaq-mu2e-config/webPortTranslation.dat`, plus the owner's statement that the top-level Gateway is the one to log in at (the `shift` instance for Mu2e).

Note: the HTTPS port 8443 in the map is the LIVE shift Gateway (3075); do not use it for demos. The dev shift Gateway is mu2e-cfo-01:3175 (http).
Earlier blocked attempt at 8443: The headless Firefox refused the HTTPS page (SEC_ERROR_UNKNOWN_ISSUER); the certificate served on that port has the issuer DigiCert Global G2 TLS RSA SHA256 2020 CA1 and an end date of 2025-01-19, which is past. Accepting the bad certificate was refused by the permission system, so nothing was changed there. Still to do after that is cleared: create the tutorial user (password given by the owner, kept out of files) through the Settings window of the shift Gateway, show a normal user versus an administrator, and a group permission for one subsystem only, with screenshots (names cropped).
Open fact check: how the top-level Desktop passes the login on to a subsystem (the owner says it handles the redirects); the slides state only that.

## Update 2026-11-17 (night): live walk-through on the dev shift Gateway (mu2e-cfo-01:3175)

Done in the GUI as the owner's administrator account (login from the environment, never written to a file), then as the new `tutorial` account:
- Created the account `tutorial` (Settings, Accounts, Create New Account; permissions `allUsers: 10, Calo: 255`), first login with the new account code (needed the input check events that the GUI fires on key up), then normal logins. The password is the one the owner gave; it is not in any page.
- Read the DesktopIconTable and SubsystemUserDataPathsTable of the active Context group `MC2ShiftContext(118)` through the Configure app's own query functions (read only).
- Compared the Desktop of the administrator and of `tutorial`; opened the Calo Hardware folder as `tutorial` (no calo login).
- Released the "ots lock" that creating the account left with the administrator (Settings, Click to unlock). Account list now has 19 accounts.

Not in the page, unexplained: with the account set to `Calo: 1, allUsers: 10`, the list inside the Calo Hardware folder was the same as at `Calo: 255`. Either the calo icons all need level 1, or the list is cached at the top level. The page states only the code's example for how the level is passed on. To check: read the DesktopIconTable of the calo instance (3125).

Other findings:
- In the dev Desktop the Trk, CRV, Trigger, DQM and HW2 folders stay as `X loading...` (not running); Calo and CFO load.
- `STM_*` and `DCS_Dashboard` icons are Status Off.
- `TOP_OTS_PORT_OFFSET` is `-1` in the saved environment dumps (`.rte_*_setup_ots.sh`); the page does not explain its value.
- The certificate on the live HTTPS gateway port 8443 had an end date of 2025-01-19 when read with openssl (the live system, not touched further).
- Images in `images/mu2e-subsystems-in-otsdaq/` come from the dev Gateway; the administrator view is cropped below the top bar so no name shows. The window titles of Settings (which carry the login name) are cropped out.
- Scratch files (not in the repo): `/home/mu2eshift/ots_ops_tdaq/tmp/subsystems_tutorial/`.

## Running artdaq in Mu2e (`running-artdaq-in-mu2e`, written 2026-11-19, draft)

Source only: nothing was run and no Mu2e instance was opened. Every class, line and folder name was read with `git show` at `origin/develop` (`origin/main` for Offline) and compared with `raw.githubusercontent.com`: all 49 line-anchored links match GitHub. Reading the git refs mattered: the local `otsdaq-mu2e-dqm` checkout was 53 commits behind `origin/develop` (and `artdaq-mu2e` 16, `otsdaq-mu2e-crv` 27, `otsdaq-mu2e-sync` 14, `Offline` 1091), so the working trees show an older `TrackerDQM`, `CaloDigiDQM` and CMake build list. Diagrams: `doc/mu2e-tutorials-diagrams/make_running_artdaq_in_mu2e_diagrams.py` (edit it, not the png).

Not in the repositories, so not on the page: the real ARTDAQ table rows of the subsystems (process labels, hosts, ports) and the table definition of the settings of `DQMMu2eHistoConsumer` (columns `SaveFile`, `FilePath`, `RadixFileName` are read in the consumer source; the definition is probably in the private `otsdaq-mu2e-config`, unverified). The naming slides therefore state only the rules in code (a label is the UID of a row, the FHiCL file per label, fragment types, product names such as `Fragments_daq_DTCEVT_eb01`, the `Mu2e` naming helper, DTC and front-end names) and say that each subsystem chooses its labels.

Still to do, after a look at a live Mu2e DQM instance (`dqm`, `dqmcalo`, `dqmtrk` or `dqmstm`; the web login in `OTS_WEB_USER`/`OTS_WEB_PASSWORD`):
- The real labels and ports of the DQM subsystems, and which analyzers run in each. The page lists the analyzers in the repository, not the ones that run.
- What the live view lists at `/`: the page says the folders and histograms of `Mu2eHistos`. The Visualizer code had a fixed `Mu2eHistos` prefix (commit 83908747, 2026-09-23); the rule that skips a single top folder was written 2026-11-18 and is uncommitted in otsdaq-utilities.
- What a `HistoSender` does when the listener is not up at **Start** (`TCPSendClient` was not read; the page says only that it connects when the analyzer is created).
- Which branch the ops installs use: `mu2e/ots_ops` exists in `otsdaq-mu2e`, `otsdaq-mu2e-dqm`, `artdaq-mu2e`, `artdaq-core-mu2e` and `otsdaq-mu2e-calorimeter`. The links use `develop`.
- `CrvOtsDqm` (cosmic ray veto, sends `hists/...:replace`) is in an older `otsdaq-mu2e-crv` checkout and is not built there; the page does not list it. Check `origin/develop` of that repository before adding it.
- Slide "Mu2e C++ Classes: Art Modules Around the Data": whether the DQM analyzers run the Offline producers in the same job, or read the fragments themselves, differs by analyzer; the page says "or".

## DCS Dashboard for Mu2e (`dcs-dashboard-for-mu2e`, placeholder written 2026-11-19, draft)

Placeholder at the owner's request, with a Coming Soon slide only. Planned content: the full Calorimeter dashboard, meaning its set of pages with their widgets, and the bitmap arrangement (a map of the crystals, with regions highlighted). The picture on the Coming Soon slide is the owner's example of the crystal map; nothing was run and what the green regions mean is not stated on the page. To fill it in: open the Calorimeter dashboard in the Slow Controls Dashboard, list its pages and widgets, and find out how the bitmap arrangement is configured (which widget and which channels). Ask the owner which other subsystems have a full dashboard, and whether the page should go before or after "Adding a ROC DCS Register to EPICS".

