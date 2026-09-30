#include <algorithm>
#include <cmath>
#include <format>
#include "otsdaq-mu2e/FEInterfaces/CFOFrontEndInterface.h"
#include "otsdaq/ConfigurationInterface/ConfigurationManagerRW.h"
#include "otsdaq/Macros/InterfacePluginMacros.h"
//#include "otsdaq/DAQHardware/FrontEndHardwareTemplate.h"
//#include "otsdaq/DAQHardware/FrontEndFirmwareTemplate.h"

//#include "mu2e_driver/mu2e_mmap_ioctl.h"	// m_ioc_cmd_t

// ROOT includes
#include "TFile.h"
#include "TGraph.h"
#include "TTree.h"

using namespace ots;

#undef __MF_SUBJECT__
#define __MF_SUBJECT__ "CFOFrontEndInterface"

//macros for generating Run Plan files from parameters
#define OUT out << tabStr << commentStr
#define PUSHTAB tabStr += "\t"
#define POPTAB tabStr.resize(tabStr.size() - 1)
#define PUSHCOMMENT commentStr += "// "
#define POPCOMMENT commentStr.resize(commentStr.size() - 3)

//===========================================================================================
CFOFrontEndInterface::CFOFrontEndInterface(
    const std::string&       interfaceUID,
    const ConfigurationTree& theXDAQContextConfigTree,
    const std::string&       interfaceConfigurationPath)
    : CFOandDTCCoreVInterface(
          interfaceUID, theXDAQContextConfigTree, interfaceConfigurationPath)
{
	__FE_COUT__ << "instantiate CFO... " << getInterfaceUID() << " "
	            << theXDAQContextConfigTree << " " << interfaceConfigurationPath << __E__;

	std::string expectedDesignVersion = "";
	try
	{
		expectedDesignVersion =
		    getSelfNode().getNode("ExpectedFirmwareVersion").getValueWithDefault("");
	}
	catch(const std::runtime_error& e)
	{
		//ignoring missing field, so not enforcing firmware version
	}

	auto mode = DTCLib::DTC_SimMode_NoCFO;

	__COUT__ << "CFO arguments..." << std::endl;
	__COUTV__(mode);
	__COUTV__(deviceIndex_);
	__COUTV__(expectedDesignVersion);
	__COUT__ << "END CFO arguments..." << std::endl;
	//Note: if we do not skip init, then the CFO::SetSimMode writes registers!
	thisCFO_ = new CFOLib::CFO(
	    mode, deviceIndex_, expectedDesignVersion, true /*skipInit*/, getInterfaceUID());

	registerFEMacros();

	try  //attempt to print out firmware version to the log
	{
		std::string designVersion = thisCFO_->ReadDesignVersion();
		__FE_COUTV__(designVersion);
	}
	catch(
	    ...)  //hide exception to finish instantiation (likely exception is from a need to reset PCIe)
	{
		__FE_COUT_WARN__
		    << "Failed to read the firmware version, likely a PCIe reset is needed!"
		    << __E__;
	}

	__FE_COUTV__(StringMacros::systemVariables_["ActiveStateMachine"]["name"]);
	__FE_COUTV__(StringMacros::systemVariables_["ActiveStateMachine"]["runAlias"]);
	if(0)  //keep as System Variable example
	{
		try
		{
			//test an extra field (e.g. to use System Vars in tree, add value '${OTS.ActiveStateMachine.name}')
			std::string test = getSelfNode().getNode("DefaultColumnName").getValue();
			__FE_COUT__ << getSelfNode().getNode("DefaultColumnName").getValueAsString()
			            << " ==> " << test << __E__;
		}
		catch(const std::runtime_error& e)
		{
			__FE_COUTV__(e.what());
		}  //ignore
	}

	__FE_COUT_INFO__ << "CFO instantiated with name: " << getInterfaceUID()
	                 << " talking to /dev/mu2e" << deviceIndex_ << __E__;
	__FE_COUT__ << "Linux Kernel Driver Version: "
	            << thisCFO_->GetDevice()->get_driver_version() << __E__;
}  // end constructor()

//===========================================================================================
CFOFrontEndInterface::~CFOFrontEndInterface(void)
{
	delete thisCFO_;
	__FE_COUT__ << "Destructed." << __E__;
}  // end destructor()

//==============================================================================
void CFOFrontEndInterface::registerFEMacros(void)
{
	__FE_COUT__ << "Registering CFO FE Macros... (inherited macro count = "
	            << mapOfFEMacroFunctions_.size() << ")" << __E__;

	// clang-format off

	registerFEMacroFunction(
		"CFO Reset",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::CFOReset),
					std::vector<std::string>{}, // namesOfInputArgs
					std::vector<std::string>{}, // namesOfOutput
					1,  // requiredUserPermissions
					"*",  // allowedCallingFEs
					"Executes a soft reset of the CFO by setting the reset bit (31) to true on the <b>CFO Control Register</b> (0x9100)."
	);

	registerFEMacroFunction(
		"Clock Marker Enable/Disable",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::EnableOrDisableClockMarkers),
					std::vector<std::string>{"Enable Clock Markers (Default := true)"}, // namesOfInputArgs
					std::vector<std::string>{}, // namesOfOutput
					1,  // requiredUserPermissions
					"*",  // allowedCallingFEs
					"Enable or Disable the Mu2e Clock Marker broadcast over the CFO timing links."
	);

	registerFEMacroFunction(
		"CFO Halt",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::CFOHalt),
					std::vector<std::string>{}, // namesOfInputArgs
					std::vector<std::string>{}, // namesOfOutput
					1,  // requiredUserPermissions
					"*",
					"Transitions the state machine to <b>Halt</b> by setting the Enable Beam Off Mode Register to off."
	);

	registerFEMacroFunction(
		"CFO Write",  // feMacroName
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::WriteCFO),  // feMacroFunction
					std::vector<std::string>{"address", "writeData"}, // namesOfInputArgs
					std::vector<std::string>{},  // namesOfOutput
					1,    // requiredUserPermissions
					"*",  // allowedCallingFEs
					"This FE Macro writes to the CFO registers."
	);

	registerFEMacroFunction(
		"Loopback Test",  // feMacroName
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::LoopbackTest),  // feMacroFunction
					std::vector<std::string>{ // namesOfInputArgs
						// "Number of Loopback Exponent (Default := 3, which is 8 Loopback Markers sent)", // as of June 2026 -- defaulting to 0 exponent always (first loopback is somehow different than 2+ loopbacks)
						"Number of Loopback tests (Default := 1)",
						"Target Link (-1 for all, Default := -1)",
						"Target ROC (-1 for all, Default := -1)",
						"Write ROOT file (Default := false)", "ROOT file name (Default := CFO_loopback.root)"},
					std::vector<std::string>{"Response"},  // namesOfOutput
					1,
					"*",
					"Similar to <b>Test Loopback marker</b>, this FE Macro repeatedly measures the delay of markers from ROCs for a specified link. "
					"The average delay is returned given the number of iterations (loopbacks), link, and delay (sleep between iterations). "
					"This FE Macro is useful for Event Window synchronization.\n\n"
					"Constraints:\n"
					"\t-Loopback must be less than 10,000.\n"
	);

	// registerFEMacroFunction(
	// 	"Test Loopback marker",  // feMacroName
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&CFOFrontEndInterface::TestMarker),  // feMacroFunction
	// 				std::vector<std::string>{"DTC-chain link index (0-7)"},
	// 				std::vector<std::string>{"Response"},  // namesOfOutput
	// 				1,
	// 				"*",
	// 				"This FE Macro measures the delay of a marker from ROCs. "
	// 				"Optionally, the delay can be measured over mutliple iterations with the <b>Loopback Test</B> Macro."
	// );

	registerFEMacroFunction(
		"CFO Read",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::ReadCFO),                  // feMacroFunction
					std::vector<std::string>{"address"},  // namesOfInputArgs
					std::vector<std::string>{"readData"},
					1,  // requiredUserPermissions
					"*",
					"Read from the CFO Memory Map.\n\n"
					"Parameters:\n"
					"\taddress (uint16_t): Address in Memory Map.\n"
	);

	registerFEMacroFunction(
		"Reset Runplan",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::ResetRunplan),                  // feMacroFunction
					std::vector<std::string>{}, // namesOfInputArgs
					std::vector<std::string>{}, // namesOfOutput
					1,   // requiredUserPermissions
					"*",
					"Resets the Event Building run plan by setting the reset bit (27) to true on the <b>CFO Control Register</b>."
	);

	registerFEMacroFunction(
		"Compile Runplan",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::CompileRunplan),                  // feMacroFunction
					std::vector<std::string>{"Input Text File", "Output Binary File"},//"Input Text Run Plan", "Output Binary Run File"},  // namesOfInputArgs
					std::vector<std::string>{"Result"},
					1,    // requiredUserPermissions
					"*" /* allowedCallingFEs */,
					"This FE Macro compiles the CFO run plan to a binary file. You must compile before running <b>Set Runplan</b> "
					"which downloads the binary run plan to the CFO.\n\nDefault text run plan: srcs/mu2e-pcie-utils/cfoInterfaceLib/Command.txt\nDefault binary run plan: srcs/mu2e-pcie-utils/cfoInterfaceLib/Command.bin" /* feMacroTooltip */
					);

	registerFEMacroFunction(
		"Set Runplan",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::SetRunplan),                  // feMacroFunction
					std::vector<std::string>{"Binary Run File"},         // namesOfInputArgs
					std::vector<std::string>{"Result"},
					1,   // requiredUserPermissions
					"*", /* allowedCallingFEs */
					"Download the binary run plan to the CFO. <b>You must first compile your run plan</b>.\n\n\n\n" /* feMacroTooltip */
					"Paramters:\n"
					"\tBinary Run File (string): Path to the binary run plan. Default: srcs/mu2e-pcie-utils/cfoInterfaceLib/Commands.bin\n"
	);

	registerFEMacroFunction(
		"Compile, Set, and Launch On/Off Spill Template Run Plan",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::CompileSetAndLaunchTemplateSuperCycleRunPlan),                  // feMacroFunction
					std::vector<std::string>{"Enable CFO Run Plan Execution (Default := false)",
											"Number of 1.4s super cycle repetitions (0 := infinite)",
											"Starting Event Window Tag (Default or -1 := start from 0 and continue)",
											"Enable Clock Markers (Default := true)",
											"Use Detached Buffer Test (Default := false)",
											"For Detached Buffer Test, Save Binary Data to File (Default: false)",
											"For Detached Buffer Test, Save Subevent Header to Binary File (Default: false)",
											"For Detached Buffer Test, Do NOT Reset Counters (Default: false)"
											},  // namesOfInputArgs
					std::vector<std::string>{"response"},
					1,   // requiredUserPermissions
					"*",
					"Compile & Set a Template CFO Run Plan. Disabling turns off output of CFO Event Window Markers, timing markers, and Heartbeat Packets. " /* feMacroTooltip */
					"Enabling turns on emulated Event Window generation and timing markers based on the CFO parameters."
	);
	registerFEMacroFunction(
		"Compile, Set, and Launch Fixed-width Event Window Template Run Plan",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::CompileSetAndLaunchTemplateFixedWidthRunPlan),                  // feMacroFunction
					std::vector<std::string>{"Enable CFO Run Plan Execution (Default := false)",
											"Fixed-width Event Window Duration (s, ms, us, ns, and clocks allowed) [clocks := 25ns]",
											"Number of Event Window Markers to generate (0 := infinite)",
											"Starting Event Window Tag (Default or -1 := start from 0 and continue)",
											"Event Window Mode (Default := 1)",
											"On-spill Flag (Default := false)",
											"Enable Clock Markers (Default := true)",
											"Use Detached Buffer Test (Default := false)",
											"For Detached Buffer Test, Save Binary Data to File (Default: false)",
											"For Detached Buffer Test, Save Subevent Header to Binary File (Default: false)",
											"For Detached Buffer Test, Do NOT Reset Counters (Default: false)"
											},  // namesOfInputArgs
					std::vector<std::string>{"response"}, // namesOfOutputArgs
					1,   // requiredUserPermissions
					"*",
					"Compile & Set a Template CFO Run Plan. Disabling turns off output of CFO Event Window Markers, timing markers, and Heartbeat Packets. " /* feMacroTooltip */
					"Enabling turns on emulated Event Window generation and timing markers based on the CFO parameters."
	);

	registerFEMacroFunction(
		"Launch Runplan",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::LaunchRunplan),                  // feMacroFunction
					std::vector<std::string>{},  // namesOfInputArgs
					std::vector<std::string>{}, // namesOfOutputArgs
					1,   // requiredUserPermissions
					"*" /* allowedCallingFEs */,
					"Launches the Event Building run plan. You must <b>Compile Runplan</b> and <b>Set Runplan</b> before launching. " /* feMacroTooltip */
					"You do not need to compile and set the same runplan more than once. Use <b>Reset Runplan</b> and <b>Launch Runplan</b> thereafter."
	);

	registerFEMacroFunction(
		"Get CFO Counters",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::GetCFOCounters),              // feMacroFunction
					std::vector<std::string>{},  // namesOfInputArgs
					std::vector<std::string>{"Counters"}, // namesOfOutputArgs
					1,   // requiredUserPermissions
					"*" /* allowedCallingFEs */,
					"Reads and displays important CFO counters."
	);

	// Shared Run Info FE Macro Registration ------------------
	{
		registerFEMacroFunction(
			"Shared Run Plan Get Status",
				static_cast<FEVInterface::frontEndMacroFunction_t>(
						&CFOFrontEndInterface::SharedRunPlanStatus),              	// feMacroFunction
						std::vector<std::string>{},  // namesOfInputArgs
						std::vector<std::string>{"Result"}, // namesOfOutputArgs
						1,
						"*",
						"This FE Macro returns the status of the CFO Run Plan. It retrieves the current Event Mode, Event Window Tag, Active Subsystems, and running status."
		);  // requiredUserPermissions

		registerFEMacroFunction(
			"Shared Run Plan Start",
				static_cast<FEVInterface::frontEndMacroFunction_t>(
						&CFOFrontEndInterface::SharedRunPlanStart),                  // feMacroFunction
						std::vector<std::string>{
							"Initial Event Mode (Default = 0)",
							"Initial Event Tag  (Default = 0)",
							"Run Plan Event Window Duration (s, ms, us, ns, and clocks allowed) [clocks := 25ns] (Default = 1.8 us)"},
						// namesOfInputArgs
						std::vector<std::string>{"Result"}, // namesOfOutputArgs
						1,
						"*",
						"This FE Macro starts the shared CFO Run Plan, with a specified Event Mode, "
						"initial Event Window Tag, and Fixed-width Window Duration or Super-cycle Emulation "
						"Event Window Duration.<br><br>"
						"Note on Event Window Duration: Remember this is a <b>Shared</b> Run Plan, so choose an "
						"Event Window Duration that works for all currentyl active subsystems. "
						"For example, if you are testing with the CRV and you want to emulate super cycles while the CRV "
						"takes 100us windows at 50% Duty Cycle, then choose 1.8us because this is the common denominator "
						"(i.e. both run type needs can be assembled from 1.8 us Event Window building blocks)."
						"<br><br>"
						"Example continued: The next step after starting with the common building block of 1.8 us windows, "
						"would be for you to select <b>Shared Run Plan Join</b> and specify your Subsystem and that you want Supercycle Emulation, "
						"while the CRV team selects <b>Shared Run Plan Join</b> and specifies their Subsystem and that they want 100 us windows at 50% duty cycle. "
		);  // requiredUserPermissions

		registerFEMacroFunction(
			"Shared Run Plan Stop",
				static_cast<FEVInterface::frontEndMacroFunction_t>(
						&CFOFrontEndInterface::SharedRunPlanStop),              	// feMacroFunction
						std::vector<std::string>{},  // namesOfInputArgs
						std::vector<std::string>{"Result"}, // namesOfOutputArgs
						1,
						"*",
						"This FE Macro stops the Shared CFO Run Plan. Note this stops the Shared Run Plan for everyone! "
						"Be sure you do not want to do this! If you only want to stop for your subsystem (and not for everyone), "
						"then choose <b>Shared Run Plan Leave</b>, not <b>Stop</b>"
		);  // requiredUserPermissions

		registerFEMacroFunction(
			"Shared Run Plan Join",
				static_cast<FEVInterface::frontEndMacroFunction_t>(
						&CFOFrontEndInterface::SharedRunPlanSubsystemJoin),              	// feMacroFunction
						std::vector<std::string>{
							"Subsystem Name (CRV, Calo, Tracker, STM, ExtMon, Default = Custom)",
							"Custom Mode Bit Position (0-47, Default = 0)",
							"Custom Mode Bit Count (1-48, Default = 48)",
							"Custom Mode Bit Value (Default = 0)",
							// "Run Type (Supercycle Emulation = 1, Fixed-width Windows = 0) (Default = Fixed-width Windows)",
							"Duty Cycle (% or M:N on:event ratio, Default = 100%)",
							"Event Offset in Loop (Default = 0)",
						},  // namesOfInputArgs
						std::vector<std::string>{"Result"}, // namesOfOutputArgs
						1,
						"*",
						"This FE Macro joins the Shared CFO Run Plan with the specified subsystem and duty cycle." // Run type and duty cycle are specified."
						"<br><br>"
						"Regarding <b>Duty Cycle</b>"
						"% specifies the duty cycle percentage of active Event Windows. "
						"M:N ratio specifies M Event Windows active every N Event Windows. "
						"For example, 40% duty cycle or 2:5 ratio with 100 us windows would be two consecutive 100 us windows active every 500 us (i.e. 2 in 5 windows active). "
						"<br><br>"
						"Here are the corresponding <b>Subsystem Mode Bits</b> from docdb 4914:"

						"<br><TAB>"
						"<br>Tracker := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::Tracker)) +
						"<br>Calo := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::Calo)) +
						"<br>CRV := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::CRV)) +
						"<br>STM := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::STM)) +
						"<br>ExtMon (TEM) := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::ExtMon)) +
						"<br>HWDev := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::HWDev)) +
						"</TAB>"

						"<br>Mode Packet Definition:<TAB>"
						"<br>Event Mode Byte 1 (Resrv’d Trk)	Event Mode Byte 0 [7:3] 	Pattern Mode [2:1]	Injection Data Source [0]"
						"<br>Event Mode Byte 3 (Resrv’d CRV)	Event Mode Byte 2 (Resrv’d Calo) [7:1]	Calo Laser Injection [0]"
						"<br>Delivery Ring RF-0 Marker TDC [15:8]	Resrv’d (TEM) [7:6] (STM) [5:4] 	Subrun Handling [3:1]	On-spill Flag [0]"
						"</TAB>"
		);  // requiredUserPermissions

		std::string allCoarseLoopValues = "";
		for(size_t i = 1; i < standardNValues_.size(); ++i)
		{
			allCoarseLoopValues += std::to_string(standardNValues_[i]);
			if(i != standardNValues_.size() - 1) allCoarseLoopValues += "/";
		}
		registerFEMacroFunction(
			"Shared Run Plan Single-shot Join",
				static_cast<FEVInterface::frontEndMacroFunction_t>(
						&CFOFrontEndInterface::SharedRunPlanSubsystemSingleShotJoin),  // feMacroFunction
						std::vector<std::string>{
							"Subsystem Name (CRV, Calo, Tracker, STM, ExtMon, Default = Custom)",
							"Duty Cycle (% or M:N on:event ratio, Default = 100%)",
							"Custom Mode Bit Position (0-47, Default = 0)",
							"Custom Mode Bit Count (1-48, Default = 48)",
							"Custom Mode Bit Value (Default = 0)",
							"Single-shot Event Count (Any count allowed, executed in chunks of 1-" +
								std::to_string(standardNValues_[0]) +
								"/" + allCoarseLoopValues + ")",
						},  // namesOfInputArgs
						std::vector<std::string>{"Result"}, // namesOfOutputArgs
						1,
						"*",
						"This FE Macro joins the Shared CFO Run Plan for a single-shot (one-time) "
						"quantity of events using the OR_SINGLESHOT opcode (opcode 111). "
						"Unlike the regular <b>Shared Run Plan Join</b>, OR_SINGLESHOT fires only "
						"once in hardware, so the specified subsystem bit(s) are set for exactly "
						"the requested count of events and then permanently cleared by the "
						"corresponding AND mask.<br><br>"
						"<b>Duty Cycle</b> supports the same syntax as <b>Shared Run Plan Join</b>: "
						"either a percentage or M:N ratio. For single-shot, however, the resolved N in any "
						"M:N ratio is limited to N &le; " + std::to_string(standardNValues_[0]) +
						", so very coarse ratios (e.g., 1:200) are not allowed. This determines how many "
						"events are set per fine-loop pass before software batches additional passes."
						"<br><br>"
						"<b>Single-shot Event Count</b> can be any count but will be built out of batches of " +
								std::to_string(standardNValues_[0]) +
								"/" + allCoarseLoopValues + ".<br><br>"
						"For example, if 143 events are requested you will get a batch of 100 and 43 (each batch managed by software)."
						"<br><br>"
						"Here are the corresponding <b>Subsystem Mode Bits</b> from docdb 4914:"
						"<br><TAB>"
						"<br>Tracker := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::Tracker)) +
						"<br>Calo := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::Calo)) +
						"<br>CRV := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::CRV)) +
						"<br>STM := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::STM)) +
						"<br>ExtMon (TEM) := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::ExtMon)) +
						"<br>HWDev := bit " + std::to_string(static_cast<int>(SharedRunPlanSubsystemModeBit::HWDev)) +
						"</TAB>"
		);  // requiredUserPermissions

		registerFEMacroFunction(
			"Shared Run Plan Leave",
				static_cast<FEVInterface::frontEndMacroFunction_t>(
						&CFOFrontEndInterface::SharedRunPlanSubsystemLeave),              	// feMacroFunction
						std::vector<std::string>{
							"Subsystem Name (CRV, Calo, Tracker, STM, ExtMon, Custom)",
							"Custom Mode Bit Position (Default = 0)",
							"Custom Mode Bit Count (Default = 48)"
						},  // namesOfInputArgs

						std::vector<std::string>{"Result"}, // namesOfOutputArgs
						1,
						"*",
						"This FE Macro removes the specified subsystem from the Shared CFO Run Plan."
		);  // requiredUserPermissions
	} //end Shared Run Info FE Macro Registration ------------------

	registerFEMacroFunction(
		"Configure for Timing Chain",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::ConfigureForTimingChain),                  // feMacroFunction
					std::vector<std::string>{"StepIndex"},  // namesOfInputArgs
					std::vector<std::string>{}, // namesOfOutputArgs
					1,
					"*",
					"This FE Macro configures the CFO for DTC chain synchronization."
	);  // requiredUserPermissions

	registerFEMacroFunction(
		"Super Orchestration Start",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::SuperOrchestrationStart),                  // feMacroFunction
					std::vector<std::string>{"Number of Event Window Markers (Default: 10)"}, // namesOfInputArgs
					std::vector<std::string>{}, // namesOfOutputArgs
					1,   // requiredUserPermissions
					"*",
					"Start Super Orchestration while in a run."
	);

	registerFEMacroFunction(
		"Super Orchestration End",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::SuperOrchestrationEnd),                  // feMacroFunction
					std::vector<std::string>{}, // namesOfInputArgs
					std::vector<std::string>{}, // namesOfOutputArgs
					1,   // requiredUserPermissions
					"*",
					"End Super Orchestration while in a run."
	);

	registerFEMacroFunction(
		"Super Orchestration",
			static_cast<FEVInterface::frontEndMacroFunction_t>(
					&CFOFrontEndInterface::SuperOrchestration),                  // feMacroFunction
					std::vector<std::string>{"Do CRV ROC Reset",
											"Do Calo ROC Reset",
											"Do Calo ROC Writes"
											},  // namesOfInputArgs
					std::vector<std::string>{"response"}, // namesOfOutputArgs
					1,   // requiredUserPermissions
					"*",
					"To assist with throttling Event Window Marker rates during Global Run 4."
	);

	registerFEMacroFunction(
		"Buffer Test Detached",
		static_cast<FEVInterface::frontEndMacroFunction_t>(
			&CFOFrontEndInterface::BufferTest_detached),  // feMacroFunction
		std::vector<std::string>{
			"Command to 0/Status (to read counters, etc.), 1/Start, or 2/Halt (Default: "
			"Status)",
			// "Data are SubEvents (Default: true)", //not needed for CFO
			// "Number of [Sub]Events (Default: 1)",  // will be continuous!
			"Starting Event Window Tag (Default: 0)",
			// "Match Event Tags (Default: false)", //not needed for CFO
			// "Display Payload at GUI (Default: true)", // will be summary output
			// "eventDuration (Default := 400)",
			// "doNotReadBack (bool)",
			"Save Binary Data to File (Default: false)",
			// "Save Binary Data Filename", //not needed for CFO (not multiple CFOs)
			// "Save Subevent Header to Binary File (Default: false)", //not needed for CFO
			// "Payload Packet Threshold for Saving Event (Default: 0)" //not needed for CFO
			// "Software Generated Data Requests (bool)",
			// "Do Not Send Heartbeats (bool)"
		},
		std::vector<std::string>{"Result"}, // namesOfOutputArgs
		1,  // requiredUserPermissions
		"*",
		"Read a specified number of events from the Data DMA channel-0, and attempt to "
		"validate data."
		// "Send a request for a number of events and waits for the respective responses. "
		// "Currently, the responses are simulated data (a counter)."
	);

	registerFEMacroFunction(
		"Runplan Subrun Config Setup",
		static_cast<FEVInterface::frontEndMacroFunction_t>(
			&CFOFrontEndInterface::RunplanSubrunConfigSetup),  // feMacroFunction
		std::vector<std::string>{
			"Subrun Event Limit (Default: 0)",
			"Subrun Prediction Offset (Default: 0)"
		},  // namesOfInputArgs
		std::vector<std::string>{},  // namesOfOutputArgs
		1,  // requiredUserPermissions
		"*",  // allowedCallingFEs
		"Set the Run Plan Subrun configuration registers. "
		"The Subrun Event Limit sets the maximum number of events per subrun. "
		"The Subrun Prediction Offset sets the prediction offset for subrun transitions."
	);

	registerFEMacroFunction(
		"Runplan Subrun Config Read",
		static_cast<FEVInterface::frontEndMacroFunction_t>(
			&CFOFrontEndInterface::RunplanSubrunConfigRead),  // feMacroFunction
		std::vector<std::string>{},  // namesOfInputArgs
		std::vector<std::string>{
			"Subrun Event Limit",
			"Subrun Prediction Offset"
		},  // namesOfOutputArgs
		1,  // requiredUserPermissions
		"*",  // allowedCallingFEs
		"Read the Run Plan Subrun configuration registers. "
		"Returns the Subrun Event Limit and Subrun Prediction Offset values."
	);

	registerFEMacroFunction(
		"Loopback Topology Discovery",
		static_cast<FEVInterface::frontEndMacroFunction_t>(
			&CFOFrontEndInterface::LoopbackTopologyDiscovery),
		std::vector<std::string>{
			"Number of Loopback tests per DTC (Default := 100)",
			"Save ROC Delay Offsets to Table (Default := false)"
		},
		std::vector<std::string>{"Response"},
		1,
		"*",
		"Discovers timing chain topology by probing each DTC with CFO loopback markers. "
		"Maps every DTC to a CFO link (chain index) and position in chain."
	);
	registerFEMacroFunction(
		"Temporary Diagnostic Test",
		static_cast<FEVInterface::frontEndMacroFunction_t>(
			&CFOFrontEndInterface::TemporaryDiagnosticTest),
		std::vector<std::string>{},
		std::vector<std::string>{"Response"},
		1,
		"*",
		"Diagnostic: calls 'Get Link Lock Status' on Calo01_DTC1 via runFrontEndMacro."
	);
	// clang-format on

	CFOandDTCCoreVInterface::registerCFOandDTCFEMacros();

	__FE_COUT__ << "Done registering CFO FE Macros. Total macro count = "
	            << mapOfFEMacroFunctions_.size() << __E__;
}  //end registerFEMacros()

// //=====================================================================================
// //
// int CFOFrontEndInterface::getLinkStatus()
// {
// 	int overall_link_status = registerRead(0x9140);

// 	int link_status = (overall_link_status >> 0) & 0x1;

// 	return link_status;
// }

//=====================================================================================
// TODO: function to do a loopback test on the specified link
uint32_t CFOFrontEndInterface::measureDelay(CFOLib::CFO_Link_ID link)
{
	// TODO: how can I understand if the measure fails?
	__FE_COUT__ << "TODO Send loopback marker on link " << link << __E__;

	// thisCFO_->ResetDelayRegister();	// reset 0x9380
	// thisCFO_->DisableLinks();	// reset 0x9114
	// // configure the DTC (to configure the ROC in a loop)
	// thisCFO_->EnableLink(link, DTC_LinkEnableMode(true, true)); // enable Tx and Rx
	// thisCFO_->EnableDelayMeasureMode(link);
	// thisCFO_->EnableDelayMeasureNow(link);
	// u_int32_t delay = thisCFO_->ReadCableDelayValue(link);	// read delay
	// __FE_COUT__ << "Delay measured: " << delay << " (ns) on link: " <<  link << __E__;
	// // reset registers
	// thisCFO_->ResetDelayRegister();
	// thisCFO_->DisableLinks();

	return -1;  //delay;
}  //end measureDelay()

//=====================================================================================
// TODO: function to do a loopback test on the specified link, handle the boadcast
void CFOFrontEndInterface::LoopbackTest(__ARGS__)
{
	__FE_COUT__ << "Operation \"Loopback test\"" << std::endl;

	// stream to print the output
	std::stringstream ostr;
	ostr << std::endl;

	// parameters
	const int numberOfLoopbacksExp = 0;
	//  __GET_ARG_IN__(
	//     "Number of Loopback Exponent (Default := 3, which is 8 Loopback Markers sent)",
	//     uint32_t,
	//     3);
	const int numberOfLoopbackTests =
	    __GET_ARG_IN__("Number of Loopback tests (Default := 1)", uint32_t, 1);
	const int targetLink =
	    __GET_ARG_IN__("Target Link (-1 for all, Default := -1)", uint8_t, uint8_t(-1));
	const int targetROC =
	    __GET_ARG_IN__("Target ROC (-1 for all, Default := -1)", uint8_t, uint8_t(-1));
	const bool writeFile =
	    __GET_ARG_IN__("Write ROOT file (Default := false)", bool, false);
	const std::string fileName =
	    __GET_ARG_IN__("ROOT file name (Default := CFO_loopback.root)",
	                   std::string,
	                   "CFO_loopback.root");

	__FE_COUTV__(numberOfLoopbacksExp);
	__FE_COUTV__(targetLink);

	ostr << "Number of Loopback Markers to Send: " << (1 << numberOfLoopbacksExp)
	     << __E__;
	ostr << "Number of Loopback Tests to Perform: " << numberOfLoopbackTests << __E__;
	ostr << "Target CFO Chain/Link: "
	     << (targetLink == uint8_t(-1) ? "All" : (std::to_string(targetLink))) << __E__;

	const bool clockMarkerWasOn = thisCFO_->ReadEmbeddedClockMarkerEnable();
	__FE_COUTV__(clockMarkerWasOn);
	if(clockMarkerWasOn)
		thisCFO_->DisableEmbeddedClockMarker();

	// setup output data if requested
	int    dtc_id, roc_id;
	double output_time, output_unc;
	TTree* tree = nullptr;
	TFile* f    = nullptr;
	if(writeFile)
	{
		f = new TFile((std::string(__ENV__("OTSDAQ_DATA")) + "/" + fileName).c_str(),
		              "RECREATE");
		f->cd();
		tree = new TTree("loopback", "Loopback test results");
		// clang-format off
	  tree->Branch("dtc_id"     , &dtc_id     );
	  tree->Branch("roc_id"     , &roc_id     );
	  tree->Branch("output_time", &output_time);
	  tree->Branch("output_unc" , &output_unc );
		// clang-format on
	}

	// store the measurement results
	struct roc_result_t
	{
		double  time   = 0.;
		int     counts = 0;
		TGraph* graph  = nullptr;

		roc_result_t() {}
		roc_result_t(int dtc, int roc)
		{
			graph = new TGraph();
			graph->SetName(std::format("g_d{}_r{}", dtc, roc).c_str());
			graph->SetTitle(
			    std::format("DTC {} ROC {} delay measurements;Test;Delay [ns]", dtc, roc)
			        .c_str());
			graph->SetMarkerStyle(20);  // default to being more visible when drawn
			                            // graph->SetMarkerColor(dtc + 1);
		}

		// add a data point
		void add_point(const int test, const double delay)
		{
			time += delay;
			++counts;
			graph->AddPoint(test, delay);
		}

		// check if info is set
		bool is_valid()
		{
			if(!graph || counts <= 0)
				return false;
			const int npoints = graph->GetN();
			if(npoints <= 0)
				return false;
			if(npoints != counts)
				return false;
			return true;
		}

		// get summary info for the results
		double get_time()
		{
			if(!is_valid())
				return -1.;
			return time / counts;
		}
		double variance()
		{
			if(!is_valid())
				return -1.;
			double       variance = 0.;
			const double mean     = time / counts;
			for(int ipoint = 0; ipoint < counts; ++ipoint)
			{
				variance += std::pow(graph->GetY()[ipoint] - mean, 2);
			}
			variance /= counts;
			return variance;
		}
		double get_unc()
		{
			if(!is_valid())
				return -1.;
			const double var = variance();
			if(var <= 0.)
				return -1.;
			return std::sqrt(var / counts);
		}
		double get_min()
		{
			if(!is_valid())
				return -1.;
			double min_val = 1.e10;
			for(int ipoint = 0; ipoint < counts; ++ipoint)
			{
				min_val = std::min(graph->GetY()[ipoint], min_val);
			}
			return min_val;
		}
		double get_max()
		{
			if(!is_valid())
				return -1.;
			double max_val = -1.e10;
			for(int ipoint = 0; ipoint < counts; ++ipoint)
			{
				max_val = std::max(graph->GetY()[ipoint], max_val);
			}
			return max_val;
		}
	};

	std::map<int, roc_result_t> roc_results;  // roc measurement data

	// units the delay is reported in are 5/8 ns
	const double delay_unit = 5. / 8.;

	for(int itest = 0; itest < numberOfLoopbackTests; ++itest)
	{  // perform the test multiple times for more accurate measurement
		thisCFO_->SetCableDelayMeasureExponentialCount(numberOfLoopbacksExp);
		thisCFO_->RunCableDelayLoopbackTest();

		//wait until done with loopback test to return final measurement
		bool        cableDelayMeasureAnyDone = false;
		bool        cableDelayMeasureDone;
		uint16_t    doneLink = -1;
		uint32_t    measuredDelay;
		uint16_t    retries   = 1;
		const float wait_time = 1000. * 10.;  // 10 ms
		while(!cableDelayMeasureAnyDone && retries-- > 0)
		{
			usleep(wait_time);
			for(int link = 0; link < 8; ++link)
			{
				if(targetLink != uint8_t(-1) && link != targetLink)
					continue;
				__FE_COUTV__(link);
				for(uint16_t roc = 0; roc < 6; ++roc)
				{
					if(targetROC != uint8_t(-1) && roc != targetROC)
						continue;
					__FE_COUTV__(roc);

					const int map_index = link * 100 + roc;
					//ensure the map entries are zeroed at first
					if(itest == 0)
					{
						roc_results[map_index] = roc_result_t(link, roc);
					}

					// retrieve the measurement result
					measuredDelay =
					    delay_unit *
					    thisCFO_->ReadCableDelayMeasurement(
					        CFOLib::CFO_Link_ID(link), roc, cableDelayMeasureDone);

					// a delay was measured
					if(cableDelayMeasureDone)
					{
						if(!cableDelayMeasureAnyDone)
						{
							cableDelayMeasureAnyDone = true;
							usleep(
							    wait_time);  //sleep after the first is found to ensure all respond, then re-check
							++retries;
							link = -1;
							break;
						}
						doneLink = link;
						__FE_COUTV__(doneLink);
						roc_results[map_index].add_point(itest, measuredDelay);
					}

					__FE_COUT__
					    << "Link=" << link << " ROC=" << roc << " retriesLeft=" << retries
					    << " done=" << cableDelayMeasureDone << " delay=" << measuredDelay
					    << std::hex << "ns 0x" << measuredDelay << __E__;

					// if(cableDelayMeasureDone)
					//   {
					//     ostr << "CFO-Link=" << link << " ROC=" << roc
					// 	 << " delay "<<std::format("{:8.3f}", measuredDelay) << std::hex << " ns 0x"
					// 	 << measuredDelay << __E__;
					//   }
				}  //end ROC delay measure loop
			}      //end DTC loop
			__COUTT__ << "Loopback try cableDelayMeasureAnyDone="
			          << cableDelayMeasureAnyDone << " retries=" << retries << __E__;
		}  //end result check loop
	}      //end tests loop

	// write out the tree data if requested
	for(auto entry : roc_results)
	{
		const int map_index = entry.first;
		auto      results   = entry.second;
		const int counts    = results.counts;
		dtc_id              = map_index / 100;
		roc_id              = map_index % 100;
		output_time         = results.get_time();
		output_unc          = results.get_unc();
		if(counts > 0)
		{
			ostr << "CFO-Link=" << dtc_id << " ROC=" << roc_id << " delay "
			     << std::format("{:8.3f} +- {:5.3f}, range = {:4.1f} ({:4} responses)",
			                    output_time,
			                    output_unc,
			                    results.get_max() - results.get_min(),
			                    counts)
			     << __E__;
			if(writeFile)
			{
				tree->Fill();
				results.graph->Write();
			}
		}
	}

	// build Plotly histogram of delay measurements
	{
		std::stringstream plotlySs;
		plotlySs << R"({"data":[)";

		bool firstTrace = true;
		for(const auto& [map_index, results] : roc_results)
		{
			const int counts = results.counts;
			if(counts <= 0)
				continue;

			if(!firstTrace)
				plotlySs << ",";
			firstTrace = false;

			plotlySs << R"({"x":[)";
			for(int i = 0; i < counts; ++i)
			{
				if(i > 0)
					plotlySs << ",";
				plotlySs << results.graph->GetY()[i];
			}
			plotlySs << R"(],"type":"histogram","name":"Link )" << (map_index / 100)
			         << " ROC " << (map_index % 100) << R"(","opacity":0.75})";
		}

		plotlySs << R"(],"layout":{)"
		         << R"("title":{"text":"CFO Loopback Delay"},)"
		         << R"("xaxis":{"title":{"text":"Delay [ns]"}},)"
		         << R"("yaxis":{"title":{"text":"Count"}},)"
		         << R"("barmode":"overlay"}})";

		__SET_ARG_OUT__(PLOTLY_PLOT, plotlySs.str());
	}

	if(writeFile)
	{
		tree->Write();
		f->Close();
	}
	// sleep(1); //wait until done with loopback test to return clock markers

	if(clockMarkerWasOn)
		thisCFO_->EnableEmbeddedClockMarker();

	// // config parameters (TODO: read from config)
	// unsigned alignament_marker = 10;
	// const int MAX_LOOPBACK = 10000;
	// const int MIN_LOOPBACK = 1;

	// if (numberOfLoopback > MAX_LOOPBACK)
	// 	numberOfLoopback = MAX_LOOPBACK;

	// if (numberOfLoopback < MIN_LOOPBACK)
	// 	numberOfLoopback = MIN_LOOPBACK;

	// // variable to compuet the average
	// float avg_delay = 0.0;
	// unsigned int comulative_delay = 0;
	// // int fail_measure = 0;

	// // log the input
	// __FE_COUTV__(numberOfLoopback);
	// __FE_COUTV__(input_link);
	// __FE_COUTV__(delay);

	// ostr << "Number of loopbacks: " << numberOfLoopback << std::endl;

	// // TODO: read from config with special param
	// if (numberOfLoopback < 0)
	// {
	// 	return;
	// }
	// if (input_link < 0)
	// {
	// 	return;
	// }

	// CFOLib::CFO_Link_ID link = static_cast<CFOLib::CFO_Link_ID>(input_link);

	// // sending first marker to align the clock of the ROC
	// __FE_COUT__ << "Align the ROC's clock..." <<  __E__;
	// for (unsigned int n = 0; n < alignament_marker; ++n)
	// {
	// 	measureDelay(link);
	// }

	// // send marker
	// for (int n = 0; n < numberOfLoopback; ++n)
	// {
	// 	// TODO: check if it fail
	// 	uint32_t link_delay = measureDelay(link);
	// 	comulative_delay += link_delay;
	// 	__FE_COUT__ << "[" << (n+1) << "] Record: " << link_delay <<  __E__;
	// 	usleep(delay);	// maybe not defined
	// }

	// avg_delay = comulative_delay / numberOfLoopback;
	// ostr << "Average delay: " << avg_delay << std::endl;
	// __FE_COUT__ << "Average delay: " << avg_delay << __E__;

	// ostr << std::endl << std::endl;

	bool anySuccessful = false;
	for(const auto& [map_index, results] : roc_results)
		if(results.counts > 0)
		{
			anySuccessful = true;
			break;
		}
	if(!anySuccessful)
		ostr << "No loopback measurements were successful. Check the link status of "
		        "targeted links."
		     << std::endl;

	__SET_ARG_OUT__("Response", ostr.str());

}  // end LoopbackTest()

//=====================================================================================
void CFOFrontEndInterface::LoopbackTopologyDiscovery(__ARGS__)
{
	__FE_COUT__ << "Operation \"Loopback Topology Discovery\"" << __E__;

	const int numberOfTests = __GET_ARG_IN__(
	    "Number of Loopback tests per DTC (Default := 100)", uint32_t, 100);
	const bool saveROCDelayOffsets =
	    __GET_ARG_IN__("Save ROC Delay Offsets to Table (Default := false)", bool, false);

	__FE_COUTV__(numberOfTests);
	__FE_COUTV__(saveROCDelayOffsets);

	std::stringstream ostr;
	ostr << "\n";
	ostr << "=== Loopback Topology Discovery ==="
	     << "\n";
	ostr << "Tests per DTC: " << numberOfTests << "\n\n";

	// enumerate enabled DTC FE interfaces by traversing
	// XDAQContextTable → enabled contexts → enabled apps → FESupervisorTable → FE interfaces
	struct DTCInfo
	{
		std::string                         uid;
		std::map<unsigned int, std::string> rocByLink;  // linkID (0-5) → rocUID
	};
	std::vector<DTCInfo> dtcInfos;
	{
		auto cfgMgr   = Configurable::getConfigurationManager();
		auto contexts = cfgMgr->getNode("XDAQContextTable").getChildren();
		for(const auto& ctx : contexts)
		{
			if(!ctx.second.isEnabled())
				continue;

			try
			{
				auto apps = ctx.second.getNode("LinkToApplicationTable").getChildren();
				for(const auto& app : apps)
				{
					if(!app.second.isEnabled())
						continue;

					try
					{
						auto supNode = app.second.getNode("LinkToSupervisorTable");
						auto feChildren =
						    supNode.getNode("LinkToFEInterfaceTable").getChildren();
						for(const auto& fe : feChildren)
						{
							if(!fe.second.isEnabled())
								continue;
							if(fe.second.getNode("FEInterfacePluginName")
							       .getValue<std::string>() != "DTCFrontEndInterface")
								continue;

							DTCInfo info;
							info.uid = fe.first;

							try
							{
								auto rocChildren = fe.second.getNode("LinkToFETypeTable")
								                       .getNode("LinkToROCGroupTable")
								                       .getChildren();
								for(const auto& roc : rocChildren)
								{
									if(!roc.second.isEnabled())
										continue;
									unsigned int linkID = roc.second.getNode("linkID")
									                          .getValue<unsigned int>();
									info.rocByLink[linkID] = roc.first;
								}
							}
							catch(...)
							{
							}

							dtcInfos.push_back(std::move(info));
						}
					}
					catch(...)
					{
					}
				}
			}
			catch(...)
			{
			}
		}
	}

	__FE_COUT__ << "Found " << dtcInfos.size() << " DTC FE interfaces." << __E__;
	ostr << "Found " << dtcInfos.size() << " DTC FE interfaces:";
	for(const auto& info : dtcInfos)
		ostr << " " << info.uid;
	ostr << "\n\n";

	if(dtcInfos.empty())
	{
		ostr << "No DTC FE interfaces found. Nothing to map.\n";
		__SET_ARG_OUT__("Response", ostr.str());
		return;
	}

	// save and disable clock markers
	const bool clockMarkerWasOn = thisCFO_->ReadEmbeddedClockMarkerEnable();
	__FE_COUTV__(clockMarkerWasOn);
	if(clockMarkerWasOn)
		thisCFO_->DisableEmbeddedClockMarker();

	// per-ROC measurement result
	struct ROCMeasurement
	{
		std::string rocUID;
		std::string dtcUID;
		int         cfoLink    = -1;
		int         rocIndex   = -1;
		double      delaySum   = 0.0;
		double      delaySumSq = 0.0;
		int         hitCount   = 0;

		double avg() const { return hitCount > 0 ? delaySum / hitCount : 0.0; }
		double stddev() const
		{
			if(hitCount < 2)
				return 0.0;
			double mean = avg();
			return std::sqrt(delaySumSq / hitCount - mean * mean);
		}
	};

	// per-DTC probe result
	struct DTCProbeResult
	{
		std::string                 uid;
		int                         cfoLink  = -1;
		double                      avgDelay = 0.0;
		bool                        error    = false;
		std::string                 errorMsg;
		std::vector<ROCMeasurement> rocMeasurements;
	};
	std::vector<DTCProbeResult> probeResults;
	probeResults.reserve(dtcInfos.size());
	std::map<int, int> linkPositionCounter;

	const double delay_unit = 5.0 / 8.0;

	// set ALL DTCs to passthrough
	ostr << "Setting all DTCs to passthrough...\n";
	for(const auto& dtcInfo : dtcInfos)
	{
		std::vector<frontEndMacroArg_t> argsIn, argsOut;
		__SET_ARG_IN__("setAsPassthrough (Default := false)", true);
		runFrontEndMacro(dtcInfo.uid, "Loopback Manual Setup", argsIn, argsOut);
		__FE_COUT__ << "  " << dtcInfo.uid << " set to passthrough." << __E__;
	}
	ostr << "\n";

	// probe each DTC
	for(const auto& dtcInfo : dtcInfos)
	{
		DTCProbeResult result;
		result.uid = dtcInfo.uid;

		__FE_COUT__ << "Probing DTC: " << dtcInfo.uid << __E__;
		ostr << "Probing " << dtcInfo.uid << "... ";

		// enable loopback on this DTC
		try
		{
			std::vector<frontEndMacroArg_t> argsIn, argsOut;
			__SET_ARG_IN__("setAsPassthrough (Default := false)", false);
			runFrontEndMacro(dtcInfo.uid, "Loopback Manual Setup", argsIn, argsOut);
		}
		catch(const std::exception& e)
		{
			result.error    = true;
			result.errorMsg = e.what();
			ostr << "FAILED (loopback enable): " << e.what() << "\n";
			probeResults.push_back(result);
			continue;
		}

		// per-(cfoLink, rocIndex) accumulators
		std::map<int, std::map<int, ROCMeasurement>> linkRocMeas;

		for(int itest = 0; itest < numberOfTests; ++itest)
		{
			thisCFO_->SetCableDelayMeasureExponentialCount(0);
			thisCFO_->RunCableDelayLoopbackTest();
			usleep(10000);

			for(int link = 0; link < 8; ++link)
			{
				for(uint16_t roc = 0; roc < 6; ++roc)
				{
					bool     done;
					uint32_t rawDelay = thisCFO_->ReadCableDelayMeasurement(
					    CFOLib::CFO_Link_ID(link), roc, done);
					if(done)
					{
						double delayNs = rawDelay * delay_unit;
						auto&  m       = linkRocMeas[link][roc];
						if(m.hitCount == 0)
						{
							m.cfoLink  = link;
							m.rocIndex = roc;
							m.dtcUID   = dtcInfo.uid;
							auto it    = dtcInfo.rocByLink.find(roc);
							m.rocUID   = (it != dtcInfo.rocByLink.end())
							                 ? it->second
							                 : "ROC_" + std::to_string(roc);
						}
						m.delaySum += delayNs;
						m.delaySumSq += delayNs * delayNs;
						m.hitCount++;
					}
				}
			}
		}

		// determine which CFO link responded (pick the one with most total hits)
		int    bestLink      = -1;
		int    bestTotalHits = 0;
		double bestTotalSum  = 0.0;
		for(const auto& [link, rocMap] : linkRocMeas)
		{
			int    totalHits = 0;
			double totalSum  = 0.0;
			for(const auto& [roc, m] : rocMap)
			{
				totalHits += m.hitCount;
				totalSum += m.delaySum;
			}
			if(totalHits > bestTotalHits)
			{
				bestLink      = link;
				bestTotalHits = totalHits;
				bestTotalSum  = totalSum;
			}
		}

		result.cfoLink  = bestLink;
		result.avgDelay = bestTotalHits > 0 ? bestTotalSum / bestTotalHits : 0.0;

		// collect ROC measurements for the responding link
		if(bestLink >= 0)
		{
			for(auto& [roc, m] : linkRocMeas[bestLink])
				result.rocMeasurements.push_back(std::move(m));
		}

		if(bestLink >= 0)
		{
			int pos = linkPositionCounter[bestLink]++;
			ostr << "Link " << bestLink << ", Pos " << pos << ", avg delay "
			     << std::format("{:.1f}", result.avgDelay) << " ns"
			     << " (" << result.rocMeasurements.size() << " ROCs responded)\n";
		}
		else
			ostr << "no response\n";

		probeResults.push_back(std::move(result));

		// restore passthrough on this DTC
		try
		{
			std::vector<frontEndMacroArg_t> argsIn, argsOut;
			__SET_ARG_IN__("setAsPassthrough (Default := false)", true);
			runFrontEndMacro(dtcInfo.uid, "Loopback Manual Setup", argsIn, argsOut);
		}
		catch(const std::exception& e)
		{
			__FE_COUT_WARN__ << "Failed to restore passthrough on " << dtcInfo.uid << ": "
			                 << e.what() << __E__;
		}
	}

	// build topology: group by CFO link, sort by delay ascending
	struct ChainEntry
	{
		std::string                 dtcUID;
		double                      avgDelay;
		std::vector<ROCMeasurement> rocMeas;
	};
	std::map<int, std::vector<ChainEntry>> chainMap;
	for(auto& r : probeResults)
	{
		if(r.cfoLink >= 0 && !r.error)
			chainMap[r.cfoLink].push_back(
			    {r.uid, r.avgDelay, std::move(r.rocMeasurements)});
	}

	for(auto& [link, entries] : chainMap)
		std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
			return a.avgDelay < b.avgDelay;
		});

	// count discovered vs not discovered
	size_t discoveredCount = 0;
	for(const auto& r : probeResults)
		if(r.cfoLink >= 0 && !r.error)
			++discoveredCount;
	size_t notDiscoveredCount = probeResults.size() - discoveredCount;

	// format topology output (DTC-level)
	ostr << "\n=== Timing Chain Topology ===\n";
	ostr << "Discovered: " << discoveredCount << " of " << probeResults.size()
	     << " DTCs\n";

	if(chainMap.empty())
	{
		ostr << "\nNo DTCs responded to loopback. Check fiber connections.\n";
	}
	else
	{
		ostr << "\n";
		for(const auto& [link, entries] : chainMap)
		{
			ostr << "Chain " << link << " --> ";
			for(size_t i = 0; i < entries.size(); ++i)
			{
				if(i > 0)
					ostr << ", ";
				ostr << entries[i].dtcUID << " ("
				     << std::format("{:.1f}", entries[i].avgDelay) << " ns)";
			}
			ostr << "\n";
		}
	}

	// list any DTCs that were not discovered
	if(notDiscoveredCount > 0)
	{
		ostr << "\nNot Discovered (" << notDiscoveredCount << "):";
		for(const auto& r : probeResults)
		{
			if(r.cfoLink < 0 || r.error)
			{
				ostr << " " << r.uid;
				if(r.error)
					ostr << " (error)";
				else
					ostr << " (no loopback response)";
			}
		}
		ostr << "\n";
	}

	// optionally save ROC delay offsets to ROCInterfaceTable
	{
		std::vector<ROCLoopbackResult> allROCResults;
		for(const auto& [link, entries] : chainMap)
			for(const auto& entry : entries)
				for(const auto& rm : entry.rocMeas)
					if(rm.hitCount > 0)
						allROCResults.push_back({rm.rocUID, rm.avg(), rm.stddev()});

		if(saveROCDelayOffsets && !allROCResults.empty())
		{
			ostr << "\n--- Modifying ROCInterfaceTable EventWindowDelayOffset ---\n";
			TableVersion newVer =
			    ModifyROCMarkerDelayOffsetConfiguration(ostr, allROCResults);
			ostr << "New table version: " << newVer << "\n";
		}
		else
		{
			TableVersion activeVer = getConfigurationManager()
			                             ->getTableByName("ROCInterfaceTable")
			                             ->getView()
			                             .getVersion();
			ostr << "\nDid not save ROC delay offsets to table (active version: "
			     << activeVer << ")\n";
		}
	}

	// build Plotly subplots: one histogram per chain position (hop)
	{
		// find max chain depth
		size_t maxDepth = 0;
		for(const auto& [link, entries] : chainMap)
			maxDepth = std::max(maxDepth, entries.size());

		std::stringstream plotlySs;
		plotlySs << R"({"data":[)";

		bool firstTrace = true;
		for(size_t pos = 0; pos < maxDepth; ++pos)
		{
			int         subplotIdx = static_cast<int>(pos) + 1;
			std::string xaxis =
			    (subplotIdx == 1) ? "x" : "x" + std::to_string(subplotIdx);
			std::string yaxis =
			    (subplotIdx == 1) ? "y" : "y" + std::to_string(subplotIdx);

			for(const auto& [link, entries] : chainMap)
			{
				if(pos >= entries.size())
					continue;
				for(const auto& rm : entries[pos].rocMeas)
				{
					if(rm.hitCount == 0)
						continue;
					if(!firstTrace)
						plotlySs << ",";
					firstTrace = false;

					plotlySs << R"({"x":[)" << std::format("{:.1f}", rm.avg())
					         << R"(],"type":"histogram","name":")" << rm.rocUID
					         << R"(","xaxis":")" << xaxis << R"(","yaxis":")" << yaxis
					         << R"(","opacity":0.75})";
				}
			}
		}

		plotlySs << R"(],"layout":{)"
		         << R"("title":{"text":"Loopback Delay by Chain Position"},)";

		plotlySs << R"("grid":{"rows":)" << maxDepth
		         << R"(,"columns":1,"pattern":"independent"},)";

		for(size_t pos = 0; pos < maxDepth; ++pos)
		{
			int         idx    = static_cast<int>(pos) + 1;
			std::string suffix = (idx == 1) ? "" : std::to_string(idx);
			plotlySs << R"("xaxis)" << suffix << R"(":{"title":{"text":"Delay [ns]"}},)";
			plotlySs << R"("yaxis)" << suffix << R"(":{"title":{"text":"Position )" << pos
			         << R"( Count"}},)";
		}

		plotlySs << R"("barmode":"overlay"})";
		plotlySs << "}";

		__SET_ARG_OUT__(PLOTLY_PLOT, plotlySs.str());
	}

	// leave all DTCs in passthrough
	for(const auto& dtcInfo : dtcInfos)
	{
		try
		{
			std::vector<frontEndMacroArg_t> argsIn, argsOut;
			__SET_ARG_IN__("setAsPassthrough (Default := false)", true);
			runFrontEndMacro(dtcInfo.uid, "Loopback Manual Setup", argsIn, argsOut);
		}
		catch(...)
		{
		}
	}

	// restore clock markers
	if(clockMarkerWasOn)
		thisCFO_->EnableEmbeddedClockMarker();

	__SET_ARG_OUT__("Response", ostr.str());

}  // end LoopbackTopologyDiscovery()

//=====================================================================================
TableVersion CFOFrontEndInterface::ModifyROCMarkerDelayOffsetConfiguration(
    std::ostream&                         os,
    const std::vector<ROCLoopbackResult>& rocResults,
    int                                   minROCdelayOffset /* = 0 */)
{
	__FE_COUT__ << "ModifyROCMarkerDelayOffsetConfiguration()" << __E__;

	if(rocResults.empty())
	{
		__FE_COUT_WARN__ << "No ROC results to process." << __E__;
		os << "No ROC results to process.\n";
		return TableVersion();
	}

	// find max delay across all ROCs
	double maxDelay = 0.0;
	for(const auto& roc : rocResults)
		if(roc.avgDelay > maxDelay)
			maxDelay = roc.avgDelay;

	// compute offsets: max delay ROC gets minROCdelayOffset,
	// all others get (maxDelay - theirDelay) + minROCdelayOffset
	struct ROCOffset
	{
		std::string rocUID;
		int         offset;
	};
	std::vector<ROCOffset> rocOffsets;
	rocOffsets.reserve(rocResults.size());

	os << "Max loopback delay: " << std::format("{:.1f}", maxDelay)
	   << " ns, minROCdelayOffset: " << minROCdelayOffset << "\n";

	for(const auto& roc : rocResults)
	{
		int offset =
		    static_cast<int>(std::round(maxDelay - roc.avgDelay)) + minROCdelayOffset;
		rocOffsets.push_back({roc.rocUID, offset});
		os << "  " << roc.rocUID << "  avg=" << std::format("{:.1f}", roc.avgDelay)
		   << " ns  stddev=" << std::format("{:.1f}", roc.stddev)
		   << " ns  offset=" << offset << "\n";
	}

	// get active version from the state machine's config manager
	const std::string tableName = "ROCInterfaceTable";
	TableVersion      activeVersion =
	    getConfigurationManager()->getTableByName(tableName)->getView().getVersion();
	__FE_COUTV__(activeVersion);
	os << "Active " << tableName << " version: " << activeVersion << "\n";

	// create ConfigurationManagerRW (do NOT initializeActiveGroups)
	std::string            author = "CFOTopologyDiscovery";
	ConfigurationManagerRW cfgMgrInst(author);
	cfgMgrInst.getAllTableInfo(true /* refresh */,
	                           0 /* accumulatedWarnings */,
	                           "" /* errorFilterName */,
	                           false /* getGroupKeys */,
	                           false /* getGroupInfo */,
	                           false /* initializeActiveGroups */);

	// create temporary view from the active version
	TableBase*   table            = cfgMgrInst.getTableByName(tableName);
	TableVersion temporaryVersion = table->createTemporaryView(activeVersion);
	TableView*   cfgView          = table->getTemporaryView(temporaryVersion);

	// modify EventWindowDelayOffset for each ROC
	const unsigned int colOffset = cfgView->findCol("EventWindowDelayOffset");
	for(const auto& ro : rocOffsets)
	{
		try
		{
			unsigned int row = cfgView->findRow(cfgView->getColUID(), ro.rocUID);
			cfgView->setValueAsString(std::to_string(ro.offset), row, colOffset);
			__FE_COUT__ << "Set " << ro.rocUID
			            << " EventWindowDelayOffset = " << ro.offset << __E__;
		}
		catch(const std::exception& e)
		{
			__FE_COUT_WARN__ << "ROC UID '" << ro.rocUID << "' not found in " << tableName
			                 << ": " << e.what() << __E__;
			os << "  WARNING: ROC '" << ro.rocUID << "' not found in table.\n";
		}
	}

	// validate
	try
	{
		cfgView->init();
	}
	catch(const std::runtime_error& e)
	{
		__FE_SS__ << "Error validating table: " << e.what() << __E__;
		__FE_SS_THROW__;
	}

	// print for debugging
	{
		std::stringstream tableSs;
		cfgView->print(tableSs);
		__FE_COUTV__(tableSs.str());
		os << "\n" << tableSs.str() << "\n";
	}

	// save as temporary version
	bool         foundEquivalent;
	TableVersion newVersion = cfgMgrInst.saveModifiedVersion(tableName,
	                                                         activeVersion,
	                                                         true /* makeTemporary */,
	                                                         table,
	                                                         cfgView->getVersion(),
	                                                         false /* ignoreDuplicates */,
	                                                         true /* lookForEquivalent */,
	                                                         &foundEquivalent);

	// TODO: uncomment for permanent save once verified:
	// TableVersion permVersion = cfgMgrInst.saveModifiedVersion(
	//     tableName, activeVersion, false /* makeTemporary */,
	//     table, cfgView->getVersion(),
	//     false /* ignoreDuplicates */, true /* lookForEquivalent */, &foundEquivalent);

	__FE_COUTV__(newVersion);
	__FE_COUTV__(foundEquivalent);

	os << "Saved temporary " << tableName << " version: " << newVersion << "\n";

	return newVersion;
}  // end ModifyROCMarkerDelayOffsetConfiguration()

//=====================================================================================
// TODO: function to do a loopback test on the specified link
void CFOFrontEndInterface::TestMarker(__ARGS__)
{
	__FE_COUT__ << "Operation \"Marker test\"" << std::endl;

	// stream to print the output
	std::stringstream ostr;
	ostr << std::endl;

	// parameters (TODO: make the default)
	int input_link = __GET_ARG_IN__("DTC-chain link index (0-7)", uint8_t);

	if(input_link < 0)
	{
		return;
	}

	__FE_COUTV__(input_link);
	CFOLib::CFO_Link_ID link       = static_cast<CFOLib::CFO_Link_ID>(input_link);
	uint32_t            link_delay = measureDelay(link);

	ostr << "Marker sent on link: " << link << std::endl
	     << "\t Delay: " << link_delay << std::endl;
	__FE_COUT__ << "Marker sent on link: " << link << std::endl
	            << "\t Delay: " << link_delay << std::endl;

	ostr << std::endl << std::endl;
	__SET_ARG_OUT__("Response", ostr.str());
}  // end TestMarker()

//=====================================================================================
//
float CFOFrontEndInterface::MeasureLoopback(int linkToLoopback)
{
	/* COMMENTED 20-Jun-2023 by rrivera to start using CFO_Register directly.. will need to add features to support loopback revival
	const int maxNumberOfLoopbacks = 10000;
	int       numberOfLoopbacks =
		getConfigurationManager()
			->getNode("/Mu2eGlobalsTable/SyncDemoConfig/NumberOfLoopbacks")
			.getValue<unsigned int>();

	__FE_COUTV__(numberOfLoopbacks);

	// prepare histograms
	float numerator   = 0.0;
	float denominator = 0.0;
	failed_loopback_  = 0;

	int loopback_data[maxNumberOfLoopbacks] = {};

	max_distribution_ = 0;      // maximum value of the histogram
	min_distribution_ = 99999;  // minimum value of the histogram

	for(int n = 0; n < 10000; n++)
		loopback_distribution_[n] = 0;  // zero out the histogram

	// get initial states
	unsigned initial_9380 = registerRead(0x9380);
	unsigned initial_9114 = registerRead(0x9114);

	// clean up after the DTC has done all of its resetting...
	__FE_COUT__ << "LOOPBACK: CFO reset serdes RX " << __E__;
	registerWrite(0x9118, 0x000000ff);
	registerWrite(0x9118, 0x0);
	sleep(1);

	__FE_COUT__ << "LOOPBACK: CFO status before loopback" << __E__;
	readStatus();

	//	__FE_COUT__ << "LOOPBACK: BEFORE max_distribution_: " <<
	// max_distribution_ << __E__;

	for(int n = 0; n <= numberOfLoopbacks; n++)
	{
		//----------take out of delay measurement mode
		registerWrite(0x9380, 0x00000000);

		//-------- Disable tx and rx data
		registerWrite(0x9114, 0x00000000);

		//--- enable tx and rx for link linkToLoopback
		int dataToWrite = (0x00000101 << linkToLoopback);
		registerWrite(0x9114, dataToWrite);

		//----- Put linkToLoopback in delay measurement mode
		dataToWrite = (0x00000100 << linkToLoopback);
		registerWrite(0x9380, dataToWrite);

		//------ begin delay measurement
		dataToWrite = (0x00000101 << linkToLoopback);
		registerWrite(0x9380, dataToWrite);
		usleep(5);

		//--------read delay value
		unsigned int delay = registerRead(0x9360);

		//__COUT_INFO__ << "LOOPBACK iteration " << std::dec << n << " gives " << delay <<
		//__E__;

		if(delay < 10000 && n > 5)
		{  // skip the first events since the ROC is
		   // resetting its alignment

			numerator += (float)delay;
			denominator += 1.0;
			// 		__FE_COUT__ << "LOOPBACK iteration " << std::dec << n <<
			// " gives " << delay << __E__;

			loopback_data[n] = delay;

			loopback_distribution_[delay]++;

			if(delay > max_distribution_)
			{
				max_distribution_ = delay;
				//		    __FE_COUT__ << "LOOPBACK: new max_distribution_: " <<
				// max_distribution_ << __E__;
			}

			if(delay < min_distribution_)
				min_distribution_ = delay;
		}
		else
		{
			loopback_data[n] = -999;

			if(n > 5)
			{  // skip the first events since the ROC is resetting its alignment
				failed_loopback_++;
			}
		}

		//----------clear delay measurement mode
		// registerWrite(0x9380,0x00000000);

		//-------- Disable tx and rx data
		// registerWrite(0x9114,0x00000000);

		usleep(5);
	}

	__FE_COUT__ << "LOOPBACK: CFO status after loopback" << __E__;
	readStatus();

	// return back to initial state
	registerWrite(0x9380, initial_9380);
	registerWrite(0x9114, initial_9114);

	// ---------------------------
	// do a little bit of analysis
	// ---------------------------

	average_loopback_ = -999.;
	if(denominator > 0.5)
		average_loopback_ = numerator / denominator;

	rms_loopback_ = 0.;

	for(int n = 0; n < numberOfLoopbacks; n++)
	{
		if(loopback_data[n] > 0)
		{
			rms_loopback_ += (loopback_data[n] - average_loopback_) *
							 (loopback_data[n] - average_loopback_);
		}
	}

	if(denominator > 0.5)
	{
		rms_loopback_ = sqrt(rms_loopback_ / denominator);
		average_loopback_ *= 5;  // convert from 5ns bins (200MHz) to 1ns bins
		rms_loopback_ *= 5;      // convert from 5ns bins (200MHz) to 1ns bins
	}

	__FE_COUT__ << "LOOPBACK: distribution: " << __E__;
	//	__FE_COUT__ << "LOOPBACK: min_distribution_: " << min_distribution_ <<
	//__E__;
	//	__FE_COUT__ << "LOOPBACK: max_distribution_: " << max_distribution_ <<
	//__E__;

	for(unsigned int n = (min_distribution_ - 5); n < (max_distribution_ + 5); n++)
	{
		__COUT_INFO__ << " delay [ " << n << " ] = " << loopback_distribution_[n] << __E__;
	}

	__COUT_INFO__ << " average = " << average_loopback_ << " ns, RMS = " << rms_loopback_
								 << " ns, failures = " << failed_loopback_ << __E__;

	__FE_COUT__ << __E__;

	__FE_COUT__ << "LOOPBACK: number of failed loopbacks = " << std::dec
				<< failed_loopback_ << __E__;
*/
	return 0;  //average_loopback_;

}  // end MeasureLoopback()

//==============================================================================
void CFOFrontEndInterface::configureSlowControls(void)
{
	__FE_COUTV__(skipInit_);
	if(skipInit_)
		return;

	bool slowControlsEnable = true;
	try
	{
		slowControlsEnable = getSelfNode().getNode("SlowControlsEnable").getValue<bool>();
	}
	catch(...)
	{
		__FE_COUT__ << "Missing `SlowControlsEnable` in configuration, "
		               "SlowControlsEnable defaults to "
		            << slowControlsEnable << __E__;
	}

	if(!slowControlsEnable)
	{
		__FE_COUT__ << "Slow controls are disabled..." << __E__;
		return;
	}
	__FE_COUT__ << "Configuring slow controls..." << __E__;

	FEVInterface::configureSlowControls();

	__FE_COUT__ << "CFO '" << getInterfaceUID()
	            << "' slow controls channel count: " << getSlowControlsChannelCount()
	            << __E__;

	__FE_COUT__ << "Done configuring CFO slow controls." << __E__;
}  // end configureSlowControls()

//===============================================================================================
void CFOFrontEndInterface::configure(void)
{
	__FE_COUTV__(getIterationIndex());
	__FE_COUTV__(getSubIterationIndex());

	// if(regWriteMonitorStream_.is_open())
	// {
	// 	regWriteMonitorStream_ << "Timestamp: " << std::dec << time(0) <<
	// 		", \t ---------- Start configure step " <<
	// 		getIterationIndex() << ":" << getSubIterationIndex() << "\n";
	// 	regWriteMonitorStream_.flush();
	// }

	if(isFirstIteration())
	{
		recordTimeAlive();
		resetConfigPhase();
		timing_chain_first_substep_ = -1;
	}

	if(skipInit_)
		return;

	testAndUpdateTimeAlive("Configure");

	if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_HARDWARE_DEV)
	{
		__FE_COUT_INFO__ << "Not configuring CFO for hardware development mode!" << __E__;
		return;
	}
	else if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING ||
	        operatingMode_ ==
	            CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING_AND_SYNC ||
	        operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_LOOPBACK)
	{
		__FE_COUT_INFO__ << "Configuring for Event Building mode!" << __E__;
		configureEventBuildingMode();
	}
	// else if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_LOOPBACK)
	// {
	// 	__FE_COUT_INFO__ << "Configuring for Loopback mode!" << __E__;
	// 	configureEventBuildingMode();
	// }
	else
	{
		__FE_SS__ << "Unknown system operating mode: " << operatingMode_ << __E__
		          << " Please specify a valid operating mode in the 'Mu2eGlobalsTable.'"
		          << __E__;
		__FE_SS_THROW__;
	}

	return;

	// NOTE: otsdaq/xdaq has a soap reply timeout for state transitions.
	// Therefore, break up configuration into several steps so as to reply before
	// the time out As well, there is a specific order in which to configure the
	// links in the chain of CFO->DTC0->DTC1->...DTCN

	const int number_of_system_configs = 2;  // if < 0, keep trying until links are OK.
	    // If > 0, go through configuration steps this many times
	int       config_clock = configure_clock_;  // 1 = yes, 0 = no
	const int reset_tx     = 1;                 // 1 = yes, 0 = no

	const int number_of_dtc_config_steps = 7;

	int number_of_total_config_steps =
	    number_of_system_configs * number_of_dtc_config_steps;

	int config_step = getIterationIndex();

	if(number_of_system_configs > 0)
	{
		if(config_step >= number_of_total_config_steps)  // done - exit system config
			return;
	}

	if((config_step % number_of_dtc_config_steps) == 0)
	{
		// disable outputs
		thisCFO_->DisableAllOutputs();

		// __FE_COUT__ << "CFO disable Event Start character output " << __E__;
		// registerWrite(0x9100, 0x0);

		// __FE_COUT__ << "CFO disable serdes transmit and receive " << __E__;
		// registerWrite(0x9114, 0x00000000);

		// __FE_COUT__ << "CFO turn off Event Windows" << __E__;
		// registerWrite(0x91a0, 0x00000000);

		// __FE_COUT__ << "CFO turn off 40MHz marker interval" << __E__;
		// registerWrite(0x9154, 0x00000000);
	}
	else if((config_step % number_of_dtc_config_steps) == 1)
	{
		// reset clocks

		if(config_clock == 1 && config_step < number_of_dtc_config_steps)
		{
			// only configure the clock/crystal the first loop through...

			__FE_COUT_INFO__ << "Step " << config_step << ": CFO reset clock..." << __E__;

			__FE_COUT__ << "CFO set crystal frequency to 156.25 MHz" << __E__;
			thisCFO_->SetSERDESOscillatorFrequency(0x09502F90);
			// registerWrite(0x9160, 0x09502F90);

			// set RST_REG bit
			thisCFO_->WriteSERDESIICInterface(
			    DTC_IICSERDESBusAddress::DTC_IICSERDESBusAddress_EVB /* device */,
			    0x87 /* address */,
			    0x01 /* data */);
			// registerWrite(0x9168, 0x55870100);
			// registerWrite(0x916c, 0x00000001);

			// sleep(5);

			//-----begin code snippet pulled from: mu2eUtil program_clock -C 0 -F
			// 200000000 ---
			// C=0 = main board SERDES clock
			// C=1 = DDR clock
			// C=2 = Timing board SERDES clock

			int targetFrequency = 200000000;

			//auto oscillator = DTCLib::DTC_OscillatorType_SERDES;  //-C 0 = CFO (main
			// board SERDES clock)
			// auto oscillator = DTCLib::DTC_OscillatorType_DDR; //-C 1 (DDR clock)
			// auto oscillator = DTCLib::DTC_OscillatorType_Timing; //-C 2 = DTC (with
			// timing card)

			__FE_COUT__ << "CFO set oscillator frequency to " << std::dec
			            << targetFrequency << " MHz" << __E__;

			thisCFO_->SetNewOscillatorFrequency(targetFrequency);

			//-----end code snippet pulled from: mu2eUtil program_clock -C 0 -F
			// 200000000

			sleep(5);
		}
		else
		{
			__FE_COUT_INFO__ << "Step " << config_step << ": CFO do NOT reset clock..."
			                 << __E__;
		}
	}
	else if((config_step % number_of_dtc_config_steps) == 3)
	{
		// after DTC jitter attenuator OK, config CFO SERDES PLLs and TX
		if(reset_tx == 1)
		{
			__FE_COUT_INFO__ << "Step " << config_step << ": CFO reset TX..." << __E__;

			__FE_COUT__ << "CFO reset serdes PLLs " << __E__;
			thisCFO_->ResetAllSERDESPlls();
			// registerWrite(0x9118, 0x0000ff00);
			// registerWrite(0x9118, 0x0);
			// sleep(3);

			__FE_COUT__ << "CFO reset serdes TX " << __E__;
			thisCFO_->ResetAllSERDESTx();
			// registerWrite(0x9118, 0x00ff0000);
			// registerWrite(0x9118, 0x0);
			// sleep(3);
		}
		else
		{
			__FE_COUT_INFO__ << "Step " << config_step << "CFO do NOT reset TX..."
			                 << __E__;
		}
	}
	else if((config_step % number_of_dtc_config_steps) == 6)
	{
		__FE_COUT_INFO__ << "Step " << config_step
		                 << ": CFO enable Event start characters, SERDES Tx "
		                    "and Rx, and event window interval"
		                 << __E__;

		__FE_COUT__ << "CFO reset serdes RX " << __E__;
		thisCFO_->ResetSERDES(CFOLib::CFO_Link_ID::CFO_Link_ALL);
		// registerWrite(0x9118, 0x000000ff);
		// registerWrite(0x9118, 0x0);
		// sleep(3);

		__FE_COUT__ << "CFO enable Event Start character output " << __E__;
		thisCFO_->EnableEmbeddedClockMarker();
		thisCFO_->EnableAcceleratorRF0();
		thisCFO_->SetPunchEnable();
		// registerWrite(0x9100, 0x5); //bit-0 is clock enable, bit-2 enables accelerator RF-0 input

		__FE_COUT__ << "CFO enable serdes transmit and receive " << __E__;
		thisCFO_->EnableLink(CFOLib::CFO_Link_ID::CFO_Link_ALL);
		// registerWrite(0x9114, 0x0000ffff);

		__FE_COUT__ << "CFO Event Window interval time now controlled by CFO Run Plan, "
		               "as of Firmware version: Nov/09/2023 11:00"
		            << __E__;
		// thisCFO_->SetEventWindowEmulatorInterval(0x1f40 /* 40us */); //0x154 = 1.7us, 0x1f40 = 40us, 0 = NO markers
		//    registerWrite(0x91a0,0x154);   //1.7us
		// registerWrite(0x91a0, 0x1f40);  // 40us
		// 	registerWrite(0x91a0,0x00000000); 	// for NO markers, write these
		// values

		// __FE_COUT__ << "CFO set 40MHz marker interval" << __E__;
		// thisCFO_->SetClockMarkerIntervalCount(0x0800);  // 0 = NO markers
		// registerWrite(0x9154, 0x0800);
		// 	registerWrite(0x9154,0x00000000); 	// for NO markers, write these
		// values

		__FE_COUT_INFO__ << "--------------" << __E__;
		__FE_COUT_INFO__ << "CFO configured" << __E__;

		if(thisCFO_->ReadSERDESRXCDRLock(CFOLib::CFO_Link_ID::CFO_Link_0))
		{
			__FE_COUT_INFO__ << "CFO links OK \n"
			                 << thisCFO_->FormatSERDESRXCDRLock() << __E__;

			if(number_of_system_configs < 0)
			{
				return;  // links OK, kick out
			}
		}
		else
		{
			__FE_COUT_INFO__ << "CFO links not OK \n"
			                 << thisCFO_->FormatSERDESRXCDRLock() << __E__;
		}
		__FE_COUT__ << __E__;
	}

	__FE_COUT__
	    << "\n"
	    << thisCFO_->FormattedRegDump(
	           130,
	           thisCFO_->formattedDumpFunctions_);  // spit out link status at every step
	indicateIterationWork();  // indicate still more configure transition work to do
	return;
}  //end configure()

//==============================================================================
void CFOFrontEndInterface::configureEventBuildingMode(int step)
{
	if(step == -1)
	{
		// The CFO subsystem always runs its pass first (subsystem-iteration 0, or
		// STANDALONE). Kept as a guard for symmetry with the DTC turn rule.
		if(!isMyConfigureSubsystemIteration(
		       CFOandDTCCoreVInterface::CONFIG_SUBSYSTEM_ITERATION_CFO_SUBSYSTEM))
		{
			__FE_COUT__ << "Not the CFO subsystem's turn (subsystem-iteration "
			            << getSubsystemIterationIndexString() << "); waiting." << __E__;
			indicateSubsystemIterationWork();
			return;
		}
		step = configPhase();
	}

	__FE_COUT_INFO__ << "configureEventBuildingMode() phase=" << step
	                 << " (subsystem-iteration " << getSubsystemIterationIndexString()
	                 << ", iteration " << getIterationIndex() << ")" << __E__;

	if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_CLOCKS_A)
	{
		// Phase 1a: CFO establishes clocks (DTCs w/o real ROCs also run at this iteration)
		if(timing_chain_first_substep_ == -1)
			timing_chain_first_substep_ = getSubIterationIndex();

		configureForTimingChain();
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_CLOCKS_B)
	{
		__FE_COUT__ << "Idle while DTCs with real ROCs establish clocks..." << __E__;
		timing_chain_first_substep_ = -1;
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_TIMING_CHAIN_ENABLE)
	{
		// Phase 2a: Enable all CFO link outputs + clock markers
		thisCFO_->EnableLink(CFOLib::CFO_Link_ID::CFO_Link_ALL);
		thisCFO_->EnableEmbeddedClockMarker();
		__FE_COUT__ << "Enabled all CFO links and embedded clock markers." << __E__;

		// Also enable the CFO link on every DTC in this subsystem, so the chain bring-up
		// (CDR check, edge fix) does not depend on the DTC front end having acted yet.
		{
			std::vector<std::string> dtcUIDs =
			    getSubsystemFEUIDsOfPlugin("DTCFrontEndInterface");
			__FE_COUT__ << "Enabling CFO link (link 6, Tx+Rx) on " << dtcUIDs.size()
			            << " DTC(s) in this subsystem via FE macro..." << __E__;
			for(const auto& dtcUID : dtcUIDs)
			{
				std::vector<FEVInterface::frontEndMacroArg_t> argsIn, argsOut;
				argsIn.emplace_back("Target Link (Default = -1 := all links)", "6");
				argsIn.emplace_back("Set Link Tx Enable (Default := false)", "1");
				argsIn.emplace_back("Set Link Rx Enable (Default := false)", "1");
				try
				{
					runFrontEndMacro(dtcUID, "Enable/Disable DTC Link", argsIn, argsOut);
				}
				catch(const std::exception& e)
				{
					__FE_SS__
					    << "Phase 2a (Timing Chain: Enable): failed to enable the CFO "
					       "link on DTC "
					    << dtcUID
					    << " via FE Macro 'Enable/Disable DTC Link': " << e.what();
					__FE_SS_THROW__;
				}
				__FE_COUT__ << "DTC " << dtcUID << " CFO link enabled." << __E__;
			}
		}

		if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING ||
		   operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING_AND_SYNC)
		{
			CompileSetAndLaunchTemplateFixedWidthRunPlan(
			    true,     // enable
			    false,    // useDetachedBufferTest
			    "1.7us",  // eventDuration
			    0,        // numberOfEventWindowMarkers (0 = infinite)
			    0,        // initialEventWindowTag
			    0,        // eventWindowMode (null heartbeat — marker traffic only)
			    true,     // enableClockMarkers
			    false,    // saveBinaryDataToFile
			    false,    // saveSubeventHeadersToDataFile
			    false);   // doNotResetCounters
			__FE_COUT__ << "Started fixed-width event run plan for aggressive 8b10 "
			               "traffic during edge fix phases."
			            << __E__;
		}

		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_TIMING_CHAIN_CHECK)
	{
		// Phase 2b: CDR lock check
		if(timing_chain_first_substep_ == -1)
			timing_chain_first_substep_ = getSubIterationIndex();

		int subStep = getSubIterationIndex() - timing_chain_first_substep_;
		__FE_COUT_INFO__ << "Phase 2b — CDR lock check, sub-step=" << subStep << __E__;

		bool isRetry = (subStep == 1);
		__FE_COUT__ << "Checking DTC CFO CDR lock ("
		            << (isRetry ? "retry" : "1st attempt") << ")..." << __E__;

		struct DTCLockInfo
		{
			std::string uid;
			bool        cfoCDRLocked = false;
		};
		std::vector<DTCLockInfo> dtcLockInfos;
		for(const auto& uid : getSubsystemFEUIDsOfPlugin("DTCFrontEndInterface"))
		{
			DTCLockInfo info;
			info.uid = uid;
			dtcLockInfos.push_back(std::move(info));
		}

		__FE_COUT__ << "Found " << dtcLockInfos.size() << " DTC FE interfaces." << __E__;

		std::vector<std::string> unlockedDTCs;
		for(auto& dtcLock : dtcLockInfos)
		{
			std::vector<FEVInterface::frontEndMacroArg_t> argsIn, argsOut;
			try
			{
				runFrontEndMacro(dtcLock.uid, "Get Link Lock Status", argsIn, argsOut);

				__FE_COUT__ << "DTC " << dtcLock.uid
				            << " argsOut.size()=" << argsOut.size() << __E__;
				for(size_t ai = 0; ai < argsOut.size(); ++ai)
					__FE_COUT__ << "  argsOut[" << ai << "] name='" << argsOut[ai].first
					            << "' val='" << argsOut[ai].second << "'" << __E__;

				std::string lockStatus = __GET_ARG_OUT__("Lock Status", std::string);
				__FE_COUT__ << "DTC " << dtcLock.uid << " Lock Status: " << lockStatus
				            << __E__;

				bool               foundCFOLine = false;
				std::istringstream iss(lockStatus);
				std::string        line;
				while(std::getline(iss, line))
				{
					if(line.find("CFO") != std::string::npos &&
					   line.find("CDR Lock") != std::string::npos)
					{
						foundCFOLine = true;
						if(line.find("[x]") != std::string::npos)
							dtcLock.cfoCDRLocked = true;
						break;
					}
				}

				if(!foundCFOLine || !dtcLock.cfoCDRLocked)
					unlockedDTCs.push_back(dtcLock.uid);
			}
			catch(const std::exception& e)
			{
				__FE_COUT_WARN__ << "Failed to read lock status from DTC " << dtcLock.uid
				                 << ": " << e.what() << __E__;
				unlockedDTCs.push_back(dtcLock.uid + " (unreachable)");
				continue;
			}
		}

		std::vector<std::string> unreachableDTCs;
		std::vector<std::string> justUnlockedDTCs;
		for(const auto& uid : unlockedDTCs)
		{
			if(uid.find("(unreachable)") != std::string::npos)
				unreachableDTCs.push_back(uid);
			else
				justUnlockedDTCs.push_back(uid);
		}

		if(!unreachableDTCs.empty())
		{
			__FE_SS__ << "Phase 2b (Timing Chain: CDR Check) failed: "
			          << unreachableDTCs.size()
			          << " DTC(s) unreachable via FE Macro 'Get Link Lock Status':";
			for(const auto& uid : unreachableDTCs)
				ss << "\n  " << uid;
			__FE_SS_THROW__;
		}

		if(justUnlockedDTCs.empty())
		{
			__FE_COUT_INFO__ << "All DTCs have CFO CDR lock." << __E__;
		}
		else if(!isRetry)
		{
			__FE_COUT_WARN__ << justUnlockedDTCs.size()
			                 << " DTC(s) missing CFO CDR lock:";
			for(const auto& uid : justUnlockedDTCs)
				__FE_COUT__ << "  " << uid;
			__FE_COUT__ << "Performing SERDES resets and retrying..." << __E__;

			try
			{
				thisCFO_->CFOandDTC_Registers::ResetSERDES();
				thisCFO_->ResetSERDES(CFOLib::CFO_Link_ID::CFO_Link_ALL);
			}
			catch(const std::exception& e)
			{
				__FE_SS__ << "Phase 2b SERDES reset failed for "
				          << justUnlockedDTCs.size() << " DTC(s) missing CFO CDR lock:";
				for(const auto& uid : justUnlockedDTCs)
					ss << "\n  " << uid;
				ss << "\n\nSERDES reset error: " << e.what();
				__FE_SS_THROW__;
			}
			indicateSubIterationWork();
		}
		else
		{
			__FE_SS__ << "Phase 2b (Timing Chain: CDR Check) failed: "
			          << justUnlockedDTCs.size()
			          << " DTC(s) still missing CFO CDR lock after retry:";
			for(const auto& uid : justUnlockedDTCs)
				ss << "\n  " << uid;
			__FE_SS_THROW__;
		}

		if(!VStateMachine::getSubIterationWork())
		{
			timing_chain_first_substep_ = -1;
			indicateIterationWork();
		}
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_CFO_EDGE_FIX)
	{
		bool doEdgeFix =
		    (operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING ||
		     operatingMode_ ==
		         CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING_AND_SYNC);

		if(!doEdgeFix)
		{
			__FE_COUT__ << "CFO edge fix phase idle." << __E__;
			indicateIterationWork();
		}
		else
		{
			if(timing_chain_first_substep_ == -1)
				timing_chain_first_substep_ = getSubIterationIndex();

			int subStep = getSubIterationIndex() - timing_chain_first_substep_;

			// enumerate all DTCs in this subsystem from the config tree
			std::vector<std::string> dtcUIDs =
			    getSubsystemFEUIDsOfPlugin("DTCFrontEndInterface");

			if(subStep == 0)
			{
				// Sub-step 0: RTF validation on all DTCs
				__FE_COUT_INFO__
				    << "Phase 2c — checking all DTCs via Get RTF Interface Status..."
				    << __E__;
				cfo_edge_fix_consecutive_clean_ = 0;

				__FE_COUT__ << "Checking " << dtcUIDs.size()
				            << " DTC(s) for RTF Interface Status..." << __E__;

				for(const auto& dtcUID : dtcUIDs)
				{
					std::vector<FEVInterface::frontEndMacroArg_t> argsIn, argsOut;
					try
					{
						runFrontEndMacro(
						    dtcUID, "Get RTF Interface Status", argsIn, argsOut);
					}
					catch(const std::exception& e)
					{
						__FE_SS__ << "Phase 2c (CFO Edge Fix): Failed to read "
						             "RTF Interface Status from DTC "
						          << dtcUID << ": " << e.what();
						__FE_SS_THROW__;
					}

					std::string status =
					    __GET_ARG_OUT__("RTF Interface Status", std::string);
					__FE_COUT__ << "DTC " << dtcUID << " RTF status: " << status << __E__;

					struct Check
					{
						std::string keyword;
						std::string required;
						std::string label;
					};
					std::vector<Check> checks = {
					    {"CFO Emulation Mode", "OFF", "CFO Emulation Mode must be OFF"},
					    {"JA Source", "RJ45", "JA Source must be RJ45"},
					    {"Saturated", "YES", "RTF histogram must be Saturated"},
					    {"CFO CDR Lock", "LOCKED", "CFO CDR Lock must be LOCKED"},
					};

					std::istringstream iss(status);
					std::string        line;
					for(auto& chk : checks)
					{
						bool found = false;
						iss.clear();
						iss.str(status);
						while(std::getline(iss, line))
						{
							if(line.find(chk.keyword) != std::string::npos)
							{
								found = true;
								if(line.find(chk.required) == std::string::npos)
								{
									__FE_SS__ << "Phase 2c (CFO Edge Fix): DTC " << dtcUID
									          << " failed check: " << chk.label
									          << ". Line: " << line;
									__FE_SS_THROW__;
								}
								break;
							}
						}
						if(!found)
						{
							__FE_SS__ << "Phase 2c (CFO Edge Fix): DTC " << dtcUID
							          << " — could not find '" << chk.keyword
							          << "' in RTF Interface Status output.";
							__FE_SS_THROW__;
						}
					}
					__FE_COUT__ << "DTC " << dtcUID << " passed all RTF checks." << __E__;
				}

				__FE_COUT_INFO__
				    << "All DTCs passed Phase 2c RTF Interface Status checks." << __E__;
				indicateSubIterationWork();
			}
			else
			{
				// Sub-steps 1+: iterative edge fix passes
				const int MAX_CFO_EDGE_FIX_PASSES = 10;
				int       pass                    = subStep;

				__FE_COUT_INFO__ << "Phase 2c — CFO edge fix pass " << pass << " of "
				                 << MAX_CFO_EDGE_FIX_PASSES << "..." << __E__;

				std::vector<std::string> toggledDTCs;

				for(const auto& dtcUID : dtcUIDs)
				{
					std::vector<FEVInterface::frontEndMacroArg_t> argsIn, argsOut;
					try
					{
						runFrontEndMacro(dtcUID, "Fix CFO Clock Edge", argsIn, argsOut);
					}
					catch(const std::exception& e)
					{
						__FE_SS__ << "Phase 2c (CFO Edge Fix): Fix CFO Clock Edge "
						             "failed on DTC "
						          << dtcUID << ": " << e.what();
						__FE_SS_THROW__;
					}

					std::string result =
					    __GET_ARG_OUT__("Fix CFO Clock Edge Result", std::string);
					__FE_COUT__ << "DTC " << dtcUID << ": " << result << __E__;

					if(result.find("toggled") != std::string::npos)
						toggledDTCs.push_back(dtcUID);
				}

				if(toggledDTCs.empty())
				{
					++cfo_edge_fix_consecutive_clean_;
					if(cfo_edge_fix_consecutive_clean_ >= 2)
					{
						__FE_COUT_INFO__
						    << "Phase 2c — CFO edge fix converged after " << pass
						    << " pass(es) (2 consecutive clean passes)." << __E__;
						timing_chain_first_substep_ = -1;
						indicateIterationWork();
					}
					else
					{
						__FE_COUT__ << "Phase 2c — pass " << pass
						            << " clean, running confirmation pass..." << __E__;
						indicateSubIterationWork();
					}
				}
				else
				{
					cfo_edge_fix_consecutive_clean_ = 0;

					__FE_COUT__ << "Phase 2c — pass " << pass << " toggled "
					            << toggledDTCs.size() << " DTC(s):";
					for(const auto& uid : toggledDTCs)
						__FE_COUT__ << "  " << uid;
					__FE_COUT__ << __E__;

					if(pass >= MAX_CFO_EDGE_FIX_PASSES)
					{
						__FE_SS__ << "Phase 2c (CFO Edge Fix): edge fix did not converge "
						             "after "
						          << MAX_CFO_EDGE_FIX_PASSES
						          << " passes. DTCs still toggling:";
						for(const auto& uid : toggledDTCs)
							ss << "\n  " << uid;
						__FE_SS_THROW__;
					}
					indicateSubIterationWork();
				}
			}
		}
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_SYNC_E)
	{
		bool doEdgeFix =
		    (operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING ||
		     operatingMode_ ==
		         CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING_AND_SYNC);
		if(!doEdgeFix)
		{
			__FE_COUT__ << "Stop-events phase idle." << __E__;
		}
		else
		{
			thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
			thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
			__FE_COUT__ << "Stopped CFO event generation after edge fix phases." << __E__;
		}
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_SYNC_A ||
	        step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_SYNC_B)
	{
		__FE_COUT__ << "Idle while DTCs perform edge fix..." << __E__;
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_SYNC_C ||
	        step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_SYNC_D)
	{
		bool doSync = (operatingMode_ ==
		               CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING_AND_SYNC);

		if(!doSync)
		{
			__FE_COUT__ << "RTF offset phase idle (no 40 MHz sync)." << __E__;
		}
		else
		{
			__FE_COUT__ << "Idle while DTCs apply RTF marker offset..." << __E__;
		}
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_ESTABLISH_ROC_CONFIG)
	{
		__FE_COUT__ << "Idle while DTCs configure ROC links and DCS..." << __E__;
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_ROC_DATA_PATH)
	{
		__FE_COUT__ << "Idle while DTCs set up ROC data path..." << __E__;
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_PHASE_FINAL_SOFT_RESET)
	{
		__FE_COUT__ << "Releasing DAQ DMA buffers, then Final SoftReset to clear errors "
		               "before enabling CFO operation."
		            << __E__;
		thisCFO_->ReleaseAllBuffers(DTC_DMA_Engine_DAQ);
		thisCFO_->SoftReset();
		indicateIterationWork();
	}
	else if(step == CFOandDTCCoreVInterface::CONFIG_CFO_EVENT_SENDING_START_ITERATION)
	{
		// Phase 6: Enable CFO Operation — last phase of the CFO pass
		__FE_COUT__ << "Enable CFO operation (RF0, punch)." << __E__;
		thisCFO_->EnableAcceleratorRF0();
		thisCFO_->SetPunchEnable();
		resetConfigPhase();
	}
	else
		__FE_COUT__ << "Do nothing while other configurable entities finish..." << __E__;

}  // end configureEventBuildingMode()

//==============================================================================
void CFOFrontEndInterface::configureLoopbackMode(int step)
{
	__FE_COUT__ << "The loopback is performed in the start transition." << __E__;
	// if(step == -1)
	// 	step = getIterationIndex();

	// __FE_COUT_INFO__ << "configureLoopbackMode() " << step << "." << getSubIterationIndex() << __E__;

	// if(step < CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX)
	// {
	// 	if(timing_chain_first_substep_ == -1)
	// 			timing_chain_first_substep_ = getSubIterationIndex();
	// 	configureForTimingChain();
	// 	indicateIterationWork();
	// }
	// else if(step < CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX +
	// 	CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_STEPS)
	// {
	// 	__FE_COUT__ << "Do nothing while DTCs finish configureForTimingChain..." << __E__;
	// 	indicateIterationWork();
	// }
	// else if(step == CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX +
	// 	CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_STEPS)
	// {
	// 	__FE_COUT__ << "CFO reset serdes TX " << __E__;
	// 	thisCFO_->ResetAllSERDESTx();
	// 	indicateIterationWork();
	// }
	// else if(step == 1 + CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_START_INDEX +
	// 	CFOandDTCCoreVInterface::CONFIG_DTC_TIMING_CHAIN_STEPS)
	// {
	// 	__FE_COUT__ << "Enable communication over links" << __E__;
	// 	thisCFO_->EnableTiming();
	// 	thisCFO_->EnableEventWindowInput();

	// 	thisCFO_->EnableLink(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	// 	__FE_COUT__ << "CFO set beam off Event Window interval time" << __E__;
	// 	thisCFO_->SetEventWindowEmulatorInterval(0x1f40 /* 40us */);

	// 	__FE_COUT__ << "CFO set 40MHz marker interval" << __E__;
	// 	thisCFO_->SetClockMarkerIntervalCount(0x0800);  // 0 = NO markers
	// }
	// else
	// 	__FE_COUT__ << "Do nothing while other configurable entities finish..." << __E__;

}  // end configureLoopbackMode()

//==============================================================================
// Phase 1 (Establish Clocks) for the CFO.
//	Sub-step 0: halt, disable beam modes, ClearControlRegister, DisableAllOutputs
//	Sub-step 1: JA setup — check lock, full reset if unlocked, mux-only if locked
//	Sub-steps 2+: JA lock polling (up to ~10 polls, 1s each)
void CFOFrontEndInterface::configureForTimingChain(int step)
{
	if(step == -1)
		step = getSubIterationIndex() - timing_chain_first_substep_;

	__FE_COUT_INFO__ << "configureForTimingChain() sub-step=" << step << __E__;

	switch(step)
	{
	case 0:
		next_starting_event_window_tag_ = 0;
		halt();
		thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
		thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
		thisCFO_->ClearControlRegister();
		thisCFO_->DisableAllOutputs();
		indicateSubIterationWork();
		break;

	case 1: {
		__FE_COUTV__(configure_clock_);
		if(configure_clock_)
		{
			uint32_t select = 0;
			try
			{
				select = getSelfNode()
				             .getNode("JitterAttenuatorInputSource")
				             .getValue<uint32_t>();
			}
			catch(...)
			{
				__FE_COUT__ << "Defaulting Jitter Attenuator Input Source to select = "
				            << select << __E__;
			}
			__FE_COUTV__(select);
			//For CFO - 0 ==> Local oscillator
			//For CFO - 1 ==> RTF copper clock

			bool jaLocked = getCFOandDTCRegisters()->ReadJitterAttenuatorLocked();
			__FE_COUT__ << "JA locked before setup: " << jaLocked << __E__;

			bool alsoResetJA = !jaLocked;
			__FE_COUT__ << "Setting JA select=" << select
			            << " alsoResetJA=" << alsoResetJA << __E__;
			getCFOandDTCRegisters()->SetJitterAttenuatorSelect(select, alsoResetJA);
			__FE_COUT__ << "JA CSR: "
			            << getCFOandDTCRegisters()->FormatJitterAttenuatorCSR() << __E__;

			indicateSubIterationWork();
		}
		else
			__FE_COUT_INFO__ << "Skipping configure clock." << __E__;
		break;
	}

	default: {
		if(!configure_clock_)
		{
			__FE_COUT__ << "Clock configuration not enabled, nothing to poll." << __E__;
			break;
		}

		const int pollIndex = step - 2;
		const int maxPolls  = 10;

		if(getCFOandDTCRegisters()->ReadJitterAttenuatorLocked())
		{
			__FE_COUT_INFO__ << "JA locked after " << pollIndex << " poll(s)." << __E__;
			__FE_COUT__ << "JA CSR: "
			            << getCFOandDTCRegisters()->FormatJitterAttenuatorCSR() << __E__;
			break;
		}

		if(pollIndex < maxPolls)
		{
			sleep(1);
			__FE_COUT__ << "JA not locked, poll " << (pollIndex + 1) << "/" << maxPolls
			            << __E__;
			indicateSubIterationWork();
		}
		else
		{
			__FE_COUT_WARN__ << "JA failed to lock after " << maxPolls
			                 << " polls! Continuing anyway." << __E__;
			__FE_COUT__ << "JA CSR: "
			            << getCFOandDTCRegisters()->FormatJitterAttenuatorCSR() << __E__;
		}
		break;
	}
	}  // end switch

}  // end configureForTimingChain()

//==============================================================================
void CFOFrontEndInterface::halt(void)
{
	//Note: some global operating modes may do nothing for halt (to not interfere with others when transitioning the FSM)!

	if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_HARDWARE_DEV)
	{
		__FE_COUT_INFO__ << "CFO halt for HW Dev mode." << __E__;
		return;
	}

	__FE_COUT__ << "HALT: CFO status" << __E__;

	resetConfigPhase();
	timing_chain_first_substep_ = -1;

	if(operatingMode_ != CFOandDTCCoreVInterface::CONFIG_MODE_LOOPBACK)
	{
		thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
		thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	}

	// readStatus();
}  //end halt()

//==============================================================================
void CFOFrontEndInterface::pause(void)
{
	__FE_COUT__ << "PAUSE: CFO status" << __E__;

	// readStatus();
}  // end pause()

//==============================================================================
void CFOFrontEndInterface::resume(void)
{
	__FE_COUT__ << "RESUME: CFO status" << __E__;

	// readStatus();
}  //end resume()

//==============================================================================
void CFOFrontEndInterface::start(std::string runNumber)  // runNumber)
{
	testAndUpdateTimeAlive("Start");
	testRTFClockInEventBuildingMode("Start");

	if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_HARDWARE_DEV)
	{
		__FE_COUT_INFO__ << "CFO start for HW Dev mode." << __E__;
		return;
	}

	__FE_COUTV__(getSubsystemIterationIndexString());
	__FE_COUTV__(getIterationIndex());
	__FE_COUTV__(getSubIterationIndex());

	if(isFirstIteration())
	{
		next_starting_event_window_tag_ = 0;  //reset next event window tag
		__FE_COUTV__(next_starting_event_window_tag_);
	}

	if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_LOOPBACK)
	{
		__FE_COUT_INFO__ << "Start the loopback!" << __E__;
		loopbackTest(runNumber);
		__FE_COUT_INFO__ << "End the loopback!" << __E__;
	}
	else if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING ||
	        operatingMode_ ==
	            CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING_AND_SYNC)
	{
		const unsigned int systemMinReady =
		    getSystemMinReadyForEventGenerationStartIteration();
		if(systemMinReady < 2)
		{
			__FE_SS__
			    << "SystemMinReadyForEventGenerationStartIteration must be >= 2 (got "
			    << systemMinReady << ")." << __E__;
			__FE_SS_THROW__;
		}

		// Start steps are ordered across subsystems (DTC SoftReset -> artdaq -> DTC final
		// SoftReset -> CFO run plan): subsystem-iterations under a top-level, plain
		// iterations when standalone.
		const unsigned int startIteration = getSubsystemSyncStepIndex();
		if(startIteration < systemMinReady)
		{
			__FE_COUT_INFO__ << "Delaying CFO run plan launch until start step >= "
			                 << systemMinReady << " (now " << startIteration << ")"
			                 << __E__;
			indicateSubsystemSyncStepWork();
			return;
		}

		if(startIteration == systemMinReady)
		{
			bool autoFixedWidthRunPlanEnable = false;
			try
			{
				autoFixedWidthRunPlanEnable =
				    getSelfNode().getNode("AutoFixedWidthRunPlanEnable").getValue<bool>();
			}
			catch(...)
			{
				__FE_COUT_WARN__
				    << "Missing CFOInterfaceTable AutoFixedWidthRunPlanEnable. "
				       "Skipping startup run plan launch."
				    << __E__;
				return;
			}

			if(!autoFixedWidthRunPlanEnable)
			{
				__FE_COUT_INFO__
				    << "AutoFixedWidthRunPlanEnable is disabled. Skipping startup run "
				       "plan launch."
				    << __E__;
				return;
			}

			const uint32_t numberOfEventWindowMarkers = 0;  // 0 means infinite windows

			std::string eventDuration = "100us";
			try
			{
				eventDuration = getSelfNode()
				                    .getNode("AutoFixedWidthRunPlanEventDuration")
				                    .getValueWithDefault("100us");
			}
			catch(...)
			{
				__FE_COUT_WARN__
				    << "Optional CFOInterfaceTable AutoFixedWidthRunPlanEventDuration "
				       "was not found; defaulting to 100us."
				    << __E__;
			}

			__FE_COUT_INFO__ << "Launching startup fixed-width CFO run plan in infinite "
			                    "mode (count=0) "
			                 << "at duration " << eventDuration << __E__;

			CompileSetAndLaunchTemplateFixedWidthRunPlan(true,
			                                             false,
			                                             eventDuration,
			                                             numberOfEventWindowMarkers,
			                                             next_starting_event_window_tag_,
			                                             1,
			                                             false,
			                                             false,
			                                             false,
			                                             false);

			next_starting_event_window_tag_ += numberOfEventWindowMarkers;
		}
	}

	/* COMMENTED 20-Jun-2023 by rrivera to start using CFO_Register directly.. will need to add features to support loopback revival

	//bool LoopbackLock = true;
	//int  loopbackROC  = 0;

	const int numberOfChains       = 1;
	int       link[numberOfChains] = {0};

	const int numberOfDTCsPerChain = 1;  // assume 0, then 1

	const int numberOfROCsPerDTC = 1;  // assume 0, then 1

	// To do loopbacks on all CFOs, first have to setup all DTCs, then the CFO
	// (this method) work per iteration. Loop back done on all chains (in this
	// method), assuming the following order: i DTC0 DTC1 ... DTCN 0 ROC0 none ...
	// none 1 ROC1 none ... none 2 none ROC0 ... none 3 none ROC1 ... none
	// ...
	// N-1 none none ... ROC0
	// N none none ... ROC1

	int numberOfMeasurements = numberOfChains * numberOfDTCsPerChain * numberOfROCsPerDTC;

	int startIndex = getIterationIndex();

	if(startIndex == 0)  // setup
	{
		initial_9100_ = registerRead(0x9100);
		initial_9114_ = registerRead(0x9114);
		initial_91a0_ = registerRead(0x91a0);
		initial_9154_ = registerRead(0x9154);

		__FE_COUT__ << "CFO disable Event Start character output " << __E__;
		registerWrite(0x9100, 0x0);

		__FE_COUT__ << "CFO turn off Event Windows" << __E__;
		registerWrite(0x91a0, 0x00000000);

		__FE_COUT__ << "CFO turn off 40MHz marker interval" << __E__;
		registerWrite(0x9154, 0x00000000);

		__FE_COUT__ << "START: CFO status" << __E__;
		readStatus();

		for(int nChain = 0; nChain < numberOfChains; nChain++)
		{
			for(int nDTC = 0; nDTC < numberOfDTCsPerChain; nDTC++)
			{
				for(int nROC = 0; nROC < numberOfROCsPerDTC; nROC++)
				{
					delay[nChain][nDTC][nROC]        = -1;
					delay_rms[nChain][nDTC][nROC]    = -1;
					delay_failed[nChain][nDTC][nROC] = -1;
				}
			}
		}

		indicateIterationWork();  // I still need to be touched
		return;
	}

	if(startIndex > numberOfMeasurements)  // finish
	{
		__FE_COUT_INFO__ << "-------------------------" << __E__;
		__FE_COUT_INFO__ << "FULL SYSTEM loopback DONE" << __E__;

		for(int nChain = 0; nChain < numberOfChains; nChain++)
		{
			for(int nDTC = 0; nDTC < numberOfDTCsPerChain; nDTC++)
			{
				for(int nROC = 0; nROC < numberOfROCsPerDTC; nROC++)
				{
					__FE_COUT_INFO__ << "chain "
								   << nChain << " - DTC " << nDTC << " - ROC " << nROC
								   << " = " << std::dec << delay[nChain][nDTC][nROC]
								   << " ns +/- " << delay_rms[nChain][nDTC][nROC] << " ("
								   << delay_failed[nChain][nDTC][nROC] << ")" << __E__;
				}
			}
		}

		float diff = delay[0][1][0] - delay[0][0][0];

		__FE_COUT_INFO__ << "DTC1_ROC0 - DTC0_ROC0 = " << diff << __E__;
		__FE_COUT_INFO__ << "-------------------------" << __E__;

		__FE_COUT__ << "LOOPBACK: CFO reset serdes RX " << __E__;
		registerWrite(0x9118, 0x000000ff);
		registerWrite(0x9118, 0x0);
		usleep(50);

		__FE_COUT__ << "CFO enable Event Start character output 0x" << std::hex << __E__;
		registerWrite(0x9100, initial_9100_);

		__FE_COUT__ << "CFO enable serdes transmit and receive 0x" << __E__;
		registerWrite(0x9114, initial_9114_);

		__FE_COUT__ << "CFO set Event Window interval time" << __E__;
		registerWrite(0x91a0, initial_91a0_);  // 40us

		__FE_COUT__ << "CFO set 40MHz marker interval" << __E__;
		registerWrite(0x9154, initial_9154_);

		readStatus();
		return;
	}

	//=========== Perform loopback=============

	// where are we in the procedure?
	int activeROC = (startIndex - 1) % numberOfROCsPerDTC;

	int activeDTC = -1;

	for(int nDTC = 0; nDTC < numberOfDTCsPerChain; nDTC++)
	{
		//	__FE_COUT__ << "loopback index = " << startIndex
		//		<< " nDTC = " << nDTC
		//		<< " numberOfDTCsPerChain = " << numberOfDTCsPerChain
		//		<< __E__;
		if((startIndex - 1) >= (nDTC * numberOfROCsPerDTC) &&
		   (startIndex - 1) < ((nDTC + 1) * numberOfROCsPerDTC))
		{
			//				__FE_COUT__ << "ACTIVE DTC " << nDTC <<
			//__E__;
			activeDTC = nDTC;
		}
	}

	//	__COUT__ 	<< "loopback index = " << startIndex;
	__FE_COUT_INFO__ << " Looping back DTC" << activeDTC << " ROC" << activeROC << __E__;

	int chainIndex = 0;

	while((chainIndex < numberOfChains))
	{
		//__COUT__ << "LOOPBACK: on DTC " << link[chainIndex] <<__E__;
		MeasureLoopback(link[chainIndex]);

		delay[chainIndex][activeDTC][activeROC]        = average_loopback_;
		delay_rms[chainIndex][activeDTC][activeROC]    = rms_loopback_;
		delay_failed[chainIndex][activeDTC][activeROC] = failed_loopback_;

		__FE_COUT__ << "LOOPBACK: link " << link[chainIndex]
					<< " -> delay = " << delay[chainIndex][activeDTC][activeROC]
					<< " ns,  rms = " << delay_rms[chainIndex][activeDTC][activeROC]
					<< " failed = " << delay_failed[chainIndex][activeDTC][activeROC]
					<< __E__;

		chainIndex++;

	}  // (chainIndex < numberOfChains)

	indicateIterationWork();  // I still need to be touched
	return;
*/
}  //end start()

//==============================================================================
void CFOFrontEndInterface::stop(void)
{
	testAndUpdateTimeAlive("Stop");
	testRTFClockInEventBuildingMode("Stop");

	if(operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_HARDWARE_DEV)
	{
		__FE_COUT_INFO__ << "CFO stop for HW Dev mode." << __E__;
		return;
	}

	// Stop the run plan if AutoFixedWidthRunPlan was enabled
	try
	{
		if(getSelfNode().getNode("AutoFixedWidthRunPlanEnable").getValue<bool>())
		{
			__FE_COUT_INFO__ << "Disabling CFO run plan." << __E__;
			CompileSetAndLaunchTemplateFixedWidthRunPlan(
			    false, false, "100us", 0, 0, 1, false, false, false, false);
		}
	}
	catch(...)
	{
	}

	// Apply explicit CFO halt behavior on stop transition.
	halt();
	thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	int numberOfCAPTANPulses =
	    getConfigurationManager()
	        ->getNode("/Mu2eGlobalsTable/SyncDemoConfig/NumberOfCAPTANPulses")
	        .getValue<unsigned int>();

	__FE_COUTV__(numberOfCAPTANPulses);

	if(numberOfCAPTANPulses == 0)
	{
		return;
	}

	int loopbackIndex = getIterationIndex();

	if(loopbackIndex > numberOfCAPTANPulses)
	{
		//---- begin read in data

		std::string filein1 = "/home/mu2edaq/sync_demo/ots/DTC0_ROC0data.txt";
		std::string filein2 = "/home/mu2edaq/sync_demo/ots/DTC1_ROC0data.txt";

		// file 1
		std::ifstream in1;

		int iteration_source1[10000];
		int timestamp_source1[10000];

		in1.open(filein1);

		//  std::cout << filein1 << std::endl;

		int nlines1 = 0;
		while(1)
		{
			in1 >> iteration_source1[nlines1] >> timestamp_source1[nlines1];
			if(!in1.good())
				break;
			if(nlines1 < 10)
				__FE_COUT__ << "iteration " << iteration_source1[nlines1] << " "
				            << timestamp_source1[nlines1] << __E__;
			nlines1++;
		}

		in1.close();

		// file 2
		std::ifstream in2;

		int iteration_source2[10000];
		int timestamp_source2[10000];

		in2.open(filein2);

		//  std::cout << filein1 << std::endl;

		int nlines2 = 0;
		while(1)
		{
			in2 >> iteration_source2[nlines2] >> timestamp_source2[nlines2];
			if(!in2.good())
				break;
			if(nlines2 < 10)
				__FE_COUT__ << "iteration " << iteration_source2[nlines2] << " "
				            << timestamp_source2[nlines2] << __E__;
			nlines2++;
		}

		in2.close();

		__FE_COUT__ << "Read in " << nlines1 << " lines from " << filein1 << __E__;
		__FE_COUT__ << "Read in " << nlines2 << " lines from " << filein2 << __E__;

		__FE_COUT__ << __E__;

		//__FE_COUT_INFO__ << "iter file1  file2  diff" << __E__;

		int distribution[1001] = {};

		int max_distribution = -1000;
		int min_distribution = 1000;

		int offset = 500;

		int timestamp_diff[1000] = {};

		float numerator   = 0.;
		float denominator = 0.;

		for(int i = 0; i < nlines1; i++)
		{
			if(timestamp_source1[i] == 65535 || timestamp_source2[i] == 65535 ||
			   timestamp_source1[i] == -999 || timestamp_source2[i] == -999)
			{
				timestamp_diff[i] = -999999;
			}
			else
			{
				timestamp_diff[i] =
				    (timestamp_source2[i] - timestamp_source1[i]) + offset;

				if(timestamp_diff[i] >= 0 &&
				   timestamp_diff[i] < 1000)  // crossed from one event window to another
				{
					numerator += (float)timestamp_diff[i];
					denominator += 1.0;

					distribution[timestamp_diff[i]]++;

					if(timestamp_diff[i] > max_distribution)
					{
						max_distribution = timestamp_diff[i];
						__FE_COUT__ << i << " new max    " << timestamp_source1[i]
						            << "   " << timestamp_source2[i] << "   "
						            << timestamp_diff[i] << __E__;
					}

					if(timestamp_diff[i] < min_distribution)
					{
						__FE_COUT__ << i << " new min    " << timestamp_source1[i]
						            << "   " << timestamp_source2[i] << "   "
						            << timestamp_diff[i] << __E__;

						min_distribution = timestamp_diff[i];
					}
				}
				else
				{
					timestamp_diff[i] = -999999;
				}
			}
		}
		float average = numerator / denominator;

		float rms = 0.;

		for(int n = 0; n < nlines1; n++)
		{
			if(timestamp_diff[n] != -999999)
			{
				rms += (timestamp_diff[n] - average) * (timestamp_diff[n] - average);
			}
		}

		if(denominator > 0.0)
			rms = sqrt(rms / denominator);

		average -= offset;

		//    __FE_COUT__ << "LOOPBACK: min_distribution_: " << min_distribution_ <<
		//    __E__;
		//    __FE_COUT__ << "LOOPBACK: max_distribution_: " << max_distribution_ <<
		//    __E__;

		__FE_COUT_INFO__ << "--------------------------------------------" << __E__;
		__FE_COUT_INFO__ << "--CAPTAN timestamp difference distribution--" << __E__;
		for(int n = (min_distribution - 5); n < (max_distribution + 5); n++)
		{
			int display = n - offset;
			__FE_COUT_INFO__ << " diff [ " << display << " ] = " << distribution[n]
			                 << __E__;
		}
		__FE_COUT_INFO__ << "--------------------------------------------" << __E__;

		__FE_COUT_INFO__ << "Average = " << average << " ... RMS = " << rms << __E__;

		return;
	}

	indicateIterationWork();
	return;
}  //end stop()

//==============================================================================
unsigned int CFOFrontEndInterface::getMinReadyForEventGenerationStartIteration(void) const
{
	return 2;
}  // end getMinReadyForEventGenerationStartIteration()

//==============================================================================
bool CFOFrontEndInterface::running(void)
{
	while(WorkLoop::continueWorkLoop_)
	{
		testAndUpdateTimeAlive("Running");
		testRTFClockInEventBuildingMode("Running");

		if(!theSuperParameters_.go)
		{
			__FE_COUT__ << "Not running the Super Orchestration loop!" << __E__;
			sleep(3);
			continue;
		}

		if(next_starting_event_window_tag_ == 0 &&
		   (operatingMode_ == CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING ||
		    operatingMode_ ==
		        CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING_AND_SYNC))
		{
			__FE_COUT_INFO__ << "Sleeping for the Super Orchestration..." << __E__;
			sleep(5);
			__FE_COUT_INFO__ << "Start the Super Orchestration!" << __E__;
			SuperOrchestration(true, true, true);
			__FE_COUT_INFO__ << "End the Super Orchestration!" << __E__;
		}
	}
	__FE_COUT_INFO__ << "End running." << __E__;
	return false;
}  //end running()

//========================================================================
void CFOFrontEndInterface::WriteCFO(__ARGS__)
{
	dtc_address_t address   = __GET_ARG_IN__("address", dtc_address_t);
	dtc_data_t    writeData = __GET_ARG_IN__("writeData", dtc_data_t);
	__FE_COUTV__((unsigned int)address);
	__FE_COUTV__((unsigned int)writeData);

	int errorCode = getDevice()->write_register(address, 100, writeData);
	if(errorCode != 0)
	{
		__FE_SS__ << "Error writing register 0x" << std::hex
		          << static_cast<uint32_t>(address) << " " << errorCode;
		__SS_THROW__;
	}

}  //end WriteCFO()

//========================================================================
void CFOFrontEndInterface::ReadCFO(__ARGS__)
{
	dtc_address_t address = __GET_ARG_IN__("address", dtc_address_t);
	__FE_COUTV__((unsigned int)address);
	dtc_data_t readData;

	int errorCode = getDevice()->read_register(address, 100, &readData);
	if(errorCode != 0)
	{
		__FE_SS__ << "Error reading register 0x" << std::hex
		          << static_cast<uint32_t>(address) << " " << errorCode;
		__SS_THROW__;
	}

	std::stringstream ss;
	ss << "Read " << std::dec << readData << " 0x" << std::hex << std::setfill('0')
	   << std::setw(8) << readData << " from address 0x" << std::setw(4) << address
	   << ".";
	__SET_ARG_OUT__("readData", ss.str());
}  //end ReadCFO()

//========================================================================
///makes it START
void CFOFrontEndInterface::SuperOrchestrationStart(__ARGS__)
{
	theSuperParameters_.numberOfEventWindows =
	    __GET_ARG_IN__("Number of Event Window Markers (Default: 10)", uint64_t, 10);
	theSuperParameters_.go = true;

	__FE_COUTV__(theSuperParameters_.numberOfEventWindows);
	__FE_COUTV__(theSuperParameters_.go);
}  //end SuperOrchestrationStart()

//========================================================================
///makes it END
void CFOFrontEndInterface::SuperOrchestrationEnd(__ARGS__)
{
	theSuperParameters_.go = false;
	__FE_COUTV__(theSuperParameters_.go);
}  //end SuperOrchestrationEnd()

//========================================================================
void CFOFrontEndInterface::SuperOrchestration(__ARGS__)
{
	__FE_COUT__ << "Super Orchestration" << __E__;

	bool doCRVReset   = __GET_ARG_IN__("Do CRV ROC Reset", bool);
	bool doCaloReset  = __GET_ARG_IN__("Do Calo ROC Reset", bool);
	bool doCaloWrites = __GET_ARG_IN__("Do Calo ROC Writes", bool);
	__FE_COUTV__(doCRVReset);
	__FE_COUTV__(doCaloReset);
	__FE_COUTV__(doCaloWrites);

	SuperOrchestration(doCRVReset, doCaloReset, doCaloWrites);
}  //end SuperOrchestration()

//========================================================================
void CFOFrontEndInterface::SuperOrchestration(bool doCRVReset,
                                              bool doCaloReset,
                                              bool doCaloWrites)
{
	// acquire enabled DTCs by priority
	ConfigurationTree dtcTable =
	    Configurable::getConfigurationManager()->getNode("DTCInterfaceTable");
	std::vector<std::string> dtcs =
	    dtcTable.getChildrenNames(true /*byPriority*/, true /*onlyStatusTrue*/);

	__CFG_COUTV__(StringMacros::vectorToString(dtcs));
	for(const auto& dtc : dtcs)
	{
		std::vector<std::pair<std::string, ConfigurationTree>> rocChildren =
		    dtcTable.getNode(dtc).getNode("LinkToROCGroupTable").getChildren();

		// for each ROC
		for(auto& roc : rocChildren)
			if(roc.second.isEnabled())
			{
				std::string rocType =
				    roc.second.getNode("ROCInterfacePluginName").getValue<std::string>();
				__FE_COUT__ << "ROC Name: " << dtc << "/" << roc.first << ":" << rocType
				            << __E__;
			}
	}  //end DTC example loop

	// ROC FEMacro - Soft Reset
	if(doCRVReset)
	{
		std::vector<frontEndMacroArg_t> argsOut;
		std::vector<frontEndMacroArg_t> argsIn;
		__SET_ARG_IN__("Target ROC (Default = -1 := all ROCs)", (unsigned int)0);

		__FE_COUTV__(StringMacros::vectorToString(argsIn));
		runFrontEndMacro(
		    "DAQ07DTC1",                 //const std::string& targetInterfaceID,
		    "ROC FEMacro - Soft Reset",  //const std::string& feMacroName,
		    argsIn,    //const std::vector<FEVInterface::frontEndMacroArg_t>& inputArgs,
		    argsOut);  //std::vector<FEVInterface::frontEndMacroArg_t>& outputArgs) const;

		__FE_COUTV__(StringMacros::vectorToString(argsOut));
	}

	// ROC FEMacro - Setup for ADCs Data Taking
	if(doCaloReset)
	{
		std::vector<frontEndMacroArg_t> argsOut;
		std::vector<frontEndMacroArg_t> argsIn;
		__SET_ARG_IN__("Target ROC (Default = -1 := all ROCs)", (unsigned int)-1);
		__SET_ARG_IN__("Set Threshold? [bool, Default := 0]", (unsigned int)1);
		__SET_ARG_IN__("Threshold [units of adccounts, Default := 2300]",
		               (unsigned int)2250);

		__FE_COUTV__(StringMacros::vectorToString(argsIn));
		runFrontEndMacro(
		    "DAQ07DTC0",  //const std::string& targetInterfaceID,
		    "ROC FEMacro - Setup for ADCs Data Taking",  //const std::string& feMacroName,
		    argsIn,    //const std::vector<FEVInterface::frontEndMacroArg_t>& inputArgs,
		    argsOut);  //std::vector<FEVInterface::frontEndMacroArg_t>& outputArgs) const;

		__FE_COUTV__(StringMacros::vectorToString(argsOut));
	}

	// ROC FEMacro - Setup for ADCs Data Taking
	if(doCaloReset)
	{
		std::vector<frontEndMacroArg_t> argsOut;
		std::vector<frontEndMacroArg_t> argsIn;
		__SET_ARG_IN__("Target ROC (Default = -1 := all ROCs)", (unsigned int)-1);
		__SET_ARG_IN__("Set Threshold? [bool, Default := 0]", (unsigned int)1);
		__SET_ARG_IN__("Threshold [units of adccounts, Default := 2300]",
		               (unsigned int)2250);

		__FE_COUTV__(StringMacros::vectorToString(argsIn));
		runFrontEndMacro(
		    "DAQ14DTC0",  //const std::string& targetInterfaceID,
		    "ROC FEMacro - Setup for ADCs Data Taking",  //const std::string& feMacroName,
		    argsIn,    //const std::vector<FEVInterface::frontEndMacroArg_t>& inputArgs,
		    argsOut);  //std::vector<FEVInterface::frontEndMacroArg_t>& outputArgs) const;

		__FE_COUTV__(StringMacros::vectorToString(argsOut));
	}

	// ROC Write
	if(doCaloWrites)
	{
		std::vector<frontEndMacroArg_t> argsOut;
		std::vector<frontEndMacroArg_t> argsIn;
		__SET_ARG_IN__("rocLinkIndex", (unsigned int)0);
		__SET_ARG_IN__("address", (unsigned int)123);
		__SET_ARG_IN__("writeData", (unsigned int)6000);

		__FE_COUTV__(StringMacros::vectorToString(argsIn));
		runFrontEndMacro(
		    "DAQ14DTC0",  //const std::string& targetInterfaceID,
		    "ROC Write",  //const std::string& feMacroName,
		    argsIn,    //const std::vector<FEVInterface::frontEndMacroArg_t>& inputArgs,
		    argsOut);  //std::vector<FEVInterface::frontEndMacroArg_t>& outputArgs) const;

		__FE_COUTV__(StringMacros::vectorToString(argsOut));
	}

	// ROC Write
	if(doCaloWrites)
	{
		std::vector<frontEndMacroArg_t> argsOut;
		std::vector<frontEndMacroArg_t> argsIn;
		__SET_ARG_IN__("rocLinkIndex", (unsigned int)1);
		__SET_ARG_IN__("address", (unsigned int)123);
		__SET_ARG_IN__("writeData", (unsigned int)6000);

		__FE_COUTV__(StringMacros::vectorToString(argsIn));
		runFrontEndMacro(
		    "DAQ14DTC0",  //const std::string& targetInterfaceID,
		    "ROC Write",  //const std::string& feMacroName,
		    argsIn,    //const std::vector<FEVInterface::frontEndMacroArg_t>& inputArgs,
		    argsOut);  //std::vector<FEVInterface::frontEndMacroArg_t>& outputArgs) const;

		__FE_COUTV__(StringMacros::vectorToString(argsOut));
	}

	// ROC Write
	if(doCaloWrites)
	{
		std::vector<frontEndMacroArg_t> argsOut;
		std::vector<frontEndMacroArg_t> argsIn;
		__SET_ARG_IN__("rocLinkIndex", (unsigned int)2);
		__SET_ARG_IN__("address", (unsigned int)123);
		__SET_ARG_IN__("writeData", (unsigned int)6000);

		__FE_COUTV__(StringMacros::vectorToString(argsIn));
		runFrontEndMacro(
		    "DAQ07DTC0",  //const std::string& targetInterfaceID,
		    "ROC Write",  //const std::string& feMacroName,
		    argsIn,    //const std::vector<FEVInterface::frontEndMacroArg_t>& inputArgs,
		    argsOut);  //std::vector<FEVInterface::frontEndMacroArg_t>& outputArgs) const;

		__FE_COUTV__(StringMacros::vectorToString(argsOut));
	}

	// ROC Write
	if(doCaloWrites)
	{
		std::vector<frontEndMacroArg_t> argsOut;
		std::vector<frontEndMacroArg_t> argsIn;
		__SET_ARG_IN__("rocLinkIndex", (unsigned int)3);
		__SET_ARG_IN__("address", (unsigned int)123);
		__SET_ARG_IN__("writeData", (unsigned int)6000);

		__FE_COUTV__(StringMacros::vectorToString(argsIn));
		runFrontEndMacro(
		    "DAQ07DTC0",  //const std::string& targetInterfaceID,
		    "ROC Write",  //const std::string& feMacroName,
		    argsIn,    //const std::vector<FEVInterface::frontEndMacroArg_t>& inputArgs,
		    argsOut);  //std::vector<FEVInterface::frontEndMacroArg_t>& outputArgs) const;

		__FE_COUTV__(StringMacros::vectorToString(argsOut));
	}

	// ROC Write
	if(doCaloWrites)
	{
		std::vector<frontEndMacroArg_t> argsOut;
		std::vector<frontEndMacroArg_t> argsIn;
		__SET_ARG_IN__("rocLinkIndex", (unsigned int)3);
		__SET_ARG_IN__("address", (unsigned int)100);
		__SET_ARG_IN__("writeData", (unsigned int)1268);

		__FE_COUTV__(StringMacros::vectorToString(argsIn));
		runFrontEndMacro(
		    "DAQ07DTC0",  //const std::string& targetInterfaceID,
		    "ROC Write",  //const std::string& feMacroName,
		    argsIn,    //const std::vector<FEVInterface::frontEndMacroArg_t>& inputArgs,
		    argsOut);  //std::vector<FEVInterface::frontEndMacroArg_t>& outputArgs) const;

		__FE_COUTV__(StringMacros::vectorToString(argsOut));
	}

	// ROC Write
	if(doCaloWrites)
	{
		std::vector<frontEndMacroArg_t> argsOut;
		std::vector<frontEndMacroArg_t> argsIn;
		__SET_ARG_IN__("rocLinkIndex", (unsigned int)1);
		__SET_ARG_IN__("address", (unsigned int)100);
		__SET_ARG_IN__("writeData", (unsigned int)1233);

		__FE_COUTV__(StringMacros::vectorToString(argsIn));
		runFrontEndMacro(
		    "DAQ14DTC0",  //const std::string& targetInterfaceID,
		    "ROC Write",  //const std::string& feMacroName,
		    argsIn,    //const std::vector<FEVInterface::frontEndMacroArg_t>& inputArgs,
		    argsOut);  //std::vector<FEVInterface::frontEndMacroArg_t>& outputArgs) const;

		__FE_COUTV__(StringMacros::vectorToString(argsOut));
	}

	CompileSetAndLaunchTemplateFixedWidthRunPlan(
	    1,  //__GET_ARG_IN__("Enable CFO Run Plan Execution (Default := false)",bool,false),
	    0,        //__GET_ARG_IN__("Use Detached Buffer Test (Default := false)",bool),
	    "100us",  //__GET_ARG_IN__("Fixed-width Event Window Duration (s, ms, us, ns, and clocks allowed) [clocks := 25ns]",std::string),
	    theSuperParameters_.numberOfEventWindows,  //numberOfEvents,
	    next_starting_event_window_tag_,           //startTag,
	    1,  //__GET_ARG_IN__("Event Window Mode (Default := 1)", uint64_t, 1),
	    1,  //__GET_ARG_IN__("Enable Clock Markers (Default := true)",bool,true),
	    0,  //__GET_ARG_IN__("For Detached Buffer Test, Save Binary Data to File (Default: false)", bool),
	    0,  //__GET_ARG_IN__("For Detached Buffer Test, Save Subevent Header to Binary File (Default: false)", bool),
	    0  //__GET_ARG_IN__("For Detached Buffer Test, Do NOT Reset Counters (Default: false)", bool)
	);
	next_starting_event_window_tag_ += theSuperParameters_.numberOfEventWindows;
}  //end SuperOrchestration()

//========================================================================
void CFOFrontEndInterface::ResetRunplan(__ARGS__)
{
	__FE_COUT__ << "Reset CFO Run Plan" << __E__;

	halt();
	//halt may disable run plan, but let's make sure:
	thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	// thisCFO_->ResetCFORunPlan();
	thisCFO_->SoftReset();

}  //end ResetRunplan()

//========================================================================
void CFOFrontEndInterface::CompileRunplan(__ARGS__)
{
	// to view output file with 8-byte rows
	// hexdump -v -e '"%08_ax " 1/8 "%016x "' -e '"\n"' srcs/mu2e-pcie-utils/cfoInterfaceLib/Commands.bin

	__FE_COUT__ << "Compile CFO Run Plan" << __E__;

	const std::string SOURCE_BASE_PATH =
	    std::string(__ENV__("OTS_SOURCE")) + "/mu2e-pcie-utils/cfoInterfaceLib/";

	std::string inFileName =
	    __GET_ARG_IN__("Input Text File", std::string, SOURCE_BASE_PATH + "Commands.txt");
	std::string outFileName = __GET_ARG_IN__(
	    "Output Binary File", std::string, SOURCE_BASE_PATH + "Commands.bin");

	CFOLib::CFO_Compiler compiler;
	__SET_ARG_OUT__("Result", "\n" + compiler.processFile(inFileName, outFileName));

}  //end CompileRunplan()

//========================================================================
void CFOFrontEndInterface::SetRunplan(__ARGS__)
{
	const std::string SOURCE_BASE_PATH =
	    std::string(__ENV__("OTS_SOURCE")) + "/mu2e-pcie-utils/cfoInterfaceLib/";
	__SET_ARG_OUT__(
	    "Result",
	    SetRunplan(__GET_ARG_IN__(
	        "Binary Run File", std::string, SOURCE_BASE_PATH + "Commands.bin")));
}  //end SetRunplan()

//========================================================================
std::string CFOFrontEndInterface::SetRunplan(const std::string& binFilename)
{
	//copying functionality of..
	//	cfoUtil write_program -p /home/kwar/cfo/RunplanFiveDTCs1.bin --cfo 0

	__FE_COUT__ << "Set CFO Run Plan" << __E__;

	__FE_COUTV__(binFilename);

	std::FILE* fp = std::fopen(binFilename.c_str(), "rb");
	if(!fp)
	{
		__SS__ << "Could not open file at " << binFilename << ". Error: " << errno
		       << " - " << strerror(errno) << __E__;
		__SS_THROW__;
	}

	std::string binaryContents;
	std::fseek(fp, 0, SEEK_END);
	binaryContents.resize(std::ftell(fp));
	std::rewind(fp);
	std::fread(&binaryContents[0], 1, binaryContents.size(), fp);
	std::fclose(fp);

	//set the file in hardware:
	thisCFO_->SetRunPlanData(binaryContents, 0 /* address */);

	std::stringstream resultSs;

	thisCFO_->SetLinuxTimestampPreset();
	resultSs << "\n\nInitialized CFO Linux Timestamp to "
	         << StringMacros::getTimestampString(thisCFO_->ReadLinuxTimestamp()) << __E__
	         << __E__;
	resultSs << "Downloaded to CFO binary run plan file: " << binFilename << __E__;
	return resultSs.str();
}  //end SetRunplan()

//========================================================================
void CFOFrontEndInterface::CompileSetAndLaunchTemplateSuperCycleRunPlan(__ARGS__)
{
	uint64_t startTag = __GET_ARG_IN__(
	    "Starting Event Window Tag (Default or -1 := start from 0 and continue)",
	    uint64_t,
	    -1);
	if(startTag == (uint64_t)-1)  //if DEFAULT, then continue from next tag position
	{
		__FE_COUTV__(next_starting_event_window_tag_);
		startTag = next_starting_event_window_tag_;
	}
	//else take user input

	__FE_COUTV__(startTag);

	uint32_t numberOfCycles = __GET_ARG_IN__(
	    "Number of 1.4s super cycle repetitions (0 := infinite)", uint32_t);
	__FE_COUTV__(numberOfCycles);

	//setup next tag calculation
	next_starting_event_window_tag_ = startTag + numberOfCycles * numberOfCycles;
	__FE_COUTV__(next_starting_event_window_tag_);

	__SET_ARG_OUT__(
	    "response",
	    CompileSetAndLaunchTemplateSuperCycleRunPlan(
	        __GET_ARG_IN__(
	            "Enable CFO Run Plan Execution (Default := false)", bool, false),
	        __GET_ARG_IN__("Use Detached Buffer Test (Default := false)", uint32_t),
	        numberOfCycles,
	        startTag,
	        __GET_ARG_IN__("Enable Clock Markers (Default := true)", bool, true),
	        __GET_ARG_IN__(
	            "For Detached Buffer Test, Save Binary Data to File (Default: false)",
	            bool),
	        __GET_ARG_IN__("For Detached Buffer Test, Save Subevent Header to Binary "
	                       "File (Default: false)",
	                       bool),
	        __GET_ARG_IN__(
	            "For Detached Buffer Test, Do NOT Reset Counters (Default: false)",
	            bool)));
}  //end CompileSetAndLaunchTemplateSuperCycleRunPlan()

//========================================================================
// OnOff spill Run Plan is represented as 235K on-spill events and 10K off-spill events
std::string CFOFrontEndInterface::CompileSetAndLaunchTemplateSuperCycleRunPlan(
    bool     enable,
    bool     useDetachedBufferTest,
    uint32_t numberOfSuperCycles,
    uint64_t initialEventWindowTag,
    bool     enableClockMarkers,
    bool     saveBinaryDataToFile,
    bool     saveSubeventHeadersToDataFile,
    bool     doNotResetBufferTestCounters)
{
	__FE_COUTV__(enable);

	std::stringstream outSs;

	halt();
	//halt may disable run plan, but let's make sure:
	thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	if(!enable)  //do not need to apply parameters if disabling
	{
		outSs << "Halted CFO Emulator!" << __E__;
		return outSs.str();
	}
	//else enabling, so apply parameters, then enable

	thisCFO_->SoftReset();  //to reset event window tag starting point handling

	const std::string SOURCE_BASE_PATH = std::string(__ENV__("OTSDAQ_DATA")) + "/";
	std::string       inFileName  = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.txt";
	std::string       outFileName = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.bin";

	//generate Run Plan from template
	{
		std::stringstream out;
		std::string       tabStr, commentStr;
		OUT << "SET_TAG " << initialEventWindowTag << __E__;

		if(numberOfSuperCycles > 0)
			OUT << "LOOP " << numberOfSuperCycles << __E__;
		else
			OUT << "LABEL" << __E__;  //for infinite loop

		PUSHTAB;

		OUT << "LOOP 235000 //32-bits" << __E__;  //start onpill loop
		PUSHTAB;
		OUT << "HEARTBEAT event_mode= 0x100000019" << __E__;
		OUT << "MARKER" << __E__;
		OUT << "WAIT 1.7 us" << __E__;
		OUT << "INC_TAG //increment event window tag" << __E__;
		POPTAB;
		OUT << "DO_LOOP" << __E__;  //end onspill loop

		OUT << "LOOP 10000 //32-bits" << __E__;  //start offpill loop
		PUSHTAB;
		OUT << "HEARTBEAT event_mode= 0x00000020" << __E__;
		OUT << "MARKER" << __E__;
		OUT << "WAIT 100 us" << __E__;
		OUT << "INC_TAG //increment event window tag" << __E__;
		POPTAB;
		OUT << "DO_LOOP" << __E__;  //end offspill loop

		POPTAB;
		if(numberOfSuperCycles > 0)
			OUT << "DO_LOOP" << __E__ << "END" << __E__;
		else
			OUT << "GOTO_LABEL" << __E__;  //for infinite loop

		FILE* fp = fopen(inFileName.c_str(), "w");
		if(!fp)
		{
			__FE_SS__ << "Error - please check path. Generated Run Plan file from "
			             "template could not be created at "
			          << inFileName << __E__;
			__FE_SS_THROW__;
		}
		fputs(out.str().c_str(), fp);
		fclose(fp);

		CFOLib::CFO_Compiler compiler;
		outSs << "\n" << compiler.processFile(inFileName, outFileName);

	}  //done generating template Run Plan

	outSs << SetRunplan(outFileName);

	if(useDetachedBufferTest)
	{
		initDetachedBufferTest(initialEventWindowTag,
		                       saveBinaryDataToFile,
		                       saveSubeventHeadersToDataFile,
		                       doNotResetBufferTestCounters);

		sleep(1);  //allow detached thread to start
	}

	__FE_COUTV__(enableClockMarkers);
	if(enableClockMarkers)
		thisCFO_->EnableEmbeddedClockMarker();
	else
		thisCFO_->DisableEmbeddedClockMarker();

	thisCFO_->EnableLink(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	thisCFO_->EnableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	outSs << "\n\nLaunched CFO Run Plan!" << __E__;
	return outSs.str();
}  //end CompileSetAndLaunchTemplateSuperCycleRunPlan()

//========================================================================
void CFOFrontEndInterface::EnableOrDisableClockMarkers(__ARGS__)
{
	bool enableClockMarkers =
	    __GET_ARG_IN__("Enable Clock Markers (Default := true)", bool, true);
	__FE_COUTV__(enableClockMarkers);
	if(enableClockMarkers)
		thisCFO_->EnableEmbeddedClockMarker();
	else
		thisCFO_->DisableEmbeddedClockMarker();
}  //end EnableOrDisableClockMarkers()

//========================================================================
void CFOFrontEndInterface::CompileSetAndLaunchTemplateFixedWidthRunPlan(__ARGS__)
{
	uint64_t startTag = __GET_ARG_IN__(
	    "Starting Event Window Tag (Default or -1 := start from 0 and continue)",
	    uint64_t,
	    -1);
	if(startTag == (uint64_t)-1)  //if DEFAULT, then continue from next tag position
	{
		__FE_COUTV__(next_starting_event_window_tag_);
		startTag = next_starting_event_window_tag_;
	}
	//else take user input

	__FE_COUTV__(startTag);

	uint32_t numberOfEvents = __GET_ARG_IN__(
	    "Number of Event Window Markers to generate (0 := infinite)", uint32_t);
	__FE_COUTV__(numberOfEvents);

	//setup next tag calculation
	next_starting_event_window_tag_ = startTag + numberOfEvents;
	__FE_COUTV__(next_starting_event_window_tag_);

	std::string modeStr =
	    __GET_ARG_IN__("Event Window Mode (Default := 1)", std::string, "1");

	uint64_t eventWindowMode =
	    modeStr == "0"
	        ? 0
	        : __GET_ARG_IN__(
	              "Event Window Mode (Default := 1)",
	              uint64_t,
	              1);  //allow mode 0 if user inputs it, but default to 1 since mode 0 is a null heartbeat and not a very useful default for a fixed width run plan

	bool onSpill = __GET_ARG_IN__("On-spill Flag (Default := false)", bool, false);
	if(onSpill)
		eventWindowMode |= 0x100000000;

	__SET_ARG_OUT__(
	    "response",
	    CompileSetAndLaunchTemplateFixedWidthRunPlan(
	        __GET_ARG_IN__(
	            "Enable CFO Run Plan Execution (Default := false)", bool, false),
	        __GET_ARG_IN__("Use Detached Buffer Test (Default := false)", bool),
	        __GET_ARG_IN__("Fixed-width Event Window Duration (s, ms, us, ns, and clocks "
	                       "allowed) [clocks := 25ns]",
	                       std::string),
	        numberOfEvents,
	        startTag,
	        eventWindowMode,
	        __GET_ARG_IN__("Enable Clock Markers (Default := true)", bool, true),
	        __GET_ARG_IN__(
	            "For Detached Buffer Test, Save Binary Data to File (Default: false)",
	            bool),
	        __GET_ARG_IN__("For Detached Buffer Test, Save Subevent Header to Binary "
	                       "File (Default: false)",
	                       bool),
	        __GET_ARG_IN__(
	            "For Detached Buffer Test, Do NOT Reset Counters (Default: false)",
	            bool)));
}  //end CompileSetAndLaunchTemplateFixedWidthRunPlan()

//========================================================================
std::string CFOFrontEndInterface::CompileSetAndLaunchTemplateFixedWidthRunPlan(
    bool        enable,
    bool        useDetachedBufferTest,
    std::string eventDuration,
    uint32_t    numberOfEventWindowMarkers,
    uint64_t    initialEventWindowTag,
    uint64_t    eventWindowMode,
    bool        enableClockMarkers,
    bool        saveBinaryDataToFile,
    bool        saveSubeventHeadersToDataFile,
    bool        doNotResetBufferTestCounters)
{
	__FE_COUTV__(enable);
	__FE_COUTV__(eventWindowMode);
	__FE_COUTV__(initialEventWindowTag);

	if(eventWindowMode == (uint64_t)-1)
	{
		__FE_SS__ << "Error - invalid eventWindowMode value. The value -1 is reserved "
		             "in the CFO Run Plan to mean 'leave the current event window mode "
		             "unchanged' "
		             "and is not allowed for a fixed-width run plan. Please use a value "
		             "other than -1."
		          << __E__;
		__FE_SS_THROW__;
	}

	std::stringstream outSs;

	halt();
	//halt may disable run plan, but let's make sure:
	thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	if(!enable)  //do not need to apply parameters if disabling
	{
		outSs << "Halted CFO Emulator!" << __E__;
		return outSs.str();
	}
	//else enabling, so apply parameters, then enable

	thisCFO_->SoftReset();  //to reset event window tag starting point handling

	const std::string SOURCE_BASE_PATH = std::string(__ENV__("OTSDAQ_DATA")) + "/";
	std::string       inFileName  = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.txt";
	std::string       outFileName = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.bin";
	__FE_COUT__ << "Generated Run Plan text file: " << inFileName << __E__;
	__FE_COUT__ << "Compiled Run Plan binary file: " << outFileName << __E__;

	//generate Run Plan and write to input file for compiler
	{
		std::stringstream out;
		std::string       tabStr, commentStr;
		OUT << "SET_TAG " << initialEventWindowTag << __E__;

		if(numberOfEventWindowMarkers > 1)
			OUT << "LOOP " << numberOfEventWindowMarkers - 1 << __E__;
		else
			OUT << "LABEL" << __E__;  //for infinite loop

		PUSHTAB;

		if(numberOfEventWindowMarkers !=
		   1)  //0 count means infinite (1 should be only null)
			OUT << "HEARTBEAT event_mode= " << eventWindowMode << __E__;
		else
			OUT << "HEARTBEAT event_mode= " << 0 << " // null heartbeat!"
			    << __E__;  //null
		OUT << "MARKER" << __E__;

		std::string eventDurationSplitNumber, eventDurationSplitUnits;
		__FE_COUTV__(eventDuration);
		parseEventDurationForRunPlan(
		    eventDuration, eventDurationSplitNumber, eventDurationSplitUnits);
		__FE_COUTV__(eventDurationSplitNumber);
		__FE_COUTV__(eventDurationSplitUnits);
		OUT << "WAIT " << eventDurationSplitNumber << " " << eventDurationSplitUnits
		    << __E__;

		if(0)
		{  //apply fixed width duration
			__FE_COUTV__(eventDuration);
			bool   foundUnits = false;
			size_t i;
			for(i = 0; i < eventDuration.size(); ++i)
				if(eventDuration[i] == 's' || eventDuration[i] == 'm' ||
				   eventDuration[i] == 'u' || eventDuration[i] == 'n' ||
				   eventDuration[i] == 'c')
				{
					foundUnits = true;
					break;
				}

			if(!foundUnits)
			{
				__FE_SS__ << "No units were found in the input parameters 'Fixed-width "
				             "Event Window Duration' value: "
				          << eventDuration
				          << ". Please use units when specifying event window duration "
				             "(s, ms, us, ns, and clocks are allowed). For example "
				             "'1.7us' or '1675ns' would be valid."
				          << __E__;
				__FE_SS_THROW__;
			}
			std::string eventDurationSplitNumber = eventDuration.substr(0, i);
			std::string eventDurationSplitUnits  = eventDuration.substr(i);
			__FE_COUTV__(eventDurationSplitNumber);
			__FE_COUTV__(eventDurationSplitUnits);
			OUT << "WAIT " << eventDurationSplitNumber << " " << eventDurationSplitUnits
			    << __E__;
		}  //end apply fixed width duration
		OUT << "INC_TAG //increment event window tag" << __E__;
		POPTAB;

		if(numberOfEventWindowMarkers > 1)
		{
			OUT << "DO_LOOP" << __E__;

			if(numberOfEventWindowMarkers > 0)
			{
				OUT << "HEARTBEAT event_mode= " << 0 << " // null heartbeat!"
				    << __E__;  //null
				OUT << "MARKER" << __E__;
			}
			OUT << "END" << __E__;
		}
		else
			OUT << "GOTO_LABEL" << __E__;  //for infinite loop

		FILE* fp = fopen(inFileName.c_str(), "w");
		if(!fp)
		{
			__FE_SS__ << "Error - please check path. Generated Run Plan file from "
			             "template could not be created at "
			          << inFileName << __E__;
			__FE_SS_THROW__;
		}
		fputs(out.str().c_str(), fp);
		fclose(fp);

		CFOLib::CFO_Compiler compiler;
		outSs << "\n" << compiler.processFile(inFileName, outFileName);

	}  //done generating template Run Plan

	outSs << SetRunplan(outFileName);

	if(useDetachedBufferTest)
	{
		initDetachedBufferTest(initialEventWindowTag,
		                       saveBinaryDataToFile,
		                       saveSubeventHeadersToDataFile,
		                       doNotResetBufferTestCounters);

		sleep(1);  //allow detached thread to start
	}

	__FE_COUTV__(enableClockMarkers);
	if(enableClockMarkers)
		thisCFO_->EnableEmbeddedClockMarker();
	else
		thisCFO_->DisableEmbeddedClockMarker();

	thisCFO_->EnableLink(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	thisCFO_->EnableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	outSs << "\n\nLaunched CFO Run Plan!" << __E__;
	return outSs.str();
}  //end SetCFOEmulatorFixedWidthEmulation()

//========================================================================
void CFOFrontEndInterface::LaunchRunplan(__ARGS__)
{
	__FE_COUT__ << "Launch CFO Run Plan" << __E__;

	thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->SoftReset();
	usleep(10);
	thisCFO_->EnableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);

}  //end LaunchRunplan()

//========================================================================
void CFOFrontEndInterface::GetCFOCounters(__ARGS__)
{
	__FE_COUT__ << "Getting CFO Counters" << __E__;

	__SET_ARG_OUT__(
	    "Counters",
	    thisCFO_->FormattedRegDump(130, thisCFO_->formattedCounterFunctions_));

}  //end GetCFOCounters()

//==============================================================================
void CFOFrontEndInterface::initDetachedBufferTest(uint64_t initialEventWindowTag,
                                                  bool     saveBinaryDataToFile,
                                                  bool     saveSubeventHeadersToDataFile,
                                                  bool     doNotResetBufferTestCounters)
{
	__FE_COUTV__(saveBinaryDataToFile);
	__FE_COUTV__(doNotResetBufferTestCounters);
	__FE_COUT__ << "Initializing detached buffer test!" << __E__;

	if(!bufferTestThreadStruct_)  //initialize shared pointer for first time
		bufferTestThreadStruct_ =
		    std::make_shared<CFOFrontEndInterface::DetachedBufferTestThreadStruct>();

	if(bufferTestThreadStruct_->running_)
	{
		__FE_COUT__ << "Found buffer test thread already running... so re-initializing"
		            << __E__;

		// start mutex scope
		{
			std::lock_guard<std::mutex> lock(bufferTestThreadStruct_->lock_);
			bufferTestThreadStruct_->expectedEventTag_ = initialEventWindowTag;
			bufferTestThreadStruct_->saveBinaryData_   = saveBinaryDataToFile;
			bufferTestThreadStruct_->publish_ =
			    static_cast<ots::FESupervisor*>(parentSupervisor_)->isPublishingData();
			bufferTestThreadStruct_->feSupervisor_ =
			    static_cast<ots::FESupervisor*>(parentSupervisor_);
			bufferTestThreadStruct_->exitThread_         = false;
			bufferTestThreadStruct_->resetStartEventTag_ = true;
			bufferTestThreadStruct_->doNotResetCounters_ = doNotResetBufferTestCounters;
			bufferTestThreadStruct_->error_              = "";
		}
		__FE_COUT__ << "Found buffer test thread already running... so re-initializing "
		               "and reading data starting at event tag "
		            << initialEventWindowTag << " (0x" << std::hex
		            << initialEventWindowTag << ")" << __E__;
	}
	else
	{
		__FE_COUT__ << "Launching detached Buffer Test thread..." << __E__;
		// start mutex scope
		{
			std::lock_guard<std::mutex> lock(bufferTestThreadStruct_->lock_);
			bufferTestThreadStruct_->expectedEventTag_ = initialEventWindowTag;
			bufferTestThreadStruct_->saveBinaryData_   = saveBinaryDataToFile;
			bufferTestThreadStruct_->publish_ =
			    static_cast<ots::FESupervisor*>(parentSupervisor_)->isPublishingData();
			bufferTestThreadStruct_->feSupervisor_ =
			    static_cast<ots::FESupervisor*>(parentSupervisor_);
			bufferTestThreadStruct_->exitThread_         = false;
			bufferTestThreadStruct_->resetStartEventTag_ = false;
			bufferTestThreadStruct_->thisCFO_            = thisCFO_;
			bufferTestThreadStruct_->running_            = true;
			bufferTestThreadStruct_->doNotResetCounters_ = false;
			bufferTestThreadStruct_->error_              = "";
		}
		std::thread(
		    [](std::shared_ptr<CFOFrontEndInterface::DetachedBufferTestThreadStruct>
		           threadStruct) {
			    CFOFrontEndInterface::detechedBufferTestThread(threadStruct);
		    },
		    bufferTestThreadStruct_)
		    .detach();
		__FE_COUT__ << "Launched detached Buffer Test thread and reading data DMA-0 "
		               "starting at event tag "
		            << initialEventWindowTag << " (0x" << std::hex
		            << initialEventWindowTag << ")" << __E__;
	}

	sleep(1);  //give time for buffer reading to be ready
}  //end initDetachedBufferTest()

//==============================================================================
uint64_t CFOFrontEndInterface::getDetachedBufferTestReceivedCount(
    std::shared_ptr<CFOFrontEndInterface::DetachedBufferTestThreadStruct> threadStruct)
{
	return threadStruct->subeventsCount_;
}  //end getDetachedBufferTestReceivedCount()

//==============================================================================
std::string CFOFrontEndInterface::getDetachedBufferTestStatus(
    std::shared_ptr<CFOFrontEndInterface::DetachedBufferTestThreadStruct> threadStruct)
{
	__COUT__ << "Get detached buffer test status..." << __E__;

	std::stringstream statusSs;

	// start mutex scope
	{
		std::lock_guard<std::mutex> lock(threadStruct->lock_);
		__COUT__ << "Have lock to read..." << __E__;

		if(threadStruct->error_ != "")
			statusSs << "Detached thread caught error:" << threadStruct->error_ << __E__;
		statusSs << "Detached thread running:"
		         << (threadStruct->running_ ? "true" : "false") << __E__;
		statusSs << "Subevents count:" << threadStruct->subeventsCount_ << __E__;

		statusSs << "Total CFO Subevent Bytes Transferred: "
		         << threadStruct->totalSubeventBytesTransferred_ << " bytes" << __E__;

		long long ns =
		    std::chrono::duration_cast<std::chrono::nanoseconds>(
		        threadStruct->transferEndTime_ - threadStruct->transferStartTime_)
		        .count();
		if(ns > 1000)  //prevent divide by 0
		{
			statusSs << "Data Transfer Duration: " << ns / 1000.0 / 1000.0 << " ms"
			         << __E__;
			statusSs << "Average Data Rate: "
			         << ((double)threadStruct->totalSubeventBytesTransferred_) /
			                (ns / 1000.0)
			         << " MB/s" << __E__;
		}
		else
			statusSs << "Data Transfer Duration too short to establish date rate."
			         << __E__;

		statusSs << "Starting Event Window Tag:" << threadStruct->expectedEventTag_
		         << __E__;
		statusSs << "Next Expected Event Window Tag:" << threadStruct->nextEventWindowTag_
		         << __E__;
		if(ns > 1000)  //prevent divide by 0
		{
			statusSs << "Average Event Rate: "
			         << (long long)((threadStruct->nextEventWindowTag_ -
			                         threadStruct->expectedEventTag_) /
			                        (ns / 1000.0 / 1000.0 / 1000.0))
			         << " Events/s" << __E__;
		}
		statusSs << "Mismatched Event Tags count:"
		         << threadStruct->mismatchedEventTagsCount_ << __E__;

		if(threadStruct->mismatchedEventTagJumps_.size() < 20)
		{
			statusSs << "\t Mismatched Tag Jumps..." << __E__;
			for(size_t i = 0; i < threadStruct->mismatchedEventTagJumps_.size(); ++i)
				statusSs << "\t\t Mismatch Jump-" << i << " Expected:"
				         << threadStruct->mismatchedEventTagJumps_[i].first << std::hex
				         << "(0x" << threadStruct->mismatchedEventTagJumps_[i].first
				         << ")" << std::dec << " Received:"
				         << threadStruct->mismatchedEventTagJumps_[i].second << std::hex
				         << "(0x" << threadStruct->mismatchedEventTagJumps_[i].second
				         << ")" << std::dec << __E__;
		}
		else
		{
			statusSs << "\t TOO MANY Mismatched Tag Jumps (showing 10)..." << __E__;
			for(size_t i = 0; i < 10; ++i)
				statusSs << "\t\t Mismatch Jump-" << i << " Expected:"
				         << threadStruct->mismatchedEventTagJumps_[i].first << std::hex
				         << "(0x" << threadStruct->mismatchedEventTagJumps_[i].first
				         << ")" << std::dec << " Received:"
				         << threadStruct->mismatchedEventTagJumps_[i].second << std::hex
				         << "(0x" << threadStruct->mismatchedEventTagJumps_[i].second
				         << ")" << std::dec << __E__;
		}

		if(threadStruct->error_ != "")
		{
			__SS__ << "Error identified in the detached buffer status: "
			       << statusSs.str();
			__SS_THROW__;
		}
	}
	__COUT__ << "Done getting detached buffer test status..." << __E__;

	return statusSs.str();
}  //end getDetachedBufferTestStatus()

//==============================================================================
void CFOFrontEndInterface::handleDetachedSubevent(
    const CFOLib::CFO_Event&                                              subeventIn,
    std::shared_ptr<CFOFrontEndInterface::DetachedBufferTestThreadStruct> threadStruct)
{
	const CFOLib::CFO_Event* subevent = &subeventIn;

	++(threadStruct->subeventsCount_);

#if 1
	// check the subevent tag window
	if(threadStruct->nextEventWindowTag_ !=
	   subevent->GetEventWindowTag().GetEventWindowTag(true))
	{
		++(threadStruct->mismatchedEventTagsCount_);
		std::stringstream ostr;
		ostr << "Mismatched event tag. Expected = " << threadStruct->nextEventWindowTag_
		     << " (0x" << std::hex << std::setw(4) << std::setfill('0')
		     << threadStruct->nextEventWindowTag_ << "), Received = " << std::dec
		     << subevent->GetEventWindowTag().GetEventWindowTag(true) << " (0x"
		     << std::hex << std::setw(4) << std::setfill('0')
		     << subevent->GetEventWindowTag().GetEventWindowTag(true) << ")";
		__COUTT__ << ostr.str();
		threadStruct->mismatchedEventTagJumps_.push_back(
		    std::make_pair<uint64_t, uint64_t>(
		        threadStruct->nextEventWindowTag_,
		        subevent->GetEventWindowTag().GetEventWindowTag(true)));
		__SS__ << ostr.str();
		__SS_THROW__;
		//to freeze TRACE
		//TRACE_CNTL("modeM",0); // "freeze" like command line 'tmodeM 0'
		// TRACE_CNTL("modeM",1); // "unfreeze" like command line 'tmodeM 1'
	}
#endif

	threadStruct->nextEventWindowTag_ =
	    subevent->GetEventWindowTag().GetEventWindowTag(true) + 1;  //increment for next

	// print the subevent header
	// ostr << subevent->toJson() << std::endl;

	//start mutex scope to change non-atomic status counters
	std::lock_guard<std::mutex> lock(threadStruct->lock_);

	if(threadStruct->transferStartTime_ ==
	   std::chrono::steady_clock::time_point::min())  //init start time
		threadStruct->transferStartTime_ = std::chrono::steady_clock::now();

	threadStruct->totalSubeventBytesTransferred_ +=
	    sizeof(CFOLib::CFO_EventRecord);  //for subevent header

#if 1
	//save binary CFO event record data
	{
		auto dataPtr = reinterpret_cast<const uint8_t*>(subevent->GetRawBufferPointer());
		for(uint32_t l = 0; l < sizeof(CFOLib::CFO_EventRecord); l += 4)
		{
			if(threadStruct->fp_)
				fwrite(&dataPtr[l], sizeof(uint32_t), 1, threadStruct->fp_);
			// ostr << "\t0x" << std::hex << std::setw(8) << std::setfill('0') << *((uint32_t *)(&(dataPtr[l]))) << std::endl;
		}

		// To receive published data:
		// 	artdaqDriver -c srcs/artdaq-mu2e/tools/fcl/cfo_driver.fcl
		if(threadStruct->publish_)
			threadStruct->feSupervisor_->publishData((const char*)dataPtr,
			                                         sizeof(CFOLib::CFO_EventRecord));
	}
#endif

	// ostr << std::endl << std::endl;

	//update end time
	threadStruct->transferEndTime_ = std::chrono::steady_clock::now();
}  //end handleDetachedSubevent()

//==============================================================================
// detechedBufferTestThread
void CFOFrontEndInterface::detechedBufferTestThread(
    std::shared_ptr<CFOFrontEndInterface::DetachedBufferTestThreadStruct> threadStruct)
try
{
	__COUT__ << "Buffer test thread established..." << __E__;

	if(threadStruct->fp_)
	{
		__SS__ << "Impossible?! File pointer already initialized?" << __E__;
		__SS_THROW__;
	}

	if(threadStruct->saveBinaryData_)
	{
		std::string filename = "/macroOutput_" + std::to_string(time(0)) + "_" +
		                       std::to_string(clock()) + ".bin";
		filename = std::string(__ENV__("OTSDAQ_DATA")) + "/" + filename;
		__COUTV__(filename);
		threadStruct->fp_ = fopen(filename.c_str(), "wb");
		if(!threadStruct->fp_)
		{
			__SS__ << "Failed to open file to save macro output '" << filename << "'..."
			       << __E__;
			__SS_THROW__;
		}
	}
	//start with clean release
	threadStruct->thisCFO_->ReleaseAllBuffers(DTC_DMA_Engine_DAQ);
	__COUTT__ << "ReleaseAllBuffers called!" << __E__;

	uint64_t ii = 0;

	// start mutex scope
	{
		std::lock_guard<std::mutex> lock(threadStruct->lock_);
		threadStruct->nextEventWindowTag_.store(
		    threadStruct->expectedEventTag_.load(std::memory_order_relaxed),
		    std::memory_order_relaxed);
		__COUT_INFO__
		    << "Starting detached buffer test thread looking for Event Window Tag = "
		    << threadStruct->nextEventWindowTag_ << std::endl;

		threadStruct->error_                    = "";
		threadStruct->subeventsCount_           = 0;
		threadStruct->mismatchedEventTagsCount_ = 0;
		threadStruct->mismatchedEventTagJumps_.clear();
		threadStruct->totalSubeventBytesTransferred_ = 0;
		threadStruct->transferStartTime_ = std::chrono::steady_clock::time_point::min();
		threadStruct->transferEndTime_   = std::chrono::steady_clock::time_point::min();
	}

	uint64_t                                        lastCount = 0;
	std::vector<std::unique_ptr<CFOLib::CFO_Event>> subevents;

	//------------------------
	while(!threadStruct->exitThread_)
	{
		//check for new starting event tag
		{
			if(threadStruct->resetStartEventTag_)
			{
				if(threadStruct->doNotResetCounters_)
					__COUT_INFO__
					    << "NOT Resetting counters; previous status was as follows: \n"
					    << getDetachedBufferTestStatus(threadStruct) << __E__;
				else
					__COUT_INFO__
					    << "Resetting counters; previous status was as follows: \n"
					    << getDetachedBufferTestStatus(threadStruct) << __E__;

				// start mutex scope
				{
					std::lock_guard<std::mutex> lock(threadStruct->lock_);
					threadStruct->nextEventWindowTag_.store(
					    threadStruct->expectedEventTag_.load(std::memory_order_relaxed),
					    std::memory_order_relaxed);
					__COUT_INFO__ << "Restarting detached buffer test thread looking for "
					                 "Event Window Tag = "
					              << threadStruct->nextEventWindowTag_ << std::endl;

					//reset counts and (re)open file
					if(!threadStruct->doNotResetCounters_)
					{
						//close and reopen file
						if(threadStruct->fp_)
						{
							fclose(threadStruct->fp_);
							threadStruct->fp_ = nullptr;
						}

						if(threadStruct->saveBinaryData_)
						{
							std::string filename = "/macroOutput_" +
							                       std::to_string(time(0)) + "_" +
							                       std::to_string(clock()) + ".bin";
							filename =
							    std::string(__ENV__("OTSDAQ_DATA")) + "/" + filename;
							__COUTV__(filename);
							threadStruct->fp_ = fopen(filename.c_str(), "wb");
							if(!threadStruct->fp_)
							{
								__SS__ << "Failed to open file to save macro output '"
								       << filename << "'..." << __E__;
								__SS_THROW__;
							}
						}

						threadStruct->error_                    = "";
						threadStruct->subeventsCount_           = 0;
						threadStruct->mismatchedEventTagsCount_ = 0;
						threadStruct->mismatchedEventTagJumps_.clear();
						threadStruct->totalSubeventBytesTransferred_ = 0;
						threadStruct->transferStartTime_ =
						    std::chrono::steady_clock::time_point::min();
						threadStruct->transferEndTime_ =
						    std::chrono::steady_clock::time_point::min();
					}
					else  //do not reset counters and do not close file unless no longer saving binary data
					{
						//close and reopen file
						if(!threadStruct->saveBinaryData_ && threadStruct->fp_)
						{
							fclose(threadStruct->fp_);
							threadStruct->fp_ = nullptr;
							__COUT__ << "Binary file closed." << __E__;
						}
					}

					threadStruct->resetStartEventTag_ = false;  //clear mailbox
				}

				//release buffers for restart
				threadStruct->thisCFO_->ReleaseAllBuffers(DTC_DMA_Engine_DAQ);
			}

		}  //done with check for starting event window tag

		//CFO Records are a "subevent" (i.e. there is no opportunity for hardware event building including the CFO)
		{
			__COUTT__ << __COUT_HDR__
			          << "get the data requested as events via ->GetSubEventData(...)";
			//GetData will clear subevents first
			while(threadStruct->thisCFO_->GetData(subevents) && subevents.size())
			{
				__COUTT__ << __COUT_HDR__ << "Read iteration #" << ii++
				          << ": SubEvents returned by the DTC: " << subevents.size()
				          << std::endl;

				if(subevents.empty())
					continue;  //impossible!

				for(auto& subeventPtr : subevents)
				{
					if(subeventPtr == nullptr)
					{
						__COUT_ERR__ << "Error: Subevent Null pointer!" << std::endl;
						continue;
					}
					handleDetachedSubevent(*(subeventPtr.get()), threadStruct);
				}
				//threadStruct->thisCFO_->ReleaseBuffers(DTC_DMA_Engine_DAQ, subevents.size()); // This currently does not exist, but it would be most efficient to release here
			}  //end primary Sub Event loop
			//if here, no more data in DMA buffer
			if(lastCount != threadStruct->subeventsCount_ || ii % 100 == 0)
			{
				__COUTT__
				    << "No more subevents found in DMA bufferr... waiting... iteration #"
				    << ii
				    << ", SubEvents received so far = " << threadStruct->subeventsCount_
				    << __E__;
				lastCount = threadStruct->subeventsCount_;

				// threadStruct->thisCFO_->GetDevice()->spy(DTC_DMA_Engine_DAQ, 3 /* for once */ | 8 /* for wide view */ | 16 /* for stack trace */);
			}
		}  // end Sub Event handling

		if(threadStruct->fp_)
			fflush(threadStruct->fp_);
		// usleep(100); //100 us sleep // this sleep affects bandwidth!
		std::this_thread::yield();  //try to be nice to other threads
		++ii;
	}  //end primary loop -------

	if(threadStruct->fp_)
	{
		fclose(threadStruct->fp_);
		threadStruct->fp_ = nullptr;
	}

	__COUT_INFO__ << "Buffer test thread exited. "
	              << " CFO Event Record SubEvents received = "
	              << threadStruct->subeventsCount_ << __E__;
	threadStruct->running_ = false;

}  //end detechedBufferTestThread()
catch(...)
{
	std::stringstream errSs;
	errSs << "Exception caught. Exiting detechedBufferTestThread()." << __E__;
	threadStruct->thisCFO_->GetDevice()->spy(
	    DTC_DMA_Engine_DAQ,
	    3 /* for once */ | 8 /* for wide view */ | 16 /* for stack trace */);

	threadStruct->running_ = false;
	try
	{
		throw;
	}
	catch(const std::runtime_error& e)
	{
		errSs << "Error message: " << e.what() << __E__;
	}
	catch(...)
	{
		errSs << "Unknown error." << __E__;
	}
	threadStruct->error_ += errSs.str();
	__COUT_ERR__ << errSs.str();
}  //end detechedBufferTestThread() exception handling

//========================================================================
void CFOFrontEndInterface::CFOReset(__ARGS__)
{
	__FE_COUT_INFO__ << "Setting up CFO for RTF, Reset and Buffer Release!" << __E__;
	next_starting_event_window_tag_ = 0;  //reset

	halt();
	//halt may disable run plan, but let's make sure:
	thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	getCFOandDTCRegisters()->SetJitterAttenuatorSelect(1 /* select RJ45 */,
	                                                   false /* alsoResetJA */);
	sleep(1);
	__FE_COUT_INFO__ << "JA Status = "
	                 << getCFOandDTCRegisters()->FormatJitterAttenuatorCSR() << __E__;

	thisCFO_->CFOandDTC_Registers::ResetSERDES();
	thisCFO_->ResetSERDES(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	thisCFO_->SoftReset();
	thisCFO_->ReleaseAllBuffers(DTC_DMA_Engine_DAQ);

	thisCFO_->EnableEmbeddedClockMarker();

	__FE_COUT_INFO__ << "Reset and ReleaseAllBuffers called!" << __E__;
}  //end CFOReset()

//========================================================================
void CFOFrontEndInterface::CFOHalt(__ARGS__)
{
	halt();
	//halt may disable run plan, but let's make sure:
	thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
}  //end CFOHalt()

//========================================================================
void CFOFrontEndInterface::GetCounters(__ARGS__)
{
	__SET_ARG_OUT__(
	    "Status", thisCFO_->FormattedRegDump(130, thisCFO_->formattedCounterFunctions_));
}  //end GetCounters()

//========================================================================
void CFOFrontEndInterface::ConfigureForTimingChain(__ARGS__)
{
	int stepIndex = __GET_ARG_IN__("StepIndex", int);

	// do 0, then 1
	configureForTimingChain(stepIndex);

}  //end ConfigureForTimingChain()

//========================================================================
void CFOFrontEndInterface::loopbackTest(std::string runNumber, int step)
{
	__FE_COUT__ << "Starting loopback test for run " << runNumber << ", step " << step
	            << __E__;
	// TODO: read from configuratione

	const int          ROCsPerDTC = 6;
	const unsigned int n_loopbacks =
	    getConfigurationManager()
	        ->getNode("/Mu2eGlobalsTable/SyncDemoConfig/NumberOfLoopbacks")
	        .getValue<unsigned int>();
	const unsigned int DTCsPerChain = 8;  //getConfigurationManager()
	    //->getNode("/Mu2eGlobalsTable/SyncDemoConfig/DTCsPerChain").getValue<unsigned int>();

	const int    alignment_marker = 10;  // TODO: check with the firmware
	unsigned int n_steps          = DTCsPerChain * ROCsPerDTC;

	if(step == -1)
		step = getIterationIndex();  // get the current index

	// alternate with the DTCs
	if((step % 2) == 0)
	{
		indicateIterationWork();
		__FE_COUT__ << "Step " << step << " is even, letting DTCs have a turn" << __E__;
		return;
	}

	unsigned int loopback_step = step / 2;
	// end by restoring the status of the registers
	if(loopback_step >= n_steps)
	{
		__FE_COUT__ << "Loopback over!" << __E__;
		return;
	}

	// send the markers  and compute the average delay
	int active_DTC = loopback_step / ROCsPerDTC;
	int active_ROC = loopback_step % ROCsPerDTC;

	__FE_COUT__ << "step " << loopback_step << ") active DTC: " << active_DTC
	            << " active ROC on link: " << active_ROC << __E__;

	// TODO: put it in a directory
	FILE*       fp       = 0;
	std::string filename = "/loopbackOutput_" + runNumber + ".txt";

	__FE_COUT__ << "Sending " << n_loopbacks << " markers on all the links..." << __E__;
	for(auto link : CFOLib::CFO_Links)
	{
		__FE_COUT__ << "step " << loopback_step << ") CFO sending markers on" << __E__
		            << "CFO link:\t" << link << __E__ << "target DTC:\t" << active_DTC
		            << __E__ << "target ROC:\t" << active_ROC << __E__;
		unsigned int comulative_delay = 0;
		float        average_delay    = 0.0;
		bool         timeout          = false;

		// sending first marker to align the clock of the ROC
		for(unsigned int n = 0; n < alignment_marker; ++n)
		{
			measureDelay(link);
		}

		for(unsigned int n = 0; n < n_loopbacks; ++n)
		{
			std::bitset<32> delay(measureDelay(link));
			// check if the marker return timeout
			if(delay.all())
			{
				__FE_COUT__ << "Timeout link: " << link << __E__;
				timeout = true;
				break;
			}
			comulative_delay += delay.to_ulong();
			__FE_COUT__ << "step " << loopback_step << ") Delay measured on link " << link
			            << ": " << delay.to_ulong() << __E__;
		}
		// compute the average
		average_delay = comulative_delay / n_loopbacks;
		__FE_COUT__ << "step " << loopback_step << ") Average Delay on link " << link
		            << ": " << average_delay << __E__;

		// save the results on file
		try
		{
			// open the file if it is the first time
			if(!fp)
			{
				__FE_COUT__ << "File " << filename << " open." << __E__;
				fp = fopen((std::string(__ENV__("OTSDAQ_DATA")) + filename).c_str(), "a");
			}
			// write the information
			fprintf(fp, "############################\n");
			fprintf(fp, "Chain:\t%d\n", link);
			fprintf(fp, "DTC:\t%d\n", active_DTC);
			fprintf(fp, "ROC:\t%d\n", active_ROC);
			if(timeout)
				fprintf(fp, "Delay:\tTIMEOUT\n");
			else
				fprintf(fp, "Delay:\t%f\n", average_delay);
		}
		catch(...)  // handle file close on error
		{
			if(fp)
			{
				__FE_COUT__ << "Error occurs: File close." << __E__;
				fclose(fp);
			}
			throw;
		}
	}

	// close the file
	if(fp)
	{
		__FE_COUT__ << "File close." << __E__;
		fclose(fp);
	}

	indicateIterationWork();
}  //end loopbackTest()

//========================================================================
/// Get Event Mode, Tag, Active Subsystems, and running status
/// 	and active Run Plan Base Address
void CFOFrontEndInterface::SharedRunPlanStatus(__ARGS__)
{
	std::stringstream result;
	std::string       divider(55, '=');
	divider += "\n";

	result << "CFO Run Plan Status: " << __E__;
	result << "\n" << divider << thisCFO_->FormatRunPlanCurrentTag() << __E__;

	uint64_t eventDurationInClocks = extractSharedRunPlanEventDuration();
	result << "\n"
	       << divider
	       << "Event Window Duration:                      " << eventDurationInClocks
	       << " 0x" << std::hex << eventDurationInClocks << std::dec << " clocks ("
	       << (eventDurationInClocks * FPGAClock_ / 1000.0) << " us)" << __E__;
	if(eventDurationInClocks > 0)
		result << "\n"
		       << divider << "Event Window Rate:                         "
		       << (1000.0 / (eventDurationInClocks * FPGAClock_ / 1000.0)) << " kHz"
		       << __E__;
	else
		result << "\n"
		       << divider << "Event Window Rate:                         0" << __E__;

	result << "\n" << divider << thisCFO_->FormatRunPlanCurrentMode() << __E__;
	result << "\n" << divider << thisCFO_->FormatBeamOnMode() << __E__;
	result << "\n" << divider << thisCFO_->FormatBeamOffMode() << __E__;
	result << "\n" << divider << thisCFO_->FormatRunPlanBeamOnBaseAddress() << __E__;
	result << "\n" << divider << thisCFO_->FormatRunPlanBeamOffBaseAddress() << __E__;

	result << "\n" << divider << __E__;
	uint64_t val = thisCFO_->ReadReceiveRF0MarkerCount();
	result << "RF-0 Markers Received (16-bits):            " << std::dec << val << " (0x"
	       << std::hex << val << ")" << __E__;
	val = thisCFO_->ReadTransmitHeartbeatPacketCount();
	result << "Heartbeats Transmitted (16-bits):           " << std::dec << val << " (0x"
	       << std::hex << val << ")" << __E__;
	val = thisCFO_->ReadTransmitEventWindowMarkerCount();
	result << "Event Window Markers Transmitted (16-bits): " << std::dec << val << " (0x"
	       << std::hex << val << ")" << __E__;

	//now readback current run plan
	std::map<uint32_t /* address */,
	         std::pair<uint32_t /* expected */, uint32_t /* actual */>>
	            mismatches;
	std::string binaryContents;
	binaryContents.resize(sharedRunPlanSize_);
	for(size_t i = 0; i < sharedRunPlanSize_; ++i)
		binaryContents[i] = (char)0xFF;
	thisCFO_->CompareRunPlanData(binaryContents, 0 /* address */, mismatches);

	result << "\n\n" << divider << "Run Plan Readback:\n";

	const std::string LOOP = CFOLib::CFO_Compiler::CODE_to_OP_TRANSLATION.at(
	    (uint8_t)CFOLib::CFO_Compiler::CFO_INSTR::LOOP);
	const std::string DO_LOOP = CFOLib::CFO_Compiler::CODE_to_OP_TRANSLATION.at(
	    (uint8_t)CFOLib::CFO_Compiler::CFO_INSTR::DO_LOOP);
	const std::string MARKER = CFOLib::CFO_Compiler::CODE_to_OP_TRANSLATION.at(
	    (uint8_t)CFOLib::CFO_Compiler::CFO_INSTR::MARKER);
	const std::string OR_MODE_BITS = CFOLib::CFO_Compiler::CODE_to_OP_TRANSLATION.at(
	    (uint8_t)CFOLib::CFO_Compiler::CFO_INSTR::OR_MODE_BITS);
	const std::string OR_SINGLESHOT_MODE_BITS =
	    CFOLib::CFO_Compiler::CODE_to_OP_TRANSLATION.at(
	        (uint8_t)CFOLib::CFO_Compiler::CFO_INSTR::OR_SINGLESHOT_MODE_BITS);
	const std::string AND_MODE_BITS = CFOLib::CFO_Compiler::CODE_to_OP_TRANSLATION.at(
	    (uint8_t)CFOLib::CFO_Compiler::CFO_INSTR::AND_MODE_BITS);

	//============================
	/// lambda function to create a comment for the bits set by OR_MODE_BITS and OR_SINGLESHOT_MODE_BITS instructions
	auto formatSetBitsComment = [this](uint64_t data48) {
		const uint64_t all48 = 0xFFFFFFFFFFFFULL;
		if(data48 == all48)
			return std::string("all bits");
		if(data48 == 0)
			return std::string("no bits");

		std::map<uint16_t, std::string> subsystemBitName = {
		    {static_cast<uint16_t>(SharedRunPlanSubsystemModeBit::CRV), "CRV"},
		    {static_cast<uint16_t>(SharedRunPlanSubsystemModeBit::Calo), "Calo"},
		    {static_cast<uint16_t>(SharedRunPlanSubsystemModeBit::Calo_inject),
		     "Calo Inject"},
		    {static_cast<uint16_t>(SharedRunPlanSubsystemModeBit::Tracker), "Tracker"},
		    {static_cast<uint16_t>(SharedRunPlanSubsystemModeBit::STM), "STM"},
		    {static_cast<uint16_t>(SharedRunPlanSubsystemModeBit::ExtMon), "ExtMon"},
		    {static_cast<uint16_t>(SharedRunPlanSubsystemModeBit::HWDev), "HWDev"}};

		std::vector<std::string> bitDescriptions;
		for(uint16_t b = 0; b < 48; ++b)
			if((data48 >> b) & 0x1ULL)
			{
				auto it = subsystemBitName.find(b);
				if(it != subsystemBitName.end())
					bitDescriptions.push_back(std::to_string(b) + "(" + it->second + ")");
				else
					bitDescriptions.push_back(std::to_string(b));
			}

		if(bitDescriptions.size() == 1)
			return std::string("bit-") + bitDescriptions.front();

		std::stringstream bitsSs;
		bitsSs << "bits-";
		for(size_t i = 0; i < bitDescriptions.size(); ++i)
		{
			std::string entry = bitDescriptions[i];
			bitsSs << (i ? "," : "") << entry;
		}
		return bitsSs.str();
	};  //end lambda formatSetBitsComment()

	uint32_t          expectedAddress = 0;
	std::stringstream loDataSs;
	uint32_t          loDataWord = 0;
	std::string       tabStr     = "";
	int               markerCnt  = 0;
	for(const auto& mismatch : mismatches)
	{
		if(expectedAddress % 2 == 1)  //groups of two words per address
			result << "\n"
			       << "Line #" << std::dec << std::setfill(' ') << std::setw(4)
			       << mismatch.first / 2 << ":" << tabStr << "     0x";

		while(mismatch.first > expectedAddress)  //pad with all F's (since not a mismatch)
		{
			if(expectedAddress % 2 == 0)  //groups of two words per address
			{
				loDataSs.str("");  //clear for new line
				loDataWord = 0xFFFFFFFF;
				loDataSs << std::hex << std::setfill('0') << std::setw(8) << 0xFFFFFFFF
				         << std::dec << " ";
			}
			else  // should be impossible to have opcode 0xFF!
				result << std::hex << std::setfill('0') << std::setw(8) << 0xFFFFFFFF
				       << std::dec << " " << loDataSs.str() << "   // INVALID";

			++expectedAddress;
			if(expectedAddress % 2 == 1)  //groups of two words per address (hi then lo)
				result << "\n"
				       << "Line #" << std::dec << std::setfill(' ') << std::setw(4)
				       << mismatch.first / 2 << ":" << tabStr << "     0x";
		}

		if(expectedAddress % 2 == 0)  //groups of two words per address
		{
			loDataSs.str("");  //clear for new line
			loDataWord = mismatch.second.second;
			loDataSs << std::hex << std::setfill('0') << std::setw(8)
			         << mismatch.second.second << std::dec << " ";
		}
		else
		{
			std::string op = "INVALID";
			if(CFOLib::CFO_Compiler::CODE_to_OP_TRANSLATION.find(mismatch.second.second >>
			                                                     24) !=
			   CFOLib::CFO_Compiler::CODE_to_OP_TRANSLATION.end())
				op = CFOLib::CFO_Compiler::CODE_to_OP_TRANSLATION.at(
				    mismatch.second.second >> 24);

			if(op == LOOP)
				tabStr += '\t';
			else if(op == DO_LOOP && tabStr.length())
				tabStr = tabStr.substr(0, tabStr.length() - 1);
			result << std::hex << std::setfill('0') << std::setw(8)
			       << mismatch.second.second << std::dec << " " << loDataSs.str()
			       << "   // " << op;
			if(op == OR_MODE_BITS || op == OR_SINGLESHOT_MODE_BITS)
			{
				uint64_t data48 =
				    (uint64_t(mismatch.second.second & 0xFFFF) << 32) | loDataWord;
				result << " sets " << formatSetBitsComment(data48) << "";
			}
			else if(op == AND_MODE_BITS)
			{
				uint64_t data48 =
				    (uint64_t(mismatch.second.second & 0xFFFF) << 32) | loDataWord;
				result << " clears "
				       << formatSetBitsComment(~(data48 | (uint64_t(0xFFFF) << 48)))
				       << "";
			}
			if(op == MARKER)
				result << " -----> #" << ++markerCnt;
		}

		expectedAddress = mismatch.first + 1;
	}                 //end run plan readback loop
	result << __E__;  //closing new line

	__SET_ARG_OUT__("Result", result.str());
}  //end SharedRunPlanStatus()

//========================================================================
void CFOFrontEndInterface::SharedRunPlanStart(__ARGS__)
{
	if(thisCFO_->ReadBeamOnMode() || thisCFO_->ReadBeamOffMode())
	{
		__SS__ << "Error: CFO is already in a Run Plan. Please do 'CFO Halt' to halt the "
		          "current Run Plan before starting a new one."
		       << __E__;
		__SS_THROW__;
	}

	uint64_t initEventMode = __GET_ARG_IN__("Initial Event Mode (Default = 0)", uint64_t);
	uint64_t initEventTag  = __GET_ARG_IN__("Initial Event Tag  (Default = 0)", uint64_t);
	std::string eventDuration = __GET_ARG_IN__(
	    "Run Plan Event Window Duration (s, ms, us, ns, and clocks allowed) [clocks := "
	    "25ns] (Default = 1.8 us)",
	    std::string,
	    "1.8 us");

	const double CALO_INJECT_RATE_PER_SEC = 1.0 / 1.5;  //1 injection per 1.5 seconds

	//Parse here because need slightly different run plan than standard fixed-width run
	//	and it may diverge further over time.
	//		* Need periodic Calo laser injection
	//
	// Mode Packet Definition: -- from docdb 4914 --
	// 		Event Mode Byte 1 (Resrv’d Trk)	Event Mode Byte 0 [7:3] 	Pattern Mode [2:1]	Injection Data Source [0]
	// 		Event Mode Byte 3 (Resrv’d CRV)	Event Mode Byte 2 (Resrv’d Calo) [7:1]	Calo Laser Injection [0]
	// 		Delivery Ring RF-0 Marker TDC [15:8]	Resrv’d (TEM) [7:6] (STM) [5:4] 	Subrun Handling [3:1]	On-spill Flag [0]
	//
	// The high Event Mode bit, for example bit index 7 of a subsystem’s mode byte (or bit 1 of a subsystem’s 2-bit mode,
	// is considered the active bit.  If set, the corresponding subsystem is expected to record data for that Event Window.
	//
	// For the Calorimeter, bit-16 := bit 0 of Event Mode Byte 2 is used to trigger laser injection.
	//
	// 	subsystemModeMap["Tracker"] = (mode >> 8) & 0xFF;  // bits [7:0] of Event Mode Byte-1
	// 	subsystemModeMap["Calo"] = (mode >> 16) & 0xFF;  // bits [7:0] of Event Mode Byte-2
	// 	subsystemModeMap["CRV"] = (mode >> 32) & 0xFF;  // bits [7:0] of Event Mode Byte-3
	// 	subsystemModeMap["STM"] = (mode >> 36) & 0x3;  // bits [1:0] of Event Mode Byte-4 upper nibble
	// 	subsystemModeMap["ExtMon"] = (mode >> 38) & 0x3;  // bits [3:2] of Event Mode Byte-4 upper nibble
	//
	//
	// Run Plan will almost be equivalent to this (except with MODE AND/OR):
	// 		result << CompileSetAndLaunchTemplateFixedWidthRunPlan(
	// 			true, 			//enable
	// 			false, 			//useDetachedBufferTest
	// 			eventDuration,
	// 			0,				//numberOfEventWindowMarkers (0 = infinite)
	// 			initEventTag,
	// 			initEventMode,
	// 			true, 			//enableClockMarkers
	// 			false,			//saveBinaryDataToFile
	// 			false,			//saveSubeventHeadersToDataFile
	// 			false			//doNotResetBufferTestCounters
	// 		) << __E__;

	std::stringstream result;

	std::string eventDurationSplitNumber, eventDurationSplitUnits;
	__FE_COUTV__(eventDuration);
	parseEventDurationForRunPlan(
	    eventDuration, eventDurationSplitNumber, eventDurationSplitUnits);
	__FE_COUTV__(eventDurationSplitNumber);
	__FE_COUTV__(eventDurationSplitUnits);
	uint32_t eventDurationInClocks =
	    CFOandDTCCoreVInterface::convertEventDurationToClocks(eventDuration);
	__FE_COUTV__(eventDurationInClocks);

	//calculate number of clocks per Calo Inject
	//
	// 	FPGAClock_  = (ns / clock)
	// 	CALO_INJECT_RATE_PER_SEC = (inject / sec)
	//
	// want (clocks / inject)...
	//		(inject / sec) * (sec / 1e9 ns) = (inject / ns)
	//			... * (ns / clock) = (inject / clock)

	double caloInjectClocks =
	    CALO_INJECT_RATE_PER_SEC * (1 / 1e9) * CFOandDTCCoreVInterface::FPGAClock_;
	__FE_COUTV__(caloInjectClocks);
	uint32_t caloClocksPerInject = 1 / caloInjectClocks;
	__FE_COUTV__(caloClocksPerInject);

	//determine M:N on ratio for calo inject
	uint32_t mPartRatio, nPartRatio;
	getRatioOfOnPerEvents(caloClocksPerInject,    //target (clocks / on) rate
	                      eventDurationInClocks,  // (clocks / event)
	                      mPartRatio,
	                      nPartRatio);
	__FE_COUTV__(mPartRatio);
	__FE_COUTV__(nPartRatio);

	halt();
	//halt may disable run plan, but let's make sure:
	thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_
	    ->SoftReset();  //to reset event window tag starting point handling and mode = 0

	const std::string SOURCE_BASE_PATH = std::string(__ENV__("OTSDAQ_DATA")) + "/";
	std::string       inFileName  = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.txt";
	std::string       outFileName = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.bin";
	result << "Generated Run Plan text file: <FILE>" << inFileName << "</FILE>" << __E__;
	result << "Compiled Run Plan binary file: <FILE>" << outFileName << "</FILE>"
	       << __E__;
	__FE_COUT__ << "Generated Run Plan text file: " << inFileName << __E__;
	__FE_COUT__ << "Compiled Run Plan binary file: " << outFileName << __E__;

	//generate Run Plan and write to input file for compiler
	// Note: as of 22-Feb-2026, Run Plan BRAM is 1024 ops
	//	Set Run Plan checks BRAM size indirectly, by reading back and validating the instruction set written!
	{
		//Start overwrites whatever is in hardware; use empty/default masks for both parts.
		const size_t          N_coarse_sz = standardNValues_.size() - 1;
		std::vector<uint64_t> emptyAndMasks(standardNValues_[0] + 1 + N_coarse_sz,
		                                    0xFFFFFFFFFFFFULL);  //keep-all
		std::vector<uint64_t> emptyOrMasks(N_coarse_sz + standardNValues_[0],
		                                   0x0ULL);  //set-nothing

		//part-1: write the initial all-on mode plan
		generateSharedRunPlanWithPeriodicModeOn(result,
		                                        inFileName,
		                                        initEventTag,
		                                        0,              //start bit
		                                        48,             //bit count
		                                        initEventMode,  //init bits ON
		                                        1,              // duty M in M:N on
		                                        1,              // duty N in M:N on
		                                        0,              //eventOffsetInLoop
		                                        eventDurationSplitNumber,
		                                        eventDurationSplitUnits,
		                                        emptyAndMasks,
		                                        emptyOrMasks);
		{
			CFOLib::CFO_Compiler compiler;
			result << "\n\nRun Plan part-1:\n"
			       << compiler.processFile(inFileName, outFileName);
			__FE_COUT__ << result.str();
			result << SetRunplan(outFileName);
			thisCFO_->EnableEmbeddedClockMarker();
			thisCFO_->EnableLink(CFOLib::CFO_Link_ID::CFO_Link_ALL);
			thisCFO_->EnableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
			__FE_COUT__ << "Shared run plan part-1 started." << __E__;
		}

		//part-2: merge calo inject bit on top of part-1 (still fresh start, so empty masks are valid)
		generateSharedRunPlanWithPeriodicModeOn(
		    result,
		    inFileName,
		    initEventTag,
		    (uint16_t)SharedRunPlanSubsystemModeBit::Calo_inject,  //start bit
		    1,                                                     //bit count
		    1,                                                     //calo inject bit ON
		    mPartRatio,                                            // duty M in M:N on
		    nPartRatio,                                            // duty N in M:N on
		    0,                                                     //eventOffsetInLoop
		    eventDurationSplitNumber,
		    eventDurationSplitUnits,
		    emptyAndMasks,
		    emptyOrMasks);

		CFOLib::CFO_Compiler compiler;
		result << "\n\nRun Plan part-2:\n"
		       << compiler.processFile(inFileName, outFileName);
		result << SetRunplan(outFileName);
		__FE_COUT__ << "Shared run plan part-2 started." << __E__;
	}  //end generate and set Run Plan

	__FE_COUT_INFO__ << "Shared run plan started." << __E__;
	__SET_ARG_OUT__("Result", result.str());
}  //end SharedRunPlanStart()

//========================================================================
void CFOFrontEndInterface::SharedRunPlanStop(__ARGS__)
{
	halt();
	//halt may disable run plan, but let's make sure:
	thisCFO_->DisableBeamOnMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);
	thisCFO_->DisableBeamOffMode(CFOLib::CFO_Link_ID::CFO_Link_ALL);

	std::stringstream result;
	result << "Done" << __E__;
	__SET_ARG_OUT__("Result", result.str());
}  //end SharedRunPlanStop()

//========================================================================
void CFOFrontEndInterface::SharedRunPlanSubsystemJoin(__ARGS__)
{
	if(!(thisCFO_->ReadBeamOnMode() || thisCFO_->ReadBeamOffMode()))
	{
		__SS__ << "Error: CFO is not currently in a Run Plan. Please do 'Shared Run Plan "
		          "Start' to start the shared Run Plan before adding subsystems."
		       << __E__;
		__SS_THROW__;
	}

	std::string subsystem =
	    __GET_ARG_IN__("Subsystem Name (CRV, Calo, Tracker, STM, ExtMon, Custom)",
	                   std::string,
	                   "Custom");
	std::string dutyCycle = __GET_ARG_IN__(
	    "Duty Cycle (% or M:N on:event ratio, Default = 100%)", std::string, "100%");

	// std::string runType = __GET_ARG_IN__("Run Type (Supercycle Emulation = 1, Fixed-width Windows = 0) (Default = Fixed-width Windows)",std::string,"Fixed-width Windows");
	uint32_t eventOffsetInLoop =
	    __GET_ARG_IN__("Event Offset in Loop (Default = 0)", uint32_t);

	std::stringstream result;
	result << "\nAdding subsystem '" << subsystem << "' to the Shared Run Plan with " <<
	    // "type='" << runType << "' " <<
	    "dutyCycle '" << dutyCycle << "'..." << __E__;

	__FE_COUTV__(subsystem);
	__FE_COUTV__(dutyCycle);
	if(supportedSubsystems_.find(subsystem) == supportedSubsystems_.end())
	{
		__FE_SS__ << "Specified subsystem '" << subsystem
		          << "' was not found in the set of supported subsystems: ";
		for(auto& subsystemPair : supportedSubsystems_)
			ss << "\t" << subsystemPair.first << __E__;
		__FE_SS_THROW__;
	}

	// __FE_COUTV__(runType);
	// uint16_t runTypeIndex = -1;
	// if(runType.size() < 3) //assume number input
	// 	runTypeIndex = __GET_ARG_IN__("Run Type (Supercycle Emulation = 1, Fixed-width Windows = 0) (Default = Fixed-width Windows)",uint16_t,0);
	// else if(runType == "Supercycle Emulation")
	// 	runTypeIndex = 1;
	// else if(runType == "Fixed-width Windows")
	// 	runTypeIndex = 0;

	// __FE_COUTV__(runTypeIndex);
	// if(runTypeIndex  > 1)
	// {
	// 	__FE_SS__ << "Illegal run type specified '" << runType << "'.. expecting 0, 1, 'Supercycle Emulation' or 'Fixed-width Windows'" << __E__;
	// 	__FE_SS_THROW__;
	// }

	__FE_COUTV__(dutyCycle);
	__FE_COUTV__(eventOffsetInLoop);
	uint32_t mPartRatio, nPartRatio;
	if(dutyCycle.size() && dutyCycle[dutyCycle.size() - 1] == '%')
	{
		mPartRatio = std::strtoul(dutyCycle.c_str(), nullptr, 10);
		nPartRatio = 100;

		if(mPartRatio > 100)
		{
			__FE_SS__ << "Illegal duty cycle percentage '" << dutyCycle
			          << "'.. expecting a percentage less than or equal to 100%."
			          << __E__;
			__FE_SS_THROW__;
		}
	}
	else  //assume in M:N ratio format
	{
		std::vector<std::string> dutyCycleSplit =
		    StringMacros::getVectorFromString(dutyCycle, {':'});
		__FE_COUTV__(StringMacros::vectorToString(dutyCycleSplit));
		if(dutyCycleSplit.size() != 2)
		{
			__FE_SS__ << "Illegal duty cycle ratio '" << dutyCycle
			          << "'.. expecting M:N format, e.g. 1:200." << __E__;
			__FE_SS_THROW__;
		}
		mPartRatio = std::strtoul(dutyCycleSplit[0].c_str(), nullptr, 10);
		nPartRatio = std::strtoul(dutyCycleSplit[1].c_str(), nullptr, 10);
	}

	//extract and/or op values to be modified in the join
	std::vector<uint64_t> existingAndMasks, existingOrMasks;
	uint64_t              eventDurationInClocks =
	    extractSharedRunPlanEventDuration(existingAndMasks, existingOrMasks);
	__FE_COUTV__(eventDurationInClocks);

	//now need to insert subsystems enable bit in run plan at duty cycle
	//if custom, take user input, otherwise use subsystem valid bit
	uint16_t onBits_startBit = 0;
	uint16_t onBits_bitCount = 48;
	uint64_t onBits_value    = 0;

	if(subsystem == "Custom")  //custom mode bits!
	{
		__FE_COUTT__ << "Custom subsystem identified!" << __E__;
		onBits_startBit =
		    __GET_ARG_IN__("Custom Mode Bit Position (Default = 0)", uint16_t, 0);
		onBits_bitCount =
		    __GET_ARG_IN__("Custom Mode Bit Count (Default = 48)", uint16_t, 48);
		onBits_value = __GET_ARG_IN__("Custom Mode Bit Value (Default = 0)", uint64_t, 0);
	}
	else
	{
		__FE_COUTT__ << "Specific subsystem identified: " << subsystem << __E__;
		onBits_startBit = supportedSubsystems_.at(subsystem);
		onBits_bitCount = 1;
		onBits_value    = 1;
	}

	__FE_COUT__ << "onBits_startBit = " << onBits_startBit
	            << " onBits_bitCount = " << onBits_bitCount << " onBits_value = 0x"
	            << std::hex << onBits_value << __E__;

	const std::string SOURCE_BASE_PATH = std::string(__ENV__("OTSDAQ_DATA")) + "/";
	std::string       inFileName  = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.txt";
	std::string       outFileName = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.bin";
	result << __E__;  //space for readability
	result << "Generated Run Plan text file: <FILE>" << inFileName << "</FILE>" << __E__;
	result << "Compiled Run Plan binary file: <FILE>" << outFileName << "</FILE>"
	       << __E__;
	result << __E__;  //space for readability

	//generate Run Plan and write to input file for compiler
	// Note: as of 22-Feb-2026, Run Plan BRAM is 1024 ops
	//	Set Run Plan checks BRAM size indirectly, by reading back and validating the instruction set written!
	{
		result << "\n\nSubsystem '" << subsystem << "' joining with M:N ratio "
		       << mPartRatio << ":" << nPartRatio << " with mode bit parameters: "
		       << "\n\tonBits_startBit = " << onBits_startBit
		       << "\n\tonBits_bitCount = " << onBits_bitCount
		       << "\n\teventOffsetInLoop = " << eventOffsetInLoop
		       << "\n\tonBits_value = 0x" << std::hex << onBits_value << std::dec
		       << __E__;
		result << __E__;  //space for readability

		// Pass 1: write all updated AND masks first while suppressing new OR additions.
		// This avoids a window where OR bits could be interpreted before the new clear
		// points are in place in the always-running run plan feeder.
		generateSharedRunPlanWithPeriodicModeOn(
		    result,
		    inFileName,
		    0,                //initEventTag does not matter (already in loops)
		    onBits_startBit,  //start bit
		    onBits_bitCount,  //bit count
		    onBits_value,     //calo inject bit ON
		    mPartRatio,       // duty M in M:N on
		    nPartRatio,       // duty N in M:N on
		    eventOffsetInLoop,
		    std::to_string(eventDurationInClocks),  //eventDurationInClocks,
		    "clocks",                               //eventDurationSplitUnits
		    existingAndMasks,
		    existingOrMasks,
		    {},    //singleShotMasks
		    false  //applyPeriodicOrMasks
		);

		{
			CFOLib::CFO_Compiler compiler;
			compiler.processFile(inFileName, outFileName);
			result << SetRunplan(outFileName);
			result
			    << "Set preparation Run Plan for subsystem join with updated AND masks "
			       "and suppressed OR additions.\n";
		}

		// Pass 2: write AND+OR plan, now that AND safeguards are already present.
		generateSharedRunPlanWithPeriodicModeOn(
		    result,
		    inFileName,
		    0,                //initEventTag does not matter (already in loops)
		    onBits_startBit,  //start bit
		    onBits_bitCount,  //bit count
		    onBits_value,     //calo inject bit ON
		    mPartRatio,       // duty M in M:N on
		    nPartRatio,       // duty N in M:N on
		    eventOffsetInLoop,
		    std::to_string(eventDurationInClocks),  //eventDurationInClocks,
		    "clocks",                               //eventDurationSplitUnits
		    existingAndMasks,
		    existingOrMasks,
		    {},   //singleShotMasks
		    true  //applyPeriodicOrMasks
		);

		CFOLib::CFO_Compiler compiler;
		result << "\n\nRun Plan to join:\n"
		       << compiler.processFile(inFileName, outFileName);
		result << SetRunplan(outFileName);
	}  //end generate and set Run Plan to join

	result << "\n\nHere is the Run Plan Status after the join:\n";
	std::vector<FEVInterface::frontEndMacroArg_t> statusArgsIn, statusArgsOut;
	statusArgsOut.push_back(std::make_pair("Result", ""));
	runSelfFrontEndMacro("Shared Run Plan Get Status", statusArgsIn, statusArgsOut);
	result << statusArgsOut[0].second;

	result << "\n\nSubsystem '" << subsystem << "' successfully joined with M:N ratio "
	       << std::dec << mPartRatio << ":" << nPartRatio << " with mode bit parameters: "
	       << "\n\tonBits_startBit = " << onBits_startBit
	       << "\n\tonBits_bitCount = " << onBits_bitCount
	       << "\n\teventOffsetInLoop = " << eventOffsetInLoop << "\n\tonBits_value = 0x"
	       << std::hex << onBits_value << std::dec << __E__;

	__SET_ARG_OUT__("Result", result.str());
}  //end SharedRunPlanSubsystemJoin()

//========================================================================
/// SharedRunPlanSubsystemSingleShotJoin
///
/// Joins the Shared Run Plan for a one-time (single-shot) count of events using
/// OR_SINGLESHOT (opcode 111) instead of the repeating OR opcode. Because the
/// hardware fires OR_SINGLESHOT exactly once, the subsystem's mode bit(s) are
/// set for exactly the requested number of events and then permanently cleared
/// by the corresponding AND mask.
///
/// Single-shot Event Count is constructed from batches of fine and coarse loop counts:
///   the requested count is decomposed into one fine-loop batch (at most standardNValues_[1st]
///   events) and zero or more coarse-loop batches (each a multiple of standardNValues_[1st]),
///   matching the nesting structure of the Shared Run Plan loop hierarchy.
///
///   1 .. standardNValues_[1st]       : fine-loop OR_SINGLESHOT (count == standardNValues_[1st]
///                                      clears at the dedicated end-of-fine-loop AND slot)
///   standardNValues_[2nd..Nth]       : coarse loop OR_SINGLESHOT + coarse end-AND
///                                      for each entry n=2..N (N = standardNValues_.size())
void CFOFrontEndInterface::SharedRunPlanSubsystemSingleShotJoin(__ARGS__)
{
	if(!(thisCFO_->ReadBeamOnMode() || thisCFO_->ReadBeamOffMode()))
	{
		__SS__ << "Error: CFO is not currently in a Run Plan. Please do 'Shared Run Plan "
		          "Start' to start the shared Run Plan before adding subsystems."
		       << __E__;
		__SS_THROW__;
	}

	std::string subsystem =
	    __GET_ARG_IN__("Subsystem Name (CRV, Calo, Tracker, STM, ExtMon, Custom)",
	                   std::string,
	                   "Custom");
	std::string dutyCycle = __GET_ARG_IN__(
	    "Duty Cycle (% or M:N on:event ratio, Default = 100%)", std::string, "100%");

	std::stringstream result;
	result << "\nSingle-shot joining subsystem '" << subsystem
	       << "' to the Shared Run Plan..." << __E__;

	__FE_COUTV__(subsystem);
	if(supportedSubsystems_.find(subsystem) == supportedSubsystems_.end())
	{
		__FE_SS__ << "Specified subsystem '" << subsystem
		          << "' was not found in the set of supported subsystems: ";
		for(auto& subsystemPair : supportedSubsystems_)
			ss << "\t" << subsystemPair.first << __E__;
		__FE_SS_THROW__;
	}

	// Single-shot count can be any positive integer. Exact coarse counts use one coarse
	// chunk; larger/non-exact values are decomposed into repeated coarse/fine chunks.
	uint32_t singleShotCount = __GET_ARG_IN__(
	    "Single-shot Event Count (positive integer; larger values are split into "
	    "coarse/fine chunks)",
	    uint32_t,
	    1);
	__FE_COUTV__(singleShotCount);
	if(singleShotCount < 1)
	{
		__FE_SS__ << "Single-shot event count must be greater than 0." << __E__;
		__FE_SS_THROW__;
	}

	//now need to insert subsystem enable bit in run plan as single-shot
	//if custom, take user input, otherwise use subsystem valid bit
	uint16_t onBits_startBit = 0;
	uint16_t onBits_bitCount = 48;
	uint64_t onBits_value    = 0;

	if(subsystem == "Custom")  //custom mode bits!
	{
		__FE_COUTT__ << "Custom subsystem identified!" << __E__;
		onBits_startBit =
		    __GET_ARG_IN__("Custom Mode Bit Position (Default = 0)", uint16_t, 0);
		onBits_bitCount =
		    __GET_ARG_IN__("Custom Mode Bit Count (Default = 48)", uint16_t, 48);
		onBits_value = __GET_ARG_IN__("Custom Mode Bit Value (Default = 0)", uint64_t, 0);
	}
	else
	{
		__FE_COUTT__ << "Specific subsystem identified: " << subsystem << __E__;
		onBits_startBit = supportedSubsystems_.at(subsystem);
		onBits_bitCount = 1;
		onBits_value    = 1;
	}

	__FE_COUT__ << "onBits_startBit = " << onBits_startBit
	            << " onBits_bitCount = " << onBits_bitCount << " onBits_value = 0x"
	            << std::hex << onBits_value << __E__;

	//extract and/or op values to be modified in the single-shot join
	std::vector<uint64_t> existingAndMasks, existingOrMasks;
	uint64_t              eventDurationInClocks =
	    extractSharedRunPlanEventDuration(existingAndMasks, existingOrMasks);
	__FE_COUTV__(eventDurationInClocks);

	// Parse and resolve single-shot duty cycle using the same syntax/logic as periodic join.
	uint32_t mPartRatio = 0, nPartRatio = 0;
	if(dutyCycle.size() && dutyCycle[dutyCycle.size() - 1] == '%')
	{
		mPartRatio = std::strtoul(dutyCycle.c_str(), nullptr, 10);
		nPartRatio = 100;

		if(mPartRatio > 100)
		{
			__FE_SS__ << "Illegal duty cycle percentage '" << dutyCycle
			          << "'.. expecting a percentage less than or equal to 100%."
			          << __E__;
			__FE_SS_THROW__;
		}
	}
	else  //assume in M:N ratio format
	{
		std::vector<std::string> dutyCycleSplit =
		    StringMacros::getVectorFromString(dutyCycle, {':'});
		__FE_COUTV__(StringMacros::vectorToString(dutyCycleSplit));
		if(dutyCycleSplit.size() != 2)
		{
			__FE_SS__ << "Illegal duty cycle ratio '" << dutyCycle
			          << "'.. expecting M:N format, e.g. 1:200." << __E__;
			__FE_SS_THROW__;
		}
		mPartRatio = std::strtoul(dutyCycleSplit[0].c_str(), nullptr, 10);
		nPartRatio = std::strtoul(dutyCycleSplit[1].c_str(), nullptr, 10);
	}

	mnFixRatio(result, mPartRatio, nPartRatio);
	__FE_COUT__ << "Resolved single-shot duty cycle M:N = " << mPartRatio << ":"
	            << nPartRatio << __E__;

	if(nPartRatio > standardNValues_[0])
	{
		__FE_SS__ << "Single-shot duty cycle currently supports resolved N <= "
		          << standardNValues_[0] << ". Resolved M:N = " << mPartRatio << ":"
		          << nPartRatio << " from input '" << dutyCycle << "'." << __E__;
		__FE_SS_THROW__;
	}

	//48-bit pattern of bits this subsystem sets when active
	const uint64_t setBitsMask48 =
	    (onBits_value << onBits_startBit) &
	    (((uint64_t(1) << onBits_bitCount) - 1) << onBits_startBit);

	const size_t N_coarse = standardNValues_.size() - 1;

	// Build fine-loop ON-slot map from resolved duty cycle (eventOffsetInLoop = 0).
	std::vector<size_t> onFineSlots;
	onFineSlots.reserve(standardNValues_[0]);
	for(size_t i = 0; i < standardNValues_[0]; ++i)
	{
		const uint32_t finePhase100 = i;
		const uint32_t subFinePhase = nPartRatio ? (i % nPartRatio) : 0;
		bool           shouldSet =
		    (nPartRatio == standardNValues_[0] && finePhase100 < mPartRatio) ||
		    (nPartRatio < standardNValues_[0] && subFinePhase < mPartRatio);
		if(shouldSet)
			onFineSlots.push_back(i);
	}

	if(onFineSlots.empty())
	{
		__FE_SS__ << "Resolved duty cycle M:N = " << mPartRatio << ":" << nPartRatio
		          << " produced zero active fine-loop slots for single-shot join."
		          << __E__;
		__FE_SS_THROW__;
	}

	const uint32_t activePerFinePass = static_cast<uint32_t>(onFineSlots.size());
	const bool     isFullDutyCycle   = (mPartRatio == nPartRatio);

	// Build available coarse batch sizes (from largest to smallest, excluding fine loop size).
	// Track both event count and coarse OR_SINGLESHOT mask index for each size.
	std::vector<uint32_t> coarseBatchSizes;
	std::vector<int32_t>  coarseBatchMaskIndices;
	for(size_t i = standardNValues_.size() - 1; i > 0; --i)
	{
		coarseBatchSizes.push_back(standardNValues_[i]);
		coarseBatchMaskIndices.push_back(
		    static_cast<int32_t>(standardNValues_.size() - 1 - i));
	}

	// Decompose the requested count into chunks.
	// For duty < 100%, only fine chunks are allowed so the requested duty is respected.
	// For full duty, coarse chunks are allowed and chosen greedily largest-first.
	std::vector<uint32_t> chunkCounts;
	// For each chunk: -1 means fine chunk; >=0 is coarse OR_SINGLESHOT mask index.
	std::vector<int32_t> chunkCoarseMaskIndices;
	uint32_t             remainingCount = singleShotCount;

	while(remainingCount > 0)
	{
		uint32_t chunkCount          = remainingCount;  // default: use entire remainder
		bool     isCoarse            = false;
		int32_t  coarseMaskIndexUsed = -1;

		// Try to find a coarse batch size that fits (full duty only).
		if(isFullDutyCycle)
			for(size_t i = 0; i < coarseBatchSizes.size(); ++i)
			{
				uint32_t coarseSize = coarseBatchSizes[i];
				if(coarseSize <= remainingCount)
				{
					chunkCount          = coarseSize;
					isCoarse            = true;
					coarseMaskIndexUsed = coarseBatchMaskIndices[i];
					break;
				}
			}

		// If no coarse batch fits and remainder > activePerFinePass, use active fine batch
		if(!isCoarse && chunkCount > activePerFinePass)
		{
			chunkCount = activePerFinePass;
		}

		// Final chunk (remainder) uses duty-filtered fine slots
		chunkCounts.push_back(chunkCount);
		chunkCoarseMaskIndices.push_back(isCoarse ? coarseMaskIndexUsed : -1);
		remainingCount -= chunkCount;
	}

	if(chunkCounts.size() > 1)
	{
		result << "Requested single-shot count " << singleShotCount << " with duty "
		       << mPartRatio << ":" << nPartRatio << " will be executed in "
		       << chunkCounts.size() << " chunks: ";
		for(size_t i = 0; i < chunkCounts.size(); ++i)
			result << (i ? ", " : "") << chunkCounts[i]
			       << (chunkCoarseMaskIndices[i] >= 0 ? "(coarse)" : "(fine)");
		result << __E__;
	}
	if(!isFullDutyCycle)
		result
		    << "Duty is below 100%; single-shot chunking is constrained to fine batches "
		    << "to preserve duty-cycle semantics." << __E__;

	//========================================================================
	/// local lambda function configureSingleShotChunk
	auto configureSingleShotChunk = [&](uint32_t               chunkIndex,
	                                    uint32_t               chunkCount,
	                                    std::vector<uint64_t>& chunkAndMasks,
	                                    std::vector<uint64_t>& chunkSingleShotMasks,
	                                    std::stringstream&     chunkLog) {
		chunkAndMasks = existingAndMasks;
		chunkSingleShotMasks.assign(N_coarse + standardNValues_[0], 0ULL);

		if(chunkIndex >= chunkCoarseMaskIndices.size())
		{
			__FE_SS__ << "Internal error: invalid single-shot chunk index " << chunkIndex
			          << " (chunkCoarseMaskIndices size=" << chunkCoarseMaskIndices.size()
			          << ")." << __E__;
			__FE_SS_THROW__;
		}

		const int32_t coarseMaskIndex = chunkCoarseMaskIndices[chunkIndex];
		if(coarseMaskIndex >= 0)
		{
			size_t coarseIdx = static_cast<size_t>(coarseMaskIndex);
			if(coarseIdx >= N_coarse)
			{
				__FE_SS__ << "Internal error: coarse mask index out of range "
				          << coarseIdx << " (N_coarse=" << N_coarse << ")." << __E__;
				__FE_SS_THROW__;
			}

			// Map coarse OR mask index back to loop level l and its coarse-end AND slot.
			size_t coarseLevelL    = standardNValues_.size() - 1 - coarseIdx;
			size_t coarseEndAndIdx = standardNValues_[0] + coarseLevelL;
			if(coarseEndAndIdx >= chunkAndMasks.size())
			{
				__FE_SS__ << "Internal error: coarse end-AND index out of range "
				          << coarseEndAndIdx << " (andMasks size=" << chunkAndMasks.size()
				          << ")." << __E__;
				__FE_SS_THROW__;
			}

			// Coarse chunk behavior:
			//  - force this subsystem bit to be KEPT at all AND points
			//  - arm OR_SINGLESHOT at the targeted coarse loop level
			//  - clear this subsystem only at that level's end-AND boundary
			// This yields exactly one coarse-sized contiguous pulse (e.g. 2000 events).
			for(auto& mask : chunkAndMasks)
				mask |= setBitsMask48;

			chunkSingleShotMasks[coarseIdx] = setBitsMask48;
			chunkAndMasks[coarseEndAndIdx] &= ~setBitsMask48;

			chunkLog << "Using coarse-batch chunk N=" << chunkCount
			         << " with coarse OR_SINGLESHOT index " << coarseIdx
			         << " (loop N=" << standardNValues_[coarseLevelL]
			         << "): keep forced at all AND points, clear applied only at coarse "
			            "end-AND index "
			         << coarseEndAndIdx << "." << __E__;
		}
		else
		{
			// Fine chunk behavior: clear at all AND points so each selected fine-slot
			// OR_SINGLESHOT pulse only affects its intended event window.
			for(auto& mask : chunkAndMasks)
				mask &= ~setBitsMask48;

			// Fine batch: fire OR_SINGLESHOT at duty-filtered fine slots
			if(chunkCount < 1 || chunkCount > onFineSlots.size())
			{
				__FE_SS__
				    << "Internal error: unsupported duty-based fine-batch chunk count "
				    << chunkCount << " (activePerFinePass=" << onFineSlots.size() << ")."
				    << __E__;
				__FE_SS_THROW__;
			}

			for(size_t k = 0; k < chunkCount; ++k)
			{
				size_t fineSlot                           = onFineSlots[k];
				chunkSingleShotMasks[N_coarse + fineSlot] = setBitsMask48;
			}
			chunkLog << "Using duty-based fine-batch chunk N=" << chunkCount
			         << " (activePerFinePass=" << onFineSlots.size() << ")"
			         << ": OR_SINGLESHOT set at " << chunkCount
			         << " fine locations, AND clears applied at all loop AND points."
			         << __E__;
		}
	};  //end configureSingleShotChunk()

	//========================================================================
	/// local lamda function areSingleShotValuesCleared
	auto areSingleShotValuesCleared = [&](std::string& unclearedDetails) {
		std::map<uint32_t /* address */,
		         std::pair<uint32_t /* expected */, uint32_t /* actual */>>
		            mismatches;
		std::string binaryContents(sharedRunPlanSize_, static_cast<char>(0xFF));
		thisCFO_->CompareRunPlanData(binaryContents, 0 /* address */, mismatches);

		bool              foundSingleShot = false;
		bool              allCleared      = true;
		uint32_t          loDataWord      = 0;
		std::stringstream details;
		for(const auto& mismatch : mismatches)
		{
			if(mismatch.first % 2 == 0)
			{
				loDataWord = mismatch.second.second;
				continue;
			}

			uint32_t hiDataWord = mismatch.second.second;
			uint8_t  opCode     = (hiDataWord >> 24) & 0xFF;
			if(opCode != static_cast<uint8_t>(
			                 CFOLib::CFO_Compiler::CFO_INSTR::OR_SINGLESHOT_MODE_BITS))
				continue;

			foundSingleShot = true;
			uint64_t data48 = static_cast<uint64_t>(loDataWord) |
			                  (static_cast<uint64_t>(hiDataWord & 0xFFFF) << 32);
			if(data48 != 0)
			{
				allCleared = false;
				details << (details.tellp() > 0 ? ", " : "") << "line#" << std::dec
				        << mismatch.first / 2 << "=0x" << std::hex << data48 << std::dec;
			}
		}

		if(!foundSingleShot)
		{
			__FE_SS__
			    << "Failed to locate OR_SINGLESHOT instructions in run plan readback."
			    << __E__;
			__FE_SS_THROW__;
		}

		unclearedDetails = details.str();
		return allCleared;
	};  //end lamda areSingleShotValuesCleared()

	//========================================================================
	/// local lamda function waitForSingleShotReady
	auto waitForSingleShotReady = [&](uint32_t chunkCount, size_t chunkIndex) {
		// Batch completion is defined as one full fine loop for sub-100% duty,
		// or the exact coarse/full-duty chunk size otherwise.
		uint32_t expectedEventCount =
		    isFullDutyCycle ? chunkCount : static_cast<uint32_t>(standardNValues_[0]);

		// Read the 48-bit current tag to detect batch completion without 16-bit wraparound.
		uint64_t initialRunPlanTag = thisCFO_->ReadRunPlanCurrentTag();

		// Use expectedEventCount so timing aligns with the completion definition
		double chunkDurationUs = static_cast<double>(expectedEventCount) *
		                         eventDurationInClocks *
		                         CFOandDTCCoreVInterface::FPGAClock_ / 1000.0;
		uint64_t pollIntervalUs = chunkDurationUs < 20000.0
		                              ? 1000
		                              : (chunkDurationUs < 200000.0 ? 5000 : 50000);
		uint64_t maxWaitUs      = static_cast<uint64_t>(chunkDurationUs * 1.2) + 5000000;

		std::string unclearedDetails;
		bool        singleShotCleared = false;
		uint64_t    eventCountDelta   = 0;

		for(uint64_t waitedUs = 0; waitedUs <= maxWaitUs; waitedUs += pollIntervalUs)
		{
			// Check condition 1: OR_SINGLESHOT values are cleared
			singleShotCleared = areSingleShotValuesCleared(unclearedDetails);

			// Check condition 2: current run-plan tag has advanced by the expected number of events.
			uint64_t currentRunPlanTag = thisCFO_->ReadRunPlanCurrentTag();
			eventCountDelta            = currentRunPlanTag - initialRunPlanTag;
			uint64_t expectedEventCountDelta =
			    expectedEventCount *
			    2;  //allow for the possibility of the worst case current position when the batch starts, and have to walk through entire coarse or fine loop before starting batch
			bool eventCountValid = (eventCountDelta >= expectedEventCountDelta);

			if(singleShotCleared && eventCountValid)
			{
				if(chunkIndex > 25 && chunkIndex != chunkCounts.size() - 1)
					result << ".";  //add a single dot, when too many
				else
				{
					if(chunkIndex == chunkCounts.size() - 1)
						result << "\n\n";  //end spacer
					result << "Chunk " << (chunkIndex + 1) << "/" << chunkCounts.size()
					       << " completed; OR_SINGLESHOT readback values cleared and "
					       << "run-plan current tag increased by " << eventCountDelta
					       << " (expected " << expectedEventCountDelta << ") after "
					       << waitedUs / 1000.0 << " ms." << __E__;
				}
				return;
			}

			usleep(pollIntervalUs);
		}

		// Timeout: report final state
		__FE_SS__ << "Timed out waiting for chunk " << (chunkIndex + 1) << "/"
		          << chunkCounts.size() << " (count=" << chunkCount << ") to complete. ";
		if(!singleShotCleared)
			ss << "OR_SINGLESHOT values not cleared: "
			   << (unclearedDetails.empty() ? std::string("<none reported>")
			                                : unclearedDetails)
			   << " ";
		ss << "Run-plan current tag delta=" << eventCountDelta << " (expected "
		   << expectedEventCount << ")." << __E__;
		__FE_SS_THROW__;
	};  //end lamda waitForSingleShotReady()

	const std::string SOURCE_BASE_PATH = std::string(__ENV__("OTSDAQ_DATA")) + "/";
	std::string       inFileName  = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.txt";
	std::string       outFileName = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.bin";
	result << __E__;  //space for readability
	result << "Generated Run Plan text file: <FILE>" << inFileName << "</FILE>" << __E__;
	result << "Compiled Run Plan binary file: <FILE>" << outFileName << "</FILE>"
	       << __E__;
	result << __E__;  //space for readability

	//generate and set one run plan per chunk, waiting for OR_SINGLESHOT auto-clear
	//between launches before arming the next chunk.
	result << "\n\nSubsystem '" << subsystem << "' single-shot joining for "
	       << singleShotCount << " events with mode bit parameters: "
	       << "\n\tonBits_startBit = " << onBits_startBit
	       << "\n\tonBits_bitCount = " << onBits_bitCount << "\n\tonBits_value = 0x"
	       << std::hex << onBits_value << std::dec << __E__;
	result << __E__;

	uint64_t runningCount = 0;
	for(size_t chunkIndex = 0; chunkIndex < chunkCounts.size(); ++chunkIndex)
	{
		std::vector<uint64_t> chunkAndMasks;
		std::vector<uint64_t> singleShotMasks;
		std::vector<uint64_t> prepSingleShotMasks(N_coarse + standardNValues_[0], 0ULL);

		if(chunkIndex > 25 && chunkIndex != chunkCounts.size() - 1)
		{
			if(chunkIndex == 26)
				result << "\n";  //start spacer
			result << ".";       //add a single dot, when too many
		}
		else
			result << "\n*** Launching chunk " << (chunkIndex + 1) << "/"
			       << chunkCounts.size() << " with count " << chunkCounts[chunkIndex]
			       << " (events so far = " << runningCount << ")" << __E__;
		runningCount += chunkCounts[chunkIndex];

		std::stringstream subResult;

		configureSingleShotChunk(chunkIndex,
		                         chunkCounts[chunkIndex],
		                         chunkAndMasks,
		                         singleShotMasks,
		                         subResult);

		generateSharedRunPlanWithPeriodicModeOn(
		    subResult,
		    inFileName,
		    0,  //initEventTag ignored once run plan is looping
		    0,  //onBits_startBit unused (onBits_value = 0)
		    1,  //onBits_bitCount unused (onBits_value = 0)
		    0,  //onBits_value = 0 (no periodic OR additions)
		    1,  //mPartRatio (no-op because onBits_value = 0)
		    1,  //nPartRatio (no-op because onBits_value = 0)
		    0,  //eventOffsetInLoop (no-op because onBits_value = 0)
		    std::to_string(eventDurationInClocks),
		    "clocks",
		    chunkAndMasks,
		    existingOrMasks,
		    prepSingleShotMasks);

		{
			CFOLib::CFO_Compiler compiler;
			compiler.processFile(inFileName, outFileName);
			SetRunplan(outFileName);

			if(chunkIndex == 0)
				result << "Set preparation Run Plan for single-shot chunk "
				       << (chunkIndex + 1) << " (with " << chunkCounts.size() - 1
				       << " chunks to follow)"
				       << " with updated AND masks and cleared OR_SINGLESHOT bits.\n";
		}

		generateSharedRunPlanWithPeriodicModeOn(
		    subResult,
		    inFileName,
		    0,  //initEventTag ignored once run plan is looping
		    0,  //onBits_startBit unused (onBits_value = 0)
		    1,  //onBits_bitCount unused (onBits_value = 0)
		    0,  //onBits_value = 0 (no periodic OR additions)
		    1,  //mPartRatio (no-op because onBits_value = 0)
		    1,  //nPartRatio (no-op because onBits_value = 0)
		    0,  //eventOffsetInLoop (no-op because onBits_value = 0)
		    std::to_string(eventDurationInClocks),
		    "clocks",
		    chunkAndMasks,
		    existingOrMasks,
		    singleShotMasks);

		{
			CFOLib::CFO_Compiler compiler;
			compiler.processFile(inFileName, outFileName);
			subResult << SetRunplan(outFileName);
			subResult
			    << "Set armed Run Plan for single-shot chunk " << (chunkIndex + 1)
			    << " (with " << chunkCounts.size() - 1 << " chunks to follow)"
			    << " with the same AND masks and the requested OR_SINGLESHOT bits.\n";
		}

		if(chunkIndex == 0)
			result << subResult.str();
		waitForSingleShotReady(chunkCounts[chunkIndex], chunkIndex);
	}  //end chunk loop

	result << "\n\nHere is the Run Plan Status after the single-shot join:\n";
	std::vector<FEVInterface::frontEndMacroArg_t> statusArgsIn, statusArgsOut;
	statusArgsOut.push_back(std::make_pair("Result", ""));
	runSelfFrontEndMacro("Shared Run Plan Get Status", statusArgsIn, statusArgsOut);
	result << statusArgsOut[0].second;

	result << "\n\nSubsystem '" << subsystem << "' successfully single-shot joined for "
	       << std::dec << singleShotCount << " events with mode bit parameters: "
	       << "\n\tonBits_startBit = " << onBits_startBit
	       << "\n\tonBits_bitCount = " << onBits_bitCount << "\n\tonBits_value = 0x"
	       << std::hex << onBits_value << std::dec << __E__;

	__SET_ARG_OUT__("Result", result.str());
}  //end SharedRunPlanSubsystemSingleShotJoin()

//========================================================================
void CFOFrontEndInterface::SharedRunPlanSubsystemLeave(__ARGS__)
{
	if(!(thisCFO_->ReadBeamOnMode() || thisCFO_->ReadBeamOffMode()))
	{
		__SS__
		    << "Error: CFO is not currently in a Run Plan. No active Run Plan to leave!"
		    << __E__;
		__SS_THROW__;
	}

	std::string subsystem =
	    __GET_ARG_IN__("Subsystem Name (CRV, Calo, Tracker, STM, ExtMon, Custom)",
	                   std::string,
	                   "Custom");

	std::stringstream result;
	result << "\nRemoving subsystem '" << subsystem << "' from the Shared Run Plan..."
	       << __E__;

	__FE_COUTV__(subsystem);
	if(supportedSubsystems_.find(subsystem) == supportedSubsystems_.end())
	{
		__FE_SS__ << "Specified subsystem '" << subsystem
		          << "' was not found in the set of supported subsystems: ";
		for(auto& subsystemPair : supportedSubsystems_)
			ss << "\t" << subsystemPair.first << __E__;
		__FE_SS_THROW__;
	}

	//extract and/or op values to be modified in the leave
	std::vector<uint64_t> existingAndMasks, existingOrMasks;
	uint64_t              eventDurationInClocks =
	    extractSharedRunPlanEventDuration(existingAndMasks, existingOrMasks);
	__FE_COUTV__(eventDurationInClocks);

	//now need to remove subsystems enable bit in run plan
	//if custom, take user input, otherwise use subsystem valid bit
	uint16_t offBits_startBit = 0;
	uint16_t offBits_bitCount = 48;
	uint64_t offBits_value    = 0;

	if(subsystem == "Custom")  //custom mode bits!
	{
		__FE_COUTT__ << "Custom subsystem identified!" << __E__;
		offBits_startBit =
		    __GET_ARG_IN__("Custom Mode Bit Position (Default = 0)", uint16_t, 0);
		offBits_bitCount =
		    __GET_ARG_IN__("Custom Mode Bit Count (Default = 48)", uint16_t, 48);
	}
	else
	{
		__FE_COUTT__ << "Specific subsystem identified: " << subsystem << __E__;
		offBits_startBit = supportedSubsystems_.at(subsystem);
		offBits_bitCount = 1;
	}

	__FE_COUT__ << "offBits_startBit = " << offBits_startBit
	            << " offBits_bitCount = " << offBits_bitCount << " offBits_value = 0x"
	            << std::hex << offBits_value << __E__;

	const std::string SOURCE_BASE_PATH = std::string(__ENV__("OTSDAQ_DATA")) + "/";
	std::string       inFileName  = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.txt";
	std::string       outFileName = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.bin";
	result << __E__;  //space for readability
	result << "Generated Run Plan text file: <FILE>" << inFileName << "</FILE>" << __E__;
	result << "Compiled Run Plan binary file: <FILE>" << outFileName << "</FILE>"
	       << __E__;
	result << __E__;  //space for readability

	//generate Run Plan and write to input file for compiler
	// Note: as of 22-Feb-2026, Run Plan BRAM is 1024 ops
	//	Set Run Plan checks BRAM size indirectly, by reading back and validating the instruction set written!
	{
		result << "\n\nSubsystem '" << subsystem
		       << "' leaving with mode off bit parameters: "
		       << "\n\toffBits_startBit = " << offBits_startBit
		       << "\n\toffBits_bitCount = " << offBits_bitCount
		       << "\n\toffBits_value = 0x" << std::hex << offBits_value << std::dec
		       << __E__;
		result << __E__;  //space for readability
		generateSharedRunPlanWithPeriodicModeOff(
		    result,
		    inFileName,
		    offBits_startBit,                       //start bit
		    offBits_bitCount,                       //bit count
		    std::to_string(eventDurationInClocks),  //eventDurationInClocks,
		    "clocks",                               //eventDurationSplitUnits
		    existingAndMasks,
		    existingOrMasks);

		CFOLib::CFO_Compiler compiler;
		result << "\n\nRun Plan to join:\n"
		       << compiler.processFile(inFileName, outFileName);
		result << SetRunplan(outFileName);
	}  //end generate and set Run Plan to join

	result << "\nSubsystem '" << subsystem
	       << "' successfully removed from the Shared Run Plan with mode bit parameters: "
	       << "\n\toffBits_startBit = " << std::dec << offBits_startBit
	       << "\n\toffBits_bitCount = " << offBits_bitCount << "\n\toffBits_value = 0x"
	       << std::hex << offBits_value << std::dec << __E__;

	__SET_ARG_OUT__("Result", result.str());
}  //end SharedRunPlanSubsystemLeave()

//========================================================================
void CFOFrontEndInterface::parseEventDurationForRunPlan(const std::string& eventDuration,
                                                        std::string&       durationValue,
                                                        std::string&       durationUnits)
{
	__FE_COUTV__(eventDuration);
	bool   foundUnits = false;
	size_t i;
	for(i = 0; i < eventDuration.size(); ++i)
		if(eventDuration[i] == 's' || eventDuration[i] == 'm' ||
		   eventDuration[i] == 'u' || eventDuration[i] == 'n' || eventDuration[i] == 'c')
		{
			foundUnits = true;
			break;
		}

	if(!foundUnits)
	{
		__FE_SS__ << "No units were found in the input parameters 'Fixed-width "
		             "Event Window Duration' value: "
		          << eventDuration
		          << ". Please use units when specifying event window duration "
		             "(s, ms, us, ns, and clocks are allowed). For example "
		             "'1.7us' or '1675ns' would be valid."
		          << __E__;
		__FE_SS_THROW__;
	}
	durationValue = eventDuration.substr(0, i);
	durationUnits = eventDuration.substr(i);
	__FE_COUTV__(durationValue);
	__FE_COUTV__(durationUnits);
}  //end parseEventDurationForRunPlan()

//========================================================================
// return M:N ratio of M events on per N events
void CFOFrontEndInterface::mnFixRatio(std::stringstream& logResult,
                                      uint32_t&          mPartRatio,
                                      uint32_t&          nPartRatio)
{
	logResult << "Resolving input ratio M:N = " << mPartRatio << ":" << nPartRatio
	          << " to N in  {";

	bool first = true;
	for(uint32_t n : standardNValues_)
		if(!first)
			logResult << ", " << n;
		else
		{
			logResult << n;
			first = false;
		}

	logResult << "}..." << __E__;

	// Sub-fine ratio: nPartRatio < standardNValues_[0] and evenly divides it.
	// These repeat the M:N pattern standardNValues_[0]/nPartRatio times per super-cycle
	// (e.g. 1:2 → ON,OFF,ON,OFF,... 50 times). Skip standard resolution for these.
	if(nPartRatio > 0 && nPartRatio < standardNValues_[0])
	{
		if(mPartRatio == 0 || mPartRatio > nPartRatio)
		{
			__FE_SS__ << "Invalid sub-fine ratio M:N = " << mPartRatio << ":"
			          << nPartRatio << ". M must satisfy 0 < M <= N." << __E__;
			__FE_SS_THROW__;
		}
		if(standardNValues_[0] % nPartRatio != 0)
		{
			__FE_SS__ << "Invalid sub-fine ratio M:N = " << mPartRatio << ":"
			          << nPartRatio << ". N must satisfy " << standardNValues_[0]
			          << " % N = 0." << __E__;
			__FE_SS_THROW__;
		}
		logResult << "Sub-fine ratio accepted: M:N = " << mPartRatio << ":" << nPartRatio
		          << " repeats " << standardNValues_[0] / nPartRatio
		          << " times per super-cycle." << __E__;
		return;
	}

	double targetRatio = static_cast<double>(mPartRatio) / nPartRatio;

	getRatioOfOnPerEvents(nPartRatio, mPartRatio, mPartRatio, nPartRatio);

	double actualRatio = static_cast<double>(mPartRatio) / nPartRatio;
	double errPct      = std::abs(actualRatio - targetRatio) / targetRatio * 100;
	logResult << "\nResolved input ratio to " << mPartRatio << ":" << nPartRatio
	          << ". Target Ratio = " << targetRatio << ", Actual Ratio = " << actualRatio
	          << ", Err Pct = " << errPct << " %" << __E__;
	__FE_COUT__ << logResult.str() << __E__;

	// Validate that N is a standard value
	bool validN = false;
	for(uint32_t n : standardNValues_)
	{
		if(nPartRatio == n)
		{
			validN = true;
			break;
		}
	}

	if(!validN)
	{
		__FE_SS__ << "Failed to resolve target ratio M:N = " << mPartRatio << ":"
		          << nPartRatio << __E__;
		ss << "\n\n" << logResult.str() << __E__;
		__FE_SS_THROW__;
	}

	if(nPartRatio != standardNValues_[0] && mPartRatio != 1)
	{
		__FE_SS__ << "Invalid M:N ratio: " << mPartRatio << ":" << nPartRatio
		          << ". For N > " << standardNValues_[0]
		          << ", M must be 1 to ensure periodicity." << __E__;
		ss << "\n\n" << logResult.str() << __E__;
		__FE_SS_THROW__;
	}
}  //end mnFixRatio()

//========================================================================
// return M:N ratio of M events on per N events
void CFOFrontEndInterface::getRatioOfOnPerEvents(uint32_t  clocksPerOn,
                                                 uint32_t  clocksPerEvent,
                                                 uint32_t& mPartRatio,
                                                 uint32_t& nPartRatio)
{
	if(clocksPerEvent == 0)
	{
		__FE_SS__ << "Invalid clocksPerEvent value: " << clocksPerEvent << __E__;
		__FE_SS_THROW__;
	}
	if(clocksPerOn == 0)
	{
		__FE_SS__ << "Invalid clocksPerOn value: " << clocksPerOn << __E__;
		__FE_SS_THROW__;
	}

	double eventsPerOn =  // (events / on) = (clocks / on) * (event / clocks)
	    static_cast<double>(clocksPerOn) / static_cast<double>(clocksPerEvent);
	__FE_COUTV__(clocksPerOn);
	__FE_COUTV__(clocksPerEvent);
	__FE_COUTV__(eventsPerOn);

	// Calculate M:N ratio with N locked to standard values
	mPartRatio = 0;
	nPartRatio = 0;

	double targetRatio = 1 / eventsPerOn;  // (on / event) = 1 / (events / on)
	__FE_COUTV__(targetRatio);

	for(uint32_t n : standardNValues_)
	{
		uint32_t m = targetRatio * n;
		__FE_COUTT__ << "n = " << n << ", m (rounded) = " << m << __E__;
		if(m > 0)  // ensure M is valid (M <= N)
		{
			if(n > standardNValues_[0])
				mPartRatio = 1;
			else
				mPartRatio = m;
			nPartRatio = n;
			break;
		}
	}  //end search loop for best value

	if(mPartRatio == 0)
	{
		__FE_COUT_WARN__ << "Target ratio is too low to achieve with standard N values. "
		                    "Setting M:N ratio to 1:"
		                 << standardNValues_.back() << __E__;
		mPartRatio = 1;
		nPartRatio = standardNValues_.back();
	}

	__FE_COUTV__(mPartRatio);
	__FE_COUTV__(nPartRatio);

	double actualRatio = static_cast<double>(mPartRatio) / nPartRatio;
	double errPct      = std::abs(actualRatio - targetRatio) / targetRatio * 100;

	__FE_COUT__ << "M:N ratio = " << mPartRatio << ":" << nPartRatio
	            << " (target ratio = " << targetRatio << ", errPct = " << errPct << " %)"
	            << __E__;
}  //end getRatioOfOnPerEvents()

//========================================================================
// Generates the Share Run Plan text code to implement a
//	periodic mode ON pattern with the specified M:N ratio of M events ON per N events,
//	and writes to the specified file.
//
// The concept is that the Shared Run Plan ops never change
//	only the AND and OR parameters change to add/remove bits
void CFOFrontEndInterface::generateSharedRunPlanWithPeriodicModeOn(
    std::stringstream&           logResult,
    std::string&                 genFilename,
    const uint64_t               initEventTag,
    const uint16_t               onBits_startBit,
    const uint16_t               onBits_bitCount,
    const uint64_t               onBits_value,
    uint32_t                     mPartRatio,
    uint32_t                     nPartRatio,
    uint32_t                     eventOffsetInLoop,
    const std::string&           eventDurationSplitNumber,
    const std::string&           eventDurationSplitUnits,
    const std::vector<uint64_t>& existingAndMasks,
    const std::vector<uint64_t>& existingOrMasks,
    const std::vector<uint64_t>& singleShotMasks,
    const bool                   applyPeriodicOrMasks)
{
	__FE_COUTV__(mPartRatio);
	__FE_COUTV__(nPartRatio);
	__FE_COUTV__(eventOffsetInLoop);
	if(!onBits_value && mPartRatio == 1 && nPartRatio == 1)
		__FE_COUT__ << "Ignoring m:n ratio with no on-bits." << __E__;
	else
		mnFixRatio(logResult, mPartRatio, nPartRatio);
	__FE_COUTV__(mPartRatio);
	__FE_COUTV__(nPartRatio);

	const size_t N_coarse = standardNValues_.size() - 1;
	if(existingAndMasks.size() != standardNValues_[0] + 1 + N_coarse ||
	   existingOrMasks.size() != N_coarse + standardNValues_[0])
	{
		__FE_SS__ << "existingAndMasks and existingOrMasks must each have size "
		          << standardNValues_[0] + 1 + N_coarse << " and "
		          << N_coarse + standardNValues_[0]
		          << " respectively. Got andMasks=" << existingAndMasks.size()
		          << " orMasks=" << existingOrMasks.size() << __E__;
		__FE_SS_THROW__;
	}

	if(singleShotMasks.size() && singleShotMasks.size() != N_coarse + standardNValues_[0])
	{
		__FE_SS__ << "singleShotMasks must have size " << (N_coarse + standardNValues_[0])
		          << " when provided. Got singleShotMasks=" << singleShotMasks.size()
		          << __E__;
		__FE_SS_THROW__;
	}

	//48-bit pattern of bits this subsystem sets when ON
	const uint64_t setBitsMask48 =
	    (onBits_value << onBits_startBit) &
	    (((uint64_t(1) << onBits_bitCount) - 1) << onBits_startBit);

	std::stringstream out;
	std::string       tabStr, commentStr;
	OUT << "SET_TAG " << initEventTag << __E__;
	OUT << "LABEL //for infinite loop" << __E__;
	PUSHTAB;
	{  // start infinite loop ops

		//strategy for duty cycle is just have standardNValues_[0] positions (i.e. granularity of 1%)
		// but allow 1 in 200, 500, 1000, etc. coarse granularity

		//coarse granularity loops
		//  existingOrMasks[coarseIdx] hold the existing coarse OR masks (before LOOP)
		//  OR_SINGLESHOT_MODE_BITS values are transient and always 0 (already fired/cleared)
		//  existingAndMasks[standardNValues_[0]+(l-1)] hold the existing coarse end-AND masks (after DO_LOOP)
		for(size_t l = standardNValues_.size() - 1; l > 0; --l)
		{
			size_t   coarseIdx = standardNValues_.size() - 1 - l;  //0 for outermost loop
			uint32_t loopN     = standardNValues_[l] / standardNValues_[l - 1];
			__FE_COUTTV__(loopN);

			uint64_t coarseSingleShot =
			    singleShotMasks.size() ? singleShotMasks[coarseIdx] : 0ULL;
			OUT << "OR_SINGLESHOT_MODE_BITS start_bit= 0 bit_count= 48 value= 0x"
			    << std::hex << coarseSingleShot << std::dec << __E__;

			uint64_t new_coarse_or =
			    (applyPeriodicOrMasks && standardNValues_[l] == nPartRatio)
			        ? setBitsMask48
			        : 0ULL;
			uint64_t merged_coarse_or = existingOrMasks[coarseIdx] | new_coarse_or;
			OUT << "OR_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
			    << merged_coarse_or << std::dec << __E__;

			__FE_COUTT__ << "LOOP " << loopN << " // for N = " << standardNValues_[l]
			             << __E__;
			OUT << "LOOP " << loopN << __E__;
			PUSHTAB;
		}  //end coarse granularity loops

		//now fine granularity loop ------

		//  existingAndMasks[i] and existingOrMasks[N_coarse+i] are the existing fine masks
		for(size_t i = 0; i < standardNValues_[0]; ++i)
		{
			size_t fineIdx = N_coarse + i;

			// Shift the fine-loop phase by eventOffsetInLoop so that "ON window" starts at
			// i = eventOffsetInLoop (mod 100). This phase is then used by both decisions:
			//  - shouldSet  => finePhase100 <  M (OR applies during ON portion)
			//  - shouldClear=> finePhase100 >= M (AND clears during OFF portion)
			uint32_t finePhase100 =
			    (i + standardNValues_[0] - (eventOffsetInLoop % standardNValues_[0])) %
			    standardNValues_[0];
			uint32_t subFineOffset = nPartRatio ? (eventOffsetInLoop % nPartRatio) : 0;
			uint32_t subFinePhase =
			    nPartRatio ? ((i + nPartRatio - subFineOffset) % nPartRatio) : 0;

			//AND: clear this subsystem's bits during OFF positions; merge with existing mask.
			// sub-fine (nPartRatio < standardNValues_[0], factor of it): repeating M:N pattern, clear when position within period >= M
			// fine      (nPartRatio == standardNValues_[0])             : clear when offset-adjusted phase >= M
			// coarse    (nPartRatio > standardNValues_[0])              : clear every i > 0 (bit was set by outer LOOP's OR)
			bool shouldClear =
			    (nPartRatio > standardNValues_[0] && i > 0) ||
			    (nPartRatio == standardNValues_[0] && finePhase100 >= mPartRatio) ||
			    (nPartRatio < standardNValues_[0] && subFinePhase >= mPartRatio);
			uint64_t new_and =
			    shouldClear ? (0xFFFFFFFFFFFFULL & ~setBitsMask48) : 0xFFFFFFFFFFFFULL;
			uint64_t merged_and = existingAndMasks[i] & new_and;
			OUT << "AND_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
			    << merged_and << std::dec
			    << __E__;  // bit positions with 1 keep, 0 remove

			//OR: set this subsystem's bits during ON positions; merge with existing mask.
			// fine:     first M of N events, with optional phase offset
			// sub-fine: repeating M:N pattern, with optional phase offset
			bool shouldSet =
			    applyPeriodicOrMasks &&
			    ((nPartRatio == standardNValues_[0] && finePhase100 < mPartRatio) ||
			     (nPartRatio < standardNValues_[0] && subFinePhase < mPartRatio));
			uint64_t new_or    = shouldSet ? setBitsMask48 : 0ULL;
			uint64_t merged_or = existingOrMasks[fineIdx] | new_or;
			OUT << "OR_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
			    << merged_or << std::dec << __E__;

			// Apply fine-slot OR_SINGLESHOT after clear/set mask ops so the pulse is
			// present for this event window's HEARTBEAT.
			uint64_t fineSingleShot =
			    singleShotMasks.size() ? singleShotMasks[fineIdx] : 0ULL;
			OUT << "OR_SINGLESHOT_MODE_BITS start_bit= 0 bit_count= 48 value= 0x"
			    << std::hex << fineSingleShot << std::dec << __E__;

			OUT << "HEARTBEAT event_mode = registered // use existing run mode" << __E__;
			OUT << "MARKER" << __E__;
			OUT << "WAIT " << eventDurationSplitNumber << " " << eventDurationSplitUnits
			    << __E__;
			OUT << "INC_TAG //increment event window tag" << __E__;
		}

		// Dedicated end-of-fine-loop AND slot: allows single-shot count == fine-loop
		// length (e.g. 100) to clear exactly at the fine-loop boundary.
		size_t   fineEndAndIdx   = standardNValues_[0];
		uint64_t merged_fine_end = existingAndMasks[fineEndAndIdx];
		OUT << "AND_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
		    << merged_fine_end << std::dec << __E__;

		for(size_t l = 1; l < standardNValues_.size(); ++l)
		{
			__FE_COUTT__ << "End loop " << l << " --> " << standardNValues_[l] << "x"
			             << __E__;
			OUT << "DO_LOOP" << __E__;
			POPTAB;

			// AND ops at the end of each coarse loop (after DO_LOOP) to support
			// OR_SINGLESHOT single-shot clearing; coarse slots follow the fine-end slot.
			size_t   coarseEndAndIdx   = standardNValues_[0] + 1 + (l - 1);
			uint64_t merged_coarse_and = existingAndMasks[coarseEndAndIdx];
			OUT << "AND_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
			    << merged_coarse_and << std::dec << __E__;
		}

	}  //end infinite loop ops
	POPTAB;
	OUT << "GOTO_LABEL //for infinite loop" << __E__;

	__COUT_MULTI__(1, out.str());

	FILE* fp = fopen(genFilename.c_str(), "w");
	if(!fp)
	{
		__FE_SS__ << "Error - please check path. Generated Run Plan file from "
		             "template could not be created at "
		          << genFilename << __E__;
		__FE_SS_THROW__;
	}
	fputs(out.str().c_str(), fp);
	fclose(fp);
}  //end generateSharedRunPlanWithPeriodicModeOn()

//========================================================================
// Generates the Share Run Plan text code to implement a
//	periodic mode OFF pattern (for all events)
//	and writes to the specified file.
//
// The concept is that the Shared Run Plan ops never change
//	only the AND and OR parameters change to add/remove bits
void CFOFrontEndInterface::generateSharedRunPlanWithPeriodicModeOff(
    std::stringstream&           logResult,
    std::string&                 genFilename,
    const uint16_t               offBits_startBit,
    const uint16_t               offBits_bitCount,
    const std::string&           eventDurationSplitNumber,
    const std::string&           eventDurationSplitUnits,
    const std::vector<uint64_t>& existingAndMasks,
    const std::vector<uint64_t>& existingOrMasks)
{
	const size_t N_coarse = standardNValues_.size() - 1;

	// When existing masks are provided, validate their sizes and use them to preserve
	// other subsystems' bits. When empty (e.g. internal dummy call), use legacy behavior.
	const bool useMasks = !existingAndMasks.empty() || !existingOrMasks.empty();
	if(useMasks && (existingAndMasks.size() != standardNValues_[0] + 1 + N_coarse ||
	                existingOrMasks.size() != N_coarse + standardNValues_[0]))
	{
		__FE_SS__ << "existingAndMasks and existingOrMasks must each have size "
		          << standardNValues_[0] + 1 + N_coarse << " and "
		          << N_coarse + standardNValues_[0]
		          << " respectively. Got andMasks=" << existingAndMasks.size()
		          << " orMasks=" << existingOrMasks.size() << __E__;
		__FE_SS_THROW__;
	}

	// 48-bit mask of bits this subsystem clears when leaving
	const uint64_t clearBitsMask48 =
	    offBits_bitCount > 0
	        ? (((uint64_t(1) << offBits_bitCount) - 1) << offBits_startBit)
	        : 0ULL;

	std::stringstream out;
	std::string       tabStr, commentStr;
	OUT << "SET_TAG " << 0 << __E__;  //irrelevant since already in the loops!
	OUT << "LABEL //for infinite loop" << __E__;
	PUSHTAB;
	{  // start infinite loop ops

		//strategy for duty cycle is just have standardNValues_[0] positions (i.e. granularity of 1%)
		// but allow 1 in 200, 500, 1000, etc. coarse granularity

		//coarse granularity loops
		//  existingOrMasks[coarseIdx] hold the existing coarse OR masks (before LOOP)
		//  OR_SINGLESHOT_MODE_BITS values are transient and always 0 (already fired/cleared)
		//  existingAndMasks[standardNValues_[0]+(l-1)] hold the existing coarse end-AND masks (after DO_LOOP)
		for(size_t l = standardNValues_.size() - 1; l > 0; --l)
		{
			size_t   coarseIdx = standardNValues_.size() - 1 - l;  //0 for outermost loop
			uint32_t loopN     = standardNValues_[l] / standardNValues_[l - 1];
			__FE_COUTTV__(loopN);

			// OR_SINGLESHOT is transient in hardware; always cleared. Emit 0x0.
			OUT << "OR_SINGLESHOT_MODE_BITS start_bit= 0 bit_count= 48 value= 0x0"
			    << __E__;

			if(useMasks)
			{
				// Keep existing coarse OR bits, but remove the leaving subsystem's bits
				uint64_t merged_coarse_or = existingOrMasks[coarseIdx] & ~clearBitsMask48;
				OUT << "OR_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
				    << merged_coarse_or << std::dec << __E__;
			}
			else
			{
				OUT << "OR_MODE_BITS start_bit= " << 0 << " bit_count= " << 1
				    << " value= " << 0 << __E__;  // no change to mode bits for this loop
			}

			__FE_COUTT__ << "LOOP " << loopN << " // for N = " << standardNValues_[l]
			             << __E__;
			OUT << "LOOP " << loopN << __E__;
			PUSHTAB;
		}

		//fine granularity loop
		//  existingAndMasks[i] and existingOrMasks[N_coarse+i] are the existing fine masks
		for(size_t i = 0; i < standardNValues_[0]; ++i)
		{
			if(useMasks)
			{
				size_t fineIdx = N_coarse + i;

				// AND: clear the leaving subsystem's bits for all positions; merge with existing mask
				uint64_t new_and    = 0xFFFFFFFFFFFFULL & ~clearBitsMask48;
				uint64_t merged_and = existingAndMasks[i] & new_and;
				OUT << "AND_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
				    << merged_and << std::dec
				    << __E__;  // bit positions with 1 keep, 0 remove

				// OR: remove the leaving subsystem's bits from existing OR mask
				uint64_t merged_or = existingOrMasks[fineIdx] & ~clearBitsMask48;
				OUT << "OR_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
				    << merged_or << std::dec << __E__;
			}
			else
			{
				//clear bits on first in iteration
				OUT << "AND_MODE_BITS start_bit= " << offBits_startBit
				    << " bit_count= " << offBits_bitCount << " value= " << 0
				    << __E__;  // bit positions with 1 keep, 0 remove

				OUT << "OR_MODE_BITS start_bit= " << 0 << " bit_count= " << 1
				    << " value= " << 0 << __E__;
			}

			// Keep one OR_SINGLESHOT op per fine slot to preserve shared run-plan opcode
			// structure; value is 0 because single-shot payload is transient/cleared.
			OUT << "OR_SINGLESHOT_MODE_BITS start_bit= 0 bit_count= 48 value= 0x0"
			    << __E__;

			OUT << "HEARTBEAT event_mode = registered // use existing run mode" << __E__;
			OUT << "MARKER" << __E__;
			OUT << "WAIT " << eventDurationSplitNumber << " " << eventDurationSplitUnits
			    << __E__;
			OUT << "INC_TAG //increment event window tag" << __E__;
		}

		// Dedicated end-of-fine-loop AND slot.
		if(useMasks)
		{
			size_t   fineEndAndIdx   = standardNValues_[0];
			uint64_t merged_fine_end = existingAndMasks[fineEndAndIdx] | clearBitsMask48;
			OUT << "AND_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
			    << merged_fine_end << std::dec << __E__;
		}
		else
		{
			OUT << "AND_MODE_BITS start_bit= 0 bit_count= 48 value= 0xFFFFFFFFFFFF"
			    << __E__;
		}

		for(size_t l = 1; l < standardNValues_.size(); ++l)
		{
			__FE_COUTT__ << "End loop " << l << " --> " << standardNValues_[l] << "x"
			             << __E__;
			OUT << "DO_LOOP" << __E__;
			POPTAB;

			// AND ops at the end of each coarse loop (after DO_LOOP) to support
			// OR_SINGLESHOT single-shot clearing; existingAndMasks[standardNValues_[0]+(l-1)]
			if(useMasks)
			{
				// Restore leaving subsystem's bit to 1 in coarse end-AND (no longer clearing it)
				size_t   coarseEndAndIdx = standardNValues_[0] + 1 + (l - 1);
				uint64_t merged_coarse_and =
				    existingAndMasks[coarseEndAndIdx] | clearBitsMask48;
				OUT << "AND_MODE_BITS start_bit= 0 bit_count= 48 value= 0x" << std::hex
				    << merged_coarse_and << std::dec << __E__;
			}
			else
			{
				OUT << "AND_MODE_BITS start_bit= 0 bit_count= 48 value= 0xFFFFFFFFFFFF"
				    << __E__;
			}
		}

	}  //end infinite loop ops
	POPTAB;
	OUT << "GOTO_LABEL //for infinite loop" << __E__;

	__COUT_MULTI__(1, out.str());

	FILE* fp = fopen(genFilename.c_str(), "w");
	if(!fp)
	{
		__FE_SS__ << "Error - please check path. Generated Run Plan file from "
		             "template could not be created at "
		          << genFilename << __E__;
		__FE_SS_THROW__;
	}
	fputs(out.str().c_str(), fp);
	fclose(fp);
}  //end generateSharedRunPlanWithPeriodicModeOff()

//========================================================================
// return M:N ratio of M events on per N events
uint64_t CFOFrontEndInterface::extractSharedRunPlanEventDuration(
    std::optional<std::reference_wrapper<std::vector<uint64_t>>> andMasks,
    std::optional<std::reference_wrapper<std::vector<uint64_t>>> orMasks)
try
{
	//make a dummy shared run plan, and use to compare against current run plan
	//	- Confirm the current run plan is exact Operand match of the dummy Shared Run Plan ops.
	//	- Extract run plan event duration from mismatches.
	//

	std::map<uint32_t /* address */,
	         std::pair<uint32_t /* expected */, uint32_t /* actual */>>
	    mismatches;

	//generate dummy Run Plan and diff with current CFO Run Plan data read back from CFO
	// Note: as of 22-Feb-2026, Run Plan BRAM is 1024 ops
	{
		const std::string SOURCE_BASE_PATH = std::string(__ENV__("OTSDAQ_DATA")) + "/";
		std::string inFileName  = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.txt";
		std::string outFileName = SOURCE_BASE_PATH + "Mu2eCFORunPlanFromTEMPLATE.bin";

		uint64_t dummyDuration = 1;
		dummyDuration <<= 47;
		dummyDuration |= 1;  //put a value in hi and lo 32-bits to show diff
		std::stringstream result;
		generateSharedRunPlanWithPeriodicModeOff(
		    result,
		    inFileName,
		    0,                              //start bit
		    1,                              //bit count
		    std::to_string(dummyDuration),  //eventDurationInClocks
		    "clocks"                        //eventDurationSplitUnits
		);

		CFOLib::CFO_Compiler compiler;
		result << "\n\nDummy Run Plan:\n"
		       << compiler.processFile(inFileName, outFileName);

		__COUT_MULTI__(1, result.str());

		std::string binaryContents;
		{  //load dummy plan data a la CFOFrontEndInterface::SetRunplan
			__FE_COUTV__(outFileName);

			std::FILE* fp = std::fopen(outFileName.c_str(), "rb");
			if(!fp)
			{
				__SS__ << "Could not open file at " << outFileName << ". Error: " << errno
				       << " - " << strerror(errno) << __E__;
				__SS_THROW__;
			}

			std::fseek(fp, 0, SEEK_END);
			binaryContents.resize(std::ftell(fp));
			std::rewind(fp);
			std::fread(&binaryContents[0], 1, binaryContents.size(), fp);
			std::fclose(fp);
			sharedRunPlanSize_ = binaryContents.size();
		}  //end load dummy plan data

		thisCFO_->CompareRunPlanData(
		    binaryContents, 0 /* address */, mismatches, andMasks, orMasks);
	}  //end generate and Run Plan diff

	//look for a WAIT op to find event duration
	if(!mismatches.size())
	{
		__FE_SS__
		    << "IMPOSSIBLE!! No mismatches were found when comparing the generated Run "
		       "Plan to the current CFO Run Plan data read back from the CFO. This "
		       "indicates that the CFO is currently running the expected shared Run "
		       "Plan, and that reading back the Run Plan data from the CFO."
		    << __E__;
		__FE_SS_THROW__;
	}

	__FE_SS__
	    << "Mismatches were found when comparing the generated Run Plan to the current "
	       "CFO Run Plan data read back from the CFO. This likely indicates that the CFO "
	       "is not currently running the expected shared Run Plan, or that there is an "
	       "issue with reading back the Run Plan data from the CFO."
	    << __E__;
	uint64_t eventDurationInClocks          = 0;
	uint32_t lastMismatchAddress            = 0;
	uint64_t potentialEventDurationInClocks = 0;  //build from 2 32-bit words
	for(auto& mismatch : mismatches)
	{
		if(mismatch.first % 2 == 0)  // only look at top-32 bits for ops
		{
			potentialEventDurationInClocks =
			    mismatch.second.second;  // actual low 32-bits from CFO
			lastMismatchAddress = mismatch.first;
			continue;
		}

		uint8_t expectedOpCode = (mismatch.second.first >> 24) & 0xEF;
		uint8_t actualOpCode   = (mismatch.second.second >> 24) & 0xEF;

		if(expectedOpCode != actualOpCode)
		{
			ss << "Address: " << mismatch.first << " Line #: " << mismatch.first / 2 + 1
			   << std::hex << " Expected: 0x" << mismatch.second.first << " Actual: 0x"
			   << mismatch.second.second << std::dec << __E__;
			__FE_SS_THROW__;
		}

		if(expectedOpCode ==
		   (uint8_t)CFOLib::CFO_Compiler::CFO_INSTR::WAIT)  // WAIT op opcode
		{
			if(lastMismatchAddress != mismatch.first - 1)
			{
				__FE_SS__
				    << "Unexpected mismatch pattern found when comparing the generated "
				       "Run Plan to the current CFO Run Plan data read back from the "
				       "CFO. Expected mismatches for WAIT op to be in consecutive "
				       "addresses with the first address containing the low 32-bits of "
				       "event duration and the second address containing the high "
				       "32-bits of event duration. Found mismatch at address "
				    << lastMismatchAddress
				    << " followed by mismatch at non-consecutive address "
				    << mismatch.first << __E__;
				__FE_SS_THROW__;
			}

			potentialEventDurationInClocks |= uint64_t(mismatch.second.second & 0xFFFF)
			                                  << 32;  // actual hi 16-bits from CFO
			__FE_COUTT__ << "potentialEventDurationInClocks = "
			             << potentialEventDurationInClocks
			             << " Address: " << mismatch.first
			             << " Line #: " << mismatch.first / 2 + 1 << std::hex
			             << " Expected: 0x" << mismatch.second.first << " Actual: 0x"
			             << mismatch.second.second << std::dec << __E__;
			if(!eventDurationInClocks)
				eventDurationInClocks = potentialEventDurationInClocks;
			else if(eventDurationInClocks != potentialEventDurationInClocks)
			{
				__FE_SS__ << "Inconsistent event duration values found in CFO Run Plan "
				             "mismatches. Expected: "
				          << eventDurationInClocks
				          << ", Found: " << potentialEventDurationInClocks << __E__;
				__FE_SS_THROW__;
			}
		}
	}  //end mismatch loop search

	__FE_COUTV__(eventDurationInClocks);

	// Verify AND/OR mask vectors populated by CompareRunPlanData have the expected size.
	// Expected AND: standardNValues_[0] fine positions + 1 fine-end slot + N_coarse coarse-end slots.
	// Expected OR:  N_coarse coarse OR slots + standardNValues_[0] fine OR slots.
	// Note: OR_SINGLESHOT values are NOT extracted (assumed always 0).
	if(andMasks || orMasks)
	{
		const uint32_t N_coarse = standardNValues_.size() - 1;
		if(andMasks && andMasks->get().size() != standardNValues_[0] + 1 + N_coarse)
		{
			__FE_SS__ << "Extracted AND ops size mismatch. "
			          << "Expected " << standardNValues_[0] + 1 + N_coarse
			          << ", got andMasks=" << andMasks->get().size() << __E__;
			__FE_SS_THROW__;
		}

		if(orMasks && orMasks->get().size() != N_coarse + standardNValues_[0])
		{
			__FE_SS__ << "Internal error: extracted mask vector size mismatch. "
			          << "Expected " << (N_coarse + standardNValues_[0])
			          << ", got orMasks=" << orMasks->get().size() << __E__;
			__FE_SS_THROW__;
		}

		__FE_COUT__ << "Extracted " << (andMasks ? andMasks->get().size() : 0)
		            << " AND masks and " << (orMasks ? orMasks->get().size() : 0)
		            << " OR masks from current CFO run plan." << __E__;
	}

	return eventDurationInClocks;
}  //end extractSharedRunPlanEventDuration()
catch(const std::runtime_error& e)
{
	__FE_SS__ << "Error extracting event duration for the Shared Run Plan - please make "
	             "sure there is an active Shared Run Plan (i.e. common operation set). "
	             "To start a Shared Run Plan, do 'CFO Halt' and then 'Share Run Plan "
	             "Start.'\n\nHere was the error:\n"
	          << e.what() << __E__;
	__FE_SS_THROW__;
}  //end extractSharedRunPlanEventDuration() catch

//========================================================================
void CFOFrontEndInterface::BufferTest_detached(__ARGS__)
{
	__FE_COUT__ << "Operation \"BufferTest_detached\"" << std::endl;

	// arguments
	std::string command = __GET_ARG_IN__(
	    "Command to 0/Status (to read counters, etc.), 1/Start, or 2/Halt (Default: "
	    "Status)",
	    std::string,
	    "Status");

	// bool dataAreSubEvents =
	//     __GET_ARG_IN__("Data are SubEvents (Default: true)", bool, true);
	// unsigned int numberOfEvents = __GET_ARG_IN__("Number of [Sub]Events (Default: 1)", uint32_t, 1);
	// bool         activeMatch = __GET_ARG_IN__("Match Event Tags (Default: false)", bool);
	unsigned int timestampStart =
	    __GET_ARG_IN__("Starting Event Window Tag (Default: 0)", unsigned int);
	bool saveBinaryDataToFile =
	    __GET_ARG_IN__("Save Binary Data to File (Default: false)", bool);
	// std::string saveBinaryDataFilename =
	//     __GET_ARG_IN__("Save Binary Data Filename", std::string);
	// bool saveSubeventHeadersToDataFile =
	//     __GET_ARG_IN__("Save Subevent Header to Binary File (Default: false)", bool);
	// bool displayPayloadAtGUI = __GET_ARG_IN__("Display Payload at GUI (Default: true)", bool, true);
	// unsigned int packetThresholdToSave = __GET_ARG_IN__(
	//     "Payload Packet Threshold for Saving Event (Default: 0)", unsigned int);

	__FE_COUTV__(command);
	// __FE_COUTV__(dataAreSubEvents);
	// __FE_COUTV__(activeMatch);
	__FE_COUTV__(timestampStart);
	__FE_COUTV__(saveBinaryDataToFile);
	// __FE_COUTV__(saveBinaryDataFilename);
	// __FE_COUTV__(saveSubeventHeadersToDataFile);
	// __FE_COUTV__(packetThresholdToSave);

	// // print the result
	std::stringstream outSs;
	outSs << "Command: " << command << __E__;
	if(command == "1" || command == "Start")
	{
		__FE_COUT__ << "Detaching thread and reading data DMA-0 starting at event tag "
		            << timestampStart << " (0x" << std::hex << timestampStart << ")"
		            << __E__;

		if(!bufferTestThreadStruct_)  //initialize shared pointer for first time
			bufferTestThreadStruct_ =
			    std::make_shared<CFOFrontEndInterface::DetachedBufferTestThreadStruct>();

		if(bufferTestThreadStruct_->running_)
			outSs
			    << "Found buffer test thread already running, doing nothing. Please "
			       "'Halt' before restarting. Or run 'Status' to read the latest status."
			    << __E__;
		else
		{
			__FE_COUT__ << "Launching detached Buffer Test thread..." << __E__;

			// start mutex scope
			{
				std::lock_guard<std::mutex> lock(bufferTestThreadStruct_->lock_);

				bufferTestThreadStruct_->expectedEventTag_ = timestampStart;
				bufferTestThreadStruct_->saveBinaryData_   = saveBinaryDataToFile;
				bufferTestThreadStruct_->publish_ =
				    static_cast<ots::FESupervisor*>(parentSupervisor_)
				        ->isPublishingData();
				bufferTestThreadStruct_->feSupervisor_ =
				    static_cast<ots::FESupervisor*>(parentSupervisor_);
				bufferTestThreadStruct_->exitThread_         = false;
				bufferTestThreadStruct_->resetStartEventTag_ = false;
				bufferTestThreadStruct_->thisCFO_            = thisCFO_;
				bufferTestThreadStruct_->running_            = true;
				bufferTestThreadStruct_->doNotResetCounters_ = false;
				bufferTestThreadStruct_->error_              = "";
			}
			std::thread(
			    [](std::shared_ptr<CFOFrontEndInterface::DetachedBufferTestThreadStruct>
			           threadStruct) {
				    CFOFrontEndInterface::detechedBufferTestThread(threadStruct);
			    },
			    bufferTestThreadStruct_)
			    .detach();
			outSs << "Launched detached Buffer Test thread and reading data DMA-0 "
			         "starting at event tag "
			      << timestampStart << " (0x" << std::hex << timestampStart << ")"
			      << __E__;
		}
		sleep(1);
		outSs << "Reading status..." << __E__;
		outSs << CFOFrontEndInterface::getDetachedBufferTestStatus(
		    bufferTestThreadStruct_);
	}
	else if(command == "0" || command == "Status")
	{
		__FE_COUT__ << "Reading thread status..." << __E__;
		outSs << "Reading thread status..." << __E__;

		if(!bufferTestThreadStruct_)  //initialize shared pointer for first time
			bufferTestThreadStruct_ =
			    std::make_shared<CFOFrontEndInterface::DetachedBufferTestThreadStruct>();

		outSs << CFOFrontEndInterface::getDetachedBufferTestStatus(
		    bufferTestThreadStruct_);
	}
	else if(command == "2" || command == "Halt")
	{
		__FE_COUT__ << "Halting thread... " << __E__;

		if(!bufferTestThreadStruct_)  //initialize shared pointer for first time
			bufferTestThreadStruct_ =
			    std::make_shared<CFOFrontEndInterface::DetachedBufferTestThreadStruct>();

		// start mutex scope
		{
			std::lock_guard<std::mutex> lock(bufferTestThreadStruct_->lock_);
			bufferTestThreadStruct_->exitThread_ = true;
		}

		//check for thread to exit
		for(int i = 0; i < 10; ++i)
		{
			usleep(100 * 1000 /*100ms*/);  // sleep for exit time
			if(!bufferTestThreadStruct_->running_)
				break;
			__FE_COUT__ << "Waiting for thread to exit... #" << i << __E__;
		}

		if(bufferTestThreadStruct_->fp_)
		{
			__FE_COUT_WARN__ << "Buffer Test thread file was left open?! Closing..."
			                 << __E__;

			fclose(bufferTestThreadStruct_->fp_);
			bufferTestThreadStruct_->fp_ = nullptr;
		}

		outSs << "Detached Buffer Test thread exited. " << __E__;
		outSs << "Reading final status..." << __E__;
		try
		{
			outSs << CFOFrontEndInterface::getDetachedBufferTestStatus(
			    bufferTestThreadStruct_);
		}
		catch(const std::runtime_error& e)
		{
			__FE_COUT_WARN__ << "Ignoring buffer status error during HALT: " << e.what()
			                 << __E__;
		}
	}
	else
	{
		outSs << "Unrecognized command '" << command
		      << "' found. Valid commands are Start, Status, and Halt." << __E__;
	}
	// outSs << "Active Event Match: " << (activeMatch?"true":"false") << __E__;
	// outSs << "Event Duration: " << cfoDelay << " = " << cfoDelay*25 << " ns" << __E__;
	// outSs << "Reading back: " << (doNotReadBack?"false":"true") << __E__;
	if(saveBinaryDataToFile)
	{
		outSs << "Binary data file saved to: "
		      << std::string(__ENV__("OTSDAQ_DATA")) + "/macroOutput_*" << __E__;
		outSs << "\n"
		      << "To view binary data do "
		         "hexdump -v -e '\"%08_ax \" 7/8 \"%016x \"' -e '\"\\n\"' "
		      << std::string(__ENV__("OTSDAQ_DATA")) << "/macroOutput_*.bin" << __E__;
	}
	// outSs << ostr.str();

	std::cout << "Untruncated output: \n" << outSs.str() << __E__;  //for no truncation!

	__SET_ARG_OUT__("Result", outSs.str());
}  //end BufferTest_detached()

//========================================================================
void CFOFrontEndInterface::RunplanSubrunConfigSetup(__ARGS__)
{
	__FE_COUT__ << "Setting Runplan Subrun Config" << __E__;

	uint32_t subrunEvtLimit =
	    __GET_ARG_IN__("Subrun Event Limit (Default: 0)", uint32_t, 0);
	uint32_t subrunPredOffset =
	    __GET_ARG_IN__("Subrun Prediction Offset (Default: 0)", uint32_t, 0);

	__FE_COUTV__(subrunEvtLimit);
	__FE_COUTV__(subrunPredOffset);

	thisCFO_->SetRunPlanSubrunEvtLimit(subrunEvtLimit);
	thisCFO_->SetRunPlanSubrunPredOffset(subrunPredOffset);

	__FE_COUT__ << "Runplan Subrun Config set successfully." << __E__;
}  //end RunplanSubrunConfigSetup()

//========================================================================
void CFOFrontEndInterface::RunplanSubrunConfigRead(__ARGS__)
{
	__FE_COUT__ << "Reading Runplan Subrun Config" << __E__;

	uint32_t subrunEvtLimit   = thisCFO_->ReadRunPlanSubrunEvtLimit();
	uint32_t subrunPredOffset = thisCFO_->ReadRunPlanSubrunPredOffset();

	__FE_COUTV__(subrunEvtLimit);
	__FE_COUTV__(subrunPredOffset);

	__SET_ARG_OUT__("Subrun Event Limit", subrunEvtLimit);
	__SET_ARG_OUT__("Subrun Prediction Offset", subrunPredOffset);
}  //end RunplanSubrunConfigRead()

//==============================================================================
void CFOFrontEndInterface::TemporaryDiagnosticTest(__ARGS__)
{
	std::string targetDTC = "Calo01_DTC1";
	__FE_COUT_INFO__ << "TemporaryDiagnosticTest: calling 'Get Link Lock Status' on "
	                 << targetDTC << __E__;

	std::vector<FEVInterface::frontEndMacroArg_t> macroArgsIn, macroArgsOut;
	runFrontEndMacro(targetDTC, "Get Link Lock Status", macroArgsIn, macroArgsOut);

	__FE_COUT_INFO__ << "macroArgsOut.size()=" << macroArgsOut.size() << __E__;
	std::ostringstream ostr;
	ostr << "Called 'Get Link Lock Status' on " << targetDTC << "\n";
	ostr << "macroArgsOut.size()=" << macroArgsOut.size() << "\n";
	for(size_t i = 0; i < macroArgsOut.size(); ++i)
		ostr << "  macroArgsOut[" << i << "] name='" << macroArgsOut[i].first << "' val='"
		     << macroArgsOut[i].second << "'\n";

	__SET_ARG_OUT__("Response", ostr.str());
}  //end TemporaryDiagnosticTest()

// DEFINE_OTS_INTERFACE(CFOFrontEndInterface)
