#include "otsdaq-mu2e/CFOandDTCCore/CFOandDTCCoreVInterface.h"
#include "otsdaq/FECore/MakeInterface.h"
#include "otsdaq/Macros/BinaryStringMacros.h"
#include "otsdaq/Macros/InterfacePluginMacros.h"
#include "otsdaq/TablePlugins/ARTDAQTableBase/ARTDAQTableBase.h"

using namespace ots;

#undef __MF_SUBJECT__
#define __MF_SUBJECT__ "CFOandDTCCoreVInterface"

std::string CFOandDTCCoreVInterface::CONFIG_MODE_HARDWARE_DEV   = "HardwareDevMode";
std::string CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING = "EventBuildingMode";
std::string CFOandDTCCoreVInterface::CONFIG_MODE_EVENT_BUILDING_AND_SYNC =
    "EventBuildingAndSyncMode";
std::string CFOandDTCCoreVInterface::CONFIG_MODE_LOOPBACK = "LoopbackMode";

//=========================================================================================
CFOandDTCCoreVInterface::CFOandDTCCoreVInterface(
    const std::string&       interfaceUID,
    const ConfigurationTree& theXDAQContextConfigTree,
    const std::string&       interfaceConfigurationPath)
    : FEVInterface(interfaceUID, theXDAQContextConfigTree, interfaceConfigurationPath)
{
	__FE_COUT__ << "instantiate CFO-DTC_core... " << getInterfaceUID() << " "
	            << theXDAQContextConfigTree << " " << interfaceConfigurationPath << __E__;

	universalAddressSize_ = sizeof(dtc_address_t);
	universalDataSize_    = sizeof(dtc_data_t);

	configure_clock_ = getSelfNode().getNode("ConfigureClock").getValue<bool>();
	__FE_COUTV__(configure_clock_);

	artdaqMode_ = ARTDAQTableBase::isARTDAQEnabled(getConfigurationManager());
	__FE_COUTV__(artdaqMode_);

	operatingMode_ = CFOandDTCCoreVInterface::CONFIG_MODE_HARDWARE_DEV;  // choose default
	try
	{
		auto mu2eGlobalRecords =
		    getConfigurationManager()->getNode("/Mu2eGlobalsTable").getChildren();
		if(mu2eGlobalRecords.size())  // take first record
			operatingMode_ = mu2eGlobalRecords[0]
			                     .second.getNode("GlobalOperatingMode")
			                     .getValue<std::string>();
	}
	catch(...)
	{
		__FE_COUT_WARN__ << "Ignoring missing Mu2eGlobalsTable definition, defaulting "
		                    "operating mode = "
		                 << operatingMode_ << __E__;
	}
	__FE_COUTV__(operatingMode_);

	skipInit_ =
	    true;  //default to skipping configure unless Mu2e Globals is set. This way MacroMaker mode will skip configure when the global table is missing.
	try
	{
		auto mu2eGlobalRecords =
		    getConfigurationManager()->getNode("/Mu2eGlobalsTable").getChildren();
		if(mu2eGlobalRecords.size())  // take first record
		{
			// Try new column name first, fall back to old name for backwards compatibility
			try
			{
				skipInit_ = mu2eGlobalRecords[0]
				                .second.getNode("GlobalSkipCFOandDTCConfigureSteps")
				                .getValue<bool>();
			}
			catch(...)
			{
				skipInit_ = mu2eGlobalRecords[0]
				                .second.getNode("SkipCFOandDTCConfigureSteps")
				                .getValue<bool>();
			}
			__FE_COUTV__(skipInit_);
		}
		__FE_COUTV__(mu2eGlobalRecords.size());
	}
	catch(const std::runtime_error& e)
	{
		__FE_COUT_WARN__
		    << "Ignoring missing Mu2eGlobalsTable definition, defaulting skipInit_ = "
		    << skipInit_ << __E__;
	}
	__FE_COUTV__(skipInit_);

	// PCIe index to communicate with
	deviceIndex_ = getSelfNode().getNode("DeviceIndex").getValue<unsigned int>();
	__FE_COUTV__(deviceIndex_);

	try
	{
		emulatorMode_ = getSelfNode().getNode("EmulatorMode").getValue<bool>();
	}
	catch(...)
	{
		__FE_COUT__ << "Assuming NOT emulator mode." << __E__;
		emulatorMode_ = false;
	}
	__FE_COUTV__(emulatorMode_);

	// if(!emulatorMode_)
	// {
	// 	snprintf(devfile_, 11, "/dev/" MU2E_DEV_FILE, device_);
	// 	fd_ = open(devfile_, O_RDONLY);
	// }

	__FE_COUT__ << "Constructed." << __E__;
}  // end constructor()

//==========================================================================================
CFOandDTCCoreVInterface::~CFOandDTCCoreVInterface(void)
{
	// if(regWriteMonitorStream_.is_open())
	// {
	// 	regWriteMonitorStream_ << "Timestamp: " << std::dec << time(0) <<
	// 		", \t ---------- Destructed: " << device_name_ << "\n";
	// 	regWriteMonitorStream_.close();
	// }
	// if(runDataFile_.is_open()) runDataFile_.close();
	// if(fd_) close(fd_);
	__FE_COUT__ << "Destructed." << __E__;
}  // end destructor()

//===========================================================================================
void CFOandDTCCoreVInterface::registerCFOandDTCFEMacros(void)
{
	bool isCFO = getInterfaceType() == "CFOFrontEndInterface";

	registerFEMacroFunction(
	    std::string(isCFO ? "CFO " : "DTC ") + "Soft Reset",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::SoftReset),
	    std::vector<std::string>{},
	    std::vector<std::string>{},
	    1,  // requiredUserPermissions
	    "*",
	    "Executes a soft reset of the " + std::string(isCFO ? "CFO" : "DTC") +
	        " by setting the reset bit (31) to true on the <b>" +
	        std::string(isCFO ? "CFO " : "DTC ") +
	        "Control Register</b> (0x9100). "
	        "This bit clear counters and FIFOs; it does not change select/control bits, "
	        "it does not reset the primary " +
	        std::string(isCFO ? "CFO" : "DTC") + " Timing Interface block.");

	registerFEMacroFunction(
	    std::string(isCFO ? "CFO " : "DTC ") + "Hard Reset",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::HardReset),
	    std::vector<std::string>{},
	    std::vector<std::string>{},
	    1,  // requiredUserPermissions
	    "*",
	    "Executes a soft reset of the " + std::string(isCFO ? "CFO" : "DTC") +
	        " by setting the reset bit (0) to true on the <b>" +
	        std::string(isCFO ? "CFO " : "DTC ") +
	        "Control Register</b> (0x9100). "
	        "This bit is like a ‘factory reset’ - it DOES change "
	        "select/control/threshold bits back to defaults; it DOES reset the primary "
	        "FPGA Timing Interface block. It also executes a soft reset of the " +
	        std::string(isCFO ? "CFO" : "DTC") + " after the hard reset.");

	registerFEMacroFunction(
	    "Get Firmware Version",  // feMacroName
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::GetFirmwareVersion),  // feMacroFunction
	    std::vector<std::string>{},
	    std::vector<std::string>{"Firmware Version Date"},  // namesOfOutputArgs
	    1,                                                  //"allUsers:0 | TDAQ:255");
	    "*",
	    "Read the modification date of the DTC firmware using <b>MON/DD/20YY HH:00</b> "
	    "format.");

	// registerFEMacroFunction( //RAR not working!
	// 	"Reset PCIe",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&CFOandDTCCoreVInterface::ResetPCIe),            // feMacroFunction
	// 				std::vector<std::string>{},  // namesOfInputArgs
	// 				std::vector<std::string>{"Status"}, // namesOfOutputArgs
	// 				1,  // requiredUserPermissions
	// 				"*",
	// 				"Reset the PCIe interface allocated for this FPGA."
	// );

	registerFEMacroFunction("Flash LEDs",  // feMacroName
	                        static_cast<FEVInterface::frontEndMacroFunction_t>(
	                            &CFOandDTCCoreVInterface::FlashLEDs),  // feMacroFunction
	                        std::vector<std::string>{},
	                        std::vector<std::string>{"Status"},  // namesOfOutputArgs
	                        1,    // requiredUserPermissions
	                        "*",  // allowedCallingFEs
	                        "Flashes the LEDs, for example to help find the target FPGA "
	                        "in a multi-FPGA deployment.");

	registerFEMacroFunction(
	    "Get Status",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::GetStatus),  // feMacroFunction
	    std::vector<std::string>{},                // namesOfInputArgs
	    std::vector<std::string>{"Status"},
	    1,    // requiredUserPermissions
	    "*",  // allowedCallingFEs
	    "Reads and displays all registers in a human-readable format.");

	registerFEMacroFunction(
	    "Get Simple Status",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::GetSimpleStatus),  // feMacroFunction
	    std::vector<std::string>{},                      // namesOfInputArgs
	    std::vector<std::string>{"Status"},
	    1,    // requiredUserPermissions
	    "*",  // allowedCallingFEs
	    "Similar to <b>Get Status</b>, this FE Macro fetches data from a select few "
	    "registers. "
	    "Registers <b>DTC Control, ROC Emulation Enable, Link Enable, SERDES Reset, and "
	    "SERDES Reset Done</b> are displayed in a human-readable format.");

	registerFEMacroFunction(
	    "Check Firefly Temperature",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::GetFireflyTemperature),  // feMacroFunction
	    std::vector<std::string>{},                            // namesOfInputArgs
	    std::vector<std::string>{"Temperature"},
	    1,  // requiredUserPermissions
	    "*",
	    "Check the temperature from the Firefly RX IIC Bus.");

	registerFEMacroFunction(
	    "Check Firefly Loss-of-Light",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::GetLinkLossOfLight),  // feMacroFunction
	    std::vector<std::string>{},                         // namesOfInputArgs
	    std::vector<std::string>{"Link Status"},
	    1,  // requiredUserPermissions
	    "*",
	    "Checking the Loss-of-Light reads from the I2C bus register on the Firefly for "
	    "any light source.");

	registerFEMacroFunction(
	    "Check FPGA Temperature",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::GetFPGATemperature),  // feMacroFunction
	    std::vector<std::string>{},                         // namesOfInputArgs
	    std::vector<std::string>{"Temperature"},
	    1,
	    "*",
	    "Get the temperature of the FPGA in degrees Celsius. "
	    "A -273.0 reading is equivalent to 0x0 from the ADC on-die sensor.");

	registerFEMacroFunction(
	    "Jitter Attenuator Setup",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::SelectJitterAttenuatorSource),
	    std::vector<std::string>{
	        "Source Clock (0 is from CFO, 1 is from RJ45)", "DoNotSet", "AlsoResetJA"},
	    std::vector<std::string>{"Register Write Results"},
	    1,  // requiredUserPermissions
	    "*",
	    "Select the source of the jitter attenuator: a local oscilator on the DTC or the "
	    "RTF.\n"
	    "The RTF (RJ45 Timing Fanout) is a separate board to alleviate jitter "
	    "accumulation. <b>Not all DTCs are connected to the RTF</b>. ");

	registerFEMacroFunction(
	    "Get Device Index",
	    static_cast<FEVInterface::frontEndMacroFunction_t>(
	        &CFOandDTCCoreVInterface::GetDeviceIndex),
	    std::vector<std::string>{},                // namesOfInputArgs
	    std::vector<std::string>{"Device Index"},  // namesOfOutputArgs
	    1,                                         // requiredUserPermissions
	    "*",                                       // allowedCallingFEs
	    "Returns the PCIe device index for this CFO/DTC instance.");

	// registerFEMacroFunction(
	// 	"Reset Link Rx",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&CFOandDTCCoreVInterface::ResetLinkRx),
	// 			        std::vector<std::string>{"Link to Reset (0-7, 6/CFO, 7/EVB)"},
	// 					std::vector<std::string>{"Register Write Results"},
	// 				1);  // requiredUserPermissions

	// registerFEMacroFunction(
	// 	"Shutdown Link Tx",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&CFOandDTCCoreVInterface::ShutdownLinkTx),
	// 			        std::vector<std::string>{"Link to Shutdown (0-7, 6/CFO, 7/EVB)"},
	// 				std::vector<std::string>{
	// 					"Reset Status",
	// 					"Link Reset Register"},
	// 				1);  // requiredUserPermissions
	// registerFEMacroFunction(
	// 	"Startup Link Tx",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&CFOandDTCCoreVInterface::StartupLinkTx),
	// 				std::vector<std::string>{"Link to Startup (0-7, 6/CFO, 7/EVB)"},
	// 				std::vector<std::string>{
	// 					"Reset Status",
	// 					"Link Reset Register"},
	// 				1);  // requiredUserPermissions

	// registerFEMacroFunction(
	// 	"Shutdown Firefly Tx",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&CFOandDTCCoreVInterface::ShutdownFireflyTx),
	// 				std::vector<std::string>{"Link to Shutdown (0-7, 6/CFO, 7/EVB)"},
	// 				std::vector<std::string>{
	// 					"Shutdown Status"},
	// 				1);  // requiredUserPermissions
	// registerFEMacroFunction(
	// 	"Startup Firefly Tx",
	// 		static_cast<FEVInterface::frontEndMacroFunction_t>(
	// 				&CFOandDTCCoreVInterface::StartupFireflyTx),
	// 				std::vector<std::string>{"Link to Startup (0-7, 6/CFO, 7/EVB)"},
	// 				std::vector<std::string>{
	// 					"Startup Status"},
	// 				1);  // requiredUserPermissions

}  //end registerCFOandDTCFEMacros()

//==========================================================================================
// universalRead
//	Must implement this function for Macro Maker to work with this
// interface. 	When Macro Maker calls:
//		- address will be a [universalAddressSize_] byte long char array
//		- returnValue will be a [universalDataSize_] byte long char
// array
//		- expects exception thrown on failure/timeout
void CFOandDTCCoreVInterface::universalRead(char* address, char* returnValue)
{
	// __FE_COUT__ << "DTC READ" << __E__;

	if(emulatorMode_)
	{
		__FE_COUT__ << "Emulator universal read " << __E__;

		for(unsigned int i = 0; i < universalDataSize_; ++i)
			returnValue[i] = (0xF0 | i) + rand() % 100;
		return;
	}

	// (*((dtc_data_t*)returnValue)) = thisDTC_->ReadRegister(*((dtc_address_t*)address));

	int errorCode = getDevice()->read_register(
	    *((dtc_address_t*)address), 100, ((dtc_data_t*)returnValue));
	if(errorCode != 0)
	{
		__FE_SS__ << "Error reading register 0x" << std::hex
		          << static_cast<uint32_t>(*((dtc_address_t*)address)) << " "
		          << errorCode;
		__SS_THROW__;
	}
}  // end universalRead()

//=====================================================================================
// universalWrite
//	Must implement this function for Macro Maker to work with this
// interface. 	When Macro Maker calls:
//		- address will be a [universalAddressSize_] byte long char array
//		- writeValue will be a [universalDataSize_] byte long char array
void CFOandDTCCoreVInterface::universalWrite(char* address, char* writeValue)
{
	// __FE_COUT__ << "DTC WRITE" << __E__;
	if(emulatorMode_)
	{
		__FE_COUT__ << "Emulator universal write " << __E__;
		return;
	}

	int errorCode = getDevice()->write_register(
	    *((dtc_address_t*)address), 100, *((dtc_data_t*)writeValue));
	if(errorCode != 0)
	{
		__FE_SS__ << "Error writing register 0x" << std::hex
		          << static_cast<uint32_t>(*((dtc_address_t*)address)) << " "
		          << errorCode;
		__SS_THROW__;
	}
}  // end universalWrite()

//==============================================================================
// GetFirmwareVersion
void CFOandDTCCoreVInterface::GetFirmwareVersion(__ARGS__)
{
	__SET_ARG_OUT__(
	    "Firmware Version Date",
	    getCFOandDTCRegisters()->FormattedRegDump(
	        130, {[this] { return getCFOandDTCRegisters()->FormatDeviceTimeAlive(); }}));
}  // end GetFirmwareVersion()

//========================================================================
void CFOandDTCCoreVInterface::ResetPCIe(__ARGS__)
{
	getCFOandDTCRegisters()->ResetPCIe();
	__SET_ARG_OUT__("Status", "Done");
}  //end ResetPCIe()

//========================================================================
void CFOandDTCCoreVInterface::FlashLEDs(__ARGS__)
{
	getCFOandDTCRegisters()->FlashLEDs();
	__SET_ARG_OUT__("Status", "Done");
}  //end FlashLEDs()

//========================================================================
void CFOandDTCCoreVInterface::GetStatus(__ARGS__)
{
	__SET_ARG_OUT__("Status",
	                getCFOandDTCRegisters()->FormattedRegDump(
	                    130, getCFOandDTCRegisters()->getFormattedDumpFunctions()));
}  //end GetStatus()

//========================================================================
void CFOandDTCCoreVInterface::GetSimpleStatus(__ARGS__)
{
	__SET_ARG_OUT__("Status",
	                getCFOandDTCRegisters()->FormattedRegDump(
	                    130, getCFOandDTCRegisters()->getFormattedSimpleDumpFunctions()));
}  //end GetSimpleStatus()

//========================================================================
void CFOandDTCCoreVInterface::GetLinkLossOfLight(__ARGS__)
{
	std::stringstream rd;

	//do initial set of writes to get the live read of loss-of-light status (because it is latched value from last read)
	/*
	// #Read Firefly RX LOS registers
	// #enable IIC on Firefly
	// my_cntl write 0x93a0 0x00000200
	registerWrite(0x93a0,0x00000200);
	// #Device address, register address, null, null
	// my_cntl write 0x9298 0x54080000
	registerWrite(0x9298,0x54080000);
	// #read enable
	// my_cntl write 0x929c 0x00000002
	registerWrite(0x929c,0x00000002);
	// #disable IIC on Firefly
	// my_cntl write 0x93a0 0x00000000
	registerWrite(0x93a0,0x00000000);
	// #read data: Device address, register address, null, value
	// my_cntl read 0x9298
*/
	getCFOandDTCRegisters()->SetTXRXFireflySelect(true);
	getCFOandDTCRegisters()->WriteFireflyRXIICInterface(
	    0x54 /*device*/, 0x08 /*address*/, 0 /*data*/);
	getCFOandDTCRegisters()->SetTXRXFireflySelect(false);

	// #{EVB, ROC4, ROC1, CFO, unused, ROC5, unused, unused}
	usleep(1000 * 100);

	/*
	// #Read Firefly RX LOS registers
	// my_cntl write 0x93a0 0x00000200
	registerWrite(0x93a0,0x00000200);
	// my_cntl write 0x9298 0x54070000
	registerWrite(0x9298,0x54070000);
	// my_cntl write 0x929c 0x00000002
	registerWrite(0x929c,0x00000002);
	// my_cntl write 0x93a0 0x00000000
	registerWrite(0x93a0,0x00000000);
	// my_cntl read 0x9298
*/

	getCFOandDTCRegisters()->SetTXRXFireflySelect(true);
	getCFOandDTCRegisters()->WriteFireflyRXIICInterface(
	    0x54 /*device*/, 0x07 /*address*/, 0 /*data*/);
	getCFOandDTCRegisters()->SetTXRXFireflySelect(false);

	//END do initial set of writes to get the live read of loss-of-light status (because it is latched value from last read)

	dtc_data_t val = 0, val2 = 0, tmpVal;
	for(int i = 0; i < 5; ++i)
	{
		usleep(1000 * 100 /* 100 ms */);
		getCFOandDTCRegisters()->SetTXRXFireflySelect(true);
		//OR := if ever 1, mark dead
		tmpVal = getCFOandDTCRegisters()->ReadFireflyRXIICInterface(0x54 /*device*/,
		                                                            0x08 /*address*/);
		val |=
		    tmpVal;  //getCFOandDTCRegisters()->ReadFireflyRXIICInterface(0x54 /*device*/, 0x08 /*address*/);
		__COUT__ << "0x08 ==> " << std::hex << " OrVal = 0x" << val << " readval = 0x"
		         << tmpVal << __E__;
		getCFOandDTCRegisters()->SetTXRXFireflySelect(false);

		usleep(1000 * 100 /* 100 ms */);
		getCFOandDTCRegisters()->SetTXRXFireflySelect(true);
		//OR := if ever 1, mark dead
		tmpVal = getCFOandDTCRegisters()->ReadFireflyRXIICInterface(0x54 /*device*/,
		                                                            0x07 /*address*/);
		val2 |= tmpVal;
		__COUT__ << "0x07 ==> " << std::hex << " OrVal = 0x" << val2 << " readval = 0x"
		         << tmpVal << __E__;
		getCFOandDTCRegisters()->SetTXRXFireflySelect(false);
	}  //end multi-read to check for strange value changing

	// #ROC0 bit 3
	rd << "{0:" << (((val2 >> (0 + 3)) & 1) ? "DEAD" : "OK");
	// #ROC1 bit 5
	rd << ", 1: " << (((val >> (0 + 5)) & 1) ? "DEAD" : "OK");
	// #ROC2 bit 2
	rd << ", 2:" << (((val2 >> (0 + 2)) & 1) ? "DEAD" : "OK");
	// #ROC3 bit 0
	rd << ", 3:" << (((val2 >> (0 + 0)) & 1) ? "DEAD" : "OK");
	// #ROC4 bit 6
	rd << ", 4: " << (((val >> (0 + 6)) & 1) ? "DEAD" : "OK");
	// #ROC5 bit 1
	rd << ", 5: " << (((val >> (0 + 1)) & 1) ? "DEAD" : "OK");
	// #CFO bit 4
	rd << ", 6/CFO: " << (((val >> (0 + 4)) & 1) ? "DEAD" : "OK");
	// #EVB bit 7  Are EVB and CFO reversed?
	rd << ", 7/EVB: " << (((val >> (0 + 7)) & 1) ? "DEAD" : "OK") << "}";

	__SET_ARG_OUT__("Link Status", rd.str());
}  //end GetLinkLossOfLight()

//========================================================================
void CFOandDTCCoreVInterface::GetFireflyTemperature(__ARGS__)
{
	std::stringstream rd;

	// // #Read Firefly RX temp registers
	// // #enable IIC on Firefly
	// // my_cntl write 0x93a0 0x00000200
	// registerWrite(0x93a0,0x00000200);
	// // #Device address, register address, null, null
	// // my_cntl write 0x9298 0x54160000
	// registerWrite(0x9298,0x54160000);
	// // #read enable
	// // my_cntl write 0x929c 0x00000002
	// registerWrite(0x929c,0x00000002);
	// // #disable IIC on Firefly
	// // my_cntl write 0x93a0 0x00000000
	// registerWrite(0x93a0,0x00000000);
	// // #read data: Device address, register address, null, temp in 2's compl.
	// // my_cntl read 0x9298

	rd << "Note: temperatures of 65C or higher should be addressed by DAQ experts.\n\n";

	getCFOandDTCRegisters()->SetTXRXFireflySelect(true);
	auto val = getCFOandDTCRegisters()->ReadFireflyRXIICInterface(0x54 /*device*/,
	                                                              0x16 /*address*/);
	getCFOandDTCRegisters()->SetTXRXFireflySelect(false);

	// dtc_data_t val = registerRead(0x9298) & 0x0FF;
	rd << std::fixed << std::setprecision(1) << uint16_t(val) << ".0 C, "
	   << double(val) * 9 / 5 + 32 << " F, <65C=" << (val < 65 ? "GOOD" : "BAD") << "\n\n"
	   << getCFOandDTCRegisters()->FormatFPGAAlarms();

	__SET_ARG_OUT__("Temperature", rd.str());
}  //end GetFireflyTemperature()

//========================================================================
void CFOandDTCCoreVInterface::GetFPGATemperature(__ARGS__)
{
	// rd << "Celsius: " << val << ", Fahrenheit: " << val*9/5 + 32 << ", " << (val < 65?"GOOD":"BAD");
	std::stringstream ss;
	ss << "Note: temperatures of 65C or higher should be addressed by DAQ experts.\n\n";
	ss << getCFOandDTCRegisters()->FormatFPGATemperature() << "\n\n"
	   << getCFOandDTCRegisters()->FormatFPGAAlarms();
	__SET_ARG_OUT__("Temperature", ss.str());
}  //end GetFPGATemperature()

//========================================================================
void CFOandDTCCoreVInterface::SelectJitterAttenuatorSource(__ARGS__)
{
	uint32_t select =
	    __GET_ARG_IN__("Source Clock (0 is from CFO, 1 is from RJ45)", uint32_t);
	select %= 4;
	__FE_COUTV__((unsigned int)select);

	// choose jitter attenuator input select (reg 0x9308, bits 5:4)
	//  0 is emulated CFO for DTC, and self-oscillator for CFO
	//  1 is RJ45 Upstream Clock
	//  2 is Timing Card Selectable (SFP+ or FPGA) Input Clock

	// char              reg_0x9308[100];
	// uint32_t          val = registerRead(0x9308);
	// std::stringstream results;

	// sprintf(reg_0x9308, "0x%8.8X", val);
	// __FE_COUTV__(reg_0x9308);
	// results << "reg_0x9118 Starting Value: " << reg_0x9308 << __E__;

	// val &= ~(3 << 4);          // clear the two bits
	// val &= ~(1);               // ensure unreset of jitter attenuator
	// val |= (select & 3) << 4;  // set the two bits to selected value

	// sprintf(reg_0x9308, "0x%8.8X", val);
	// __FE_COUTV__(reg_0x9308);
	// results << "reg_0x9118 Select Value: " << reg_0x9308 << __E__;

	// registerWrite(0x9308, val);  // write select value

	if(!__GET_ARG_IN__("DoNotSet", bool))
	{
		bool alsoResetJA = __GET_ARG_IN__("AlsoResetJA", bool);
		__FE_COUTV__(alsoResetJA);
		getCFOandDTCRegisters()->SetJitterAttenuatorSelect(select, alsoResetJA);
		sleep(1);
		for(int i = 0; i < 10; ++i)  //wait for JA to lock before reading
		{
			if(getCFOandDTCRegisters()->ReadJitterAttenuatorLocked())
				break;
			sleep(1);
		}
	}
	__FE_COUT__ << "Done with jitter attenuator source select: " << select << __E__;

	__SET_ARG_OUT__("Register Write Results",
	                getCFOandDTCRegisters()->FormatJitterAttenuatorCSR());

}  // end SelectJitterAttenuatorSource()

//========================================================================
void CFOandDTCCoreVInterface::SoftReset(__ARGS__)
{
	getCFOandDTCRegisters()->SoftReset();
}  //end SoftReset()
//========================================================================
void CFOandDTCCoreVInterface::HardReset(__ARGS__)
{
	getCFOandDTCRegisters()->HardReset();
}  //end HardReset()

//========================================================================
void CFOandDTCCoreVInterface::GetDeviceIndex(__ARGS__)
{
	__SET_ARG_OUT__("Device Index", getDevice()->getDeviceIndex());
}  //end GetDeviceIndex()

//========================================================================
uint64_t CFOandDTCCoreVInterface::convertEventDurationToClocks(
    const std::string& eventDuration)
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
		__FE_SS__
		    << "No units were found in the input parameters 'Fixed-width Event Window "
		       "Duration' value: "
		    << eventDuration
		    << ". Please use units when specifying event window duration (s, ms, us, ns, "
		       "and clocks are allowed). For example '1.7us' or '1675ns' would be valid."
		    << __E__;
		__FE_SS_THROW__;
	}
	std::string eventDurationSplitNumber = eventDuration.substr(0, i);
	std::string eventDurationSplitUnits  = eventDuration.substr(i);
	__FE_COUTV__(eventDurationSplitNumber);
	__FE_COUTV__(eventDurationSplitUnits);

	//copied from CFO_Compiler.cpp::transcribeInstructions() [L494]
	uint64_t value;
	if(!StringMacros::getNumber(eventDurationSplitNumber, value))
	{
		__FE_SS__ << "The duration parameter value '" << eventDurationSplitNumber << " "
		          << eventDurationSplitUnits << "' is not a valid number. "
		          << "Use 0x### to indicate hex and b### to indicate binary; otherwise, "
		             "decimal is inferred."
		          << __E__;
		__FE_SS_THROW__;
	}
	//test floating point in case integer conversion dropped something
	double timeValue = strtod(eventDurationSplitNumber.c_str(), 0);
	__FE_COUTV__(timeValue);
	if(timeValue < value)
		timeValue = value;

	__FE_COUTV__(FPGAClock_);
	__FE_COUTV__(value);
	__FE_COUTV__(timeValue);

	uint32_t eventDurationInClocks;

	if(eventDurationSplitUnits == "s")  // Wait wanted in seconds
		eventDurationInClocks = timeValue * 1e9 / FPGAClock_;
	else if(eventDurationSplitUnits == "ms")  // Wait wanted in milliseconds
		eventDurationInClocks = timeValue * 1e6 / FPGAClock_;
	else if(eventDurationSplitUnits == "us")  // Wait wanted in microseconds
		eventDurationInClocks = timeValue * 1e3 / FPGAClock_;
	else if(eventDurationSplitUnits == "ns")  // Wait wanted in nanoseconds
	{
		if((value % FPGAClock_) != 0)
		{
			__FE_SS__ << "FPGA can only wait in multiples of " << FPGAClock_
			          << " ns: the input event duration value '" << value
			          << "' yields a remainder of " << (value % FPGAClock_) << __E__;
			__FE_SS_THROW__;
		}
		eventDurationInClocks = value / FPGAClock_;
	}
	else if(eventDurationSplitUnits == "clocks")  // Wait wanted in FPGA clocks
		eventDurationInClocks = value;
	else  //impossible
	{
		__FE_SS__ << "The event duration input parameter is missing a valid unit type "
		             "after parameter: "
		          << eventDurationSplitUnits
		          << ". Accepted unit types are clocks, ns, us, ms, and s." << __E__;
		__FE_SS_THROW__;
	}
	if(eventDurationInClocks < 40)
	{
		__FE_SS__ << "The event duration input parameter can not evaluate to less than "
		             "40 clocks (1000ns). The input value '"
		          << eventDurationSplitNumber << " " << eventDurationSplitUnits
		          << "' evaluates to " << eventDurationInClocks << "clocks < 40."
		          << __E__;
		__FE_SS_THROW__;
	}

	return eventDurationInClocks;
}  //end convertEventDurationToClocks()

//
////==================================================================================================
// int CFOandDTCCoreVInterface::getROCLinkStatus(int ROC_link)
//{
//	int overall_link_status = registerRead(0x9140);
//
//	int ROC_link_status = (overall_link_status >> ROC_link) & 0x1;
//
//	return ROC_link_status;
//}
//
// int CFOandDTCCoreVInterface::getCFOLinkStatus()
//{
//	int overall_link_status = registerRead(0x9140);
//
//	int CFO_link_status = (overall_link_status >> 6) & 0x1;
//
//	return CFO_link_status;
//}

// std::string CFOandDTCCoreVInterface::printVoltages()
// {
// 	int adc_vccint  = registerRead(0x9014);
// 	int adc_vccaux  = registerRead(0x9018);
// 	int adc_vccbram = registerRead(0x901c);

// 	float volt_vccint  = ((float)adc_vccint / 4095.) * 3.0;
// 	float volt_vccaux  = ((float)adc_vccaux / 4095.) * 3.0;
// 	float volt_vccbram = ((float)adc_vccbram / 4095.) * 3.0;

// 	std::stringstream ss;
// 	ss << device_name_ << " VCCINT. = " << volt_vccint << " V" << __E__;
// 	ss << device_name_ << " VCCAUX. = " << volt_vccaux << " V" << __E__;
// 	ss << device_name_ << " VCCBRAM = " << volt_vccbram << " V" << __E__;
// 	__FE_COUT__ << ss.str();

// 	return ss.str();
// } //end printVoltages()
//
// int CFOandDTCCoreVInterface::checkLinkStatus()
//{
//	int ROCs_OK = 1;
//
//	for(int i = 0; i < 8; i++)
//	{
//		//__FE_COUT__ << " check link " << i << " ";
//
//		if(ROCActive(i))
//		{
//			//__FE_COUT__ << " active... status = " << getROCLinkStatus(i) << __E__;
//			ROCs_OK &= getROCLinkStatus(i);
//		}
//	}
//
//	if((getCFOLinkStatus() == 1) && ROCs_OK == 1)
//	{
//		//	__FE_COUT__ << "DTC links OK = 0x" << std::hex << registerRead(0x9140)
//		//<< std::dec << __E__;
//		//	__COUT__ << "DTC links OK = 0x" << std::hex << registerRead(0x9140) <<
//		// std::dec << __E__;
//
//		return 1;
//	}
//	else
//	{
//		//	__FE_COUT__ << "DTC links not OK = 0x" << std::hex <<
//		// registerRead(0x9140) << std::dec << __E__;
//		//	__COUT__ << "DTC links not OK = 0x" << std::hex << registerRead(0x9140)
//		//<< std::dec << __E__;
//
//		return 0;
//	}
//}
//
// bool CFOandDTCCoreVInterface::ROCActive(unsigned ROC_link)
//{
//	// __FE_COUTV__(roc_mask_);
//	// __FE_COUTV__(ROC_link);
//
//	if(((roc_mask_ >> ROC_link) & 0x01) == 1)
//	{
//		return true;
//	}
//	else
//	{
//		return false;
//	}
//}
//
////==================================================================================================
// void CFOandDTCCoreVInterface::configure(void) try
//{
//	__FE_COUTV__(getIterationIndex());
//	__FE_COUTV__(getSubIterationIndex());
//
//}  // end configure()
// catch(const std::runtime_error& e)
//{
//	__FE_SS__ << "Error caught: " << e.what() << __E__;
//	__FE_SS_THROW__;
//}
// catch(...)
//{
//	__FE_SS__ << "Unknown error caught. Check the printouts!" << __E__;
//	__FE_SS_THROW__;
//}
//
////==============================================================================
// void CFOandDTCCoreVInterface::halt(void)
//{
//}
//
////==============================================================================
// void CFOandDTCCoreVInterface::pause(void)
//{
//}
//
////==============================================================================
// void CFOandDTCCoreVInterface::resume(void)
//{
//}
//
////==============================================================================
// void CFOandDTCCoreVInterface::start(std::string runNumber)
//{
//}
//
////==============================================================================
// void CFOandDTCCoreVInterface::stop(void)
//{
//}
//
////==============================================================================
// bool CFOandDTCCoreVInterface::running(void)
//{
//	return true; //true to end loop
//}

// //=====================================
// void CFOandDTCCoreVInterface::configureJitterAttenuator(void)
// {
// 	// Start configuration preamble
// 	// set page B
// 	registerWrite(0x9168, 0x68010B00);
// 	registerWrite(0x916c, 0x00000001);

// 	// page B registers
// 	registerWrite(0x9168, 0x6824C000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68250000);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 5
// 	registerWrite(0x9168, 0x68010500);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 5 registers
// 	registerWrite(0x9168, 0x68400100);
// 	registerWrite(0x916c, 0x00000001);

// 	// End configuration preamble
// 	//
// 	// Delay 300 msec
// 	usleep(300000 /*300ms*/);

// 	// Delay is worst case time for device to complete any calibration
// 	// that is running due to device state change previous to this script
// 	// being processed.
// 	//
// 	// Start configuration registers
// 	// set page 0
// 	registerWrite(0x9168, 0x68010000);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 0 registers
// 	registerWrite(0x9168, 0x68060000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68070000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68080000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680B6800);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68160200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6817DC00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68180000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6819DD00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681ADF00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682B0200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682C0F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682D5500);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682E3700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68303700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68310000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68323700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68330000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68343700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68350000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68363700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68370000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68383700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68390000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683A3700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683C3700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683FFF00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68400400);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68410E00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68420E00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68430E00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68440E00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68450C00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68463200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68473200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68483200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68493200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684A3200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684B3200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684C3200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684D3200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684E5500);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684F5500);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68500F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68510300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68520300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68530300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68540300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68550300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68560300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68570300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68580300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68595500);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685AAA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685BAA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685C0A00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685D0100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685EAA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685FAA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68600A00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68610100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6862AA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6863AA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68640A00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68650100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6866AA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6867AA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68680A00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68690100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68920200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6893A000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68950000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68968000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68986000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689A0200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689B6000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689D0800);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689E4000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68A02000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68A20000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68A98A00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68AA6100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68AB0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68AC0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68E52100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68EA0A00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68EB6000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68EC0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68ED0000);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 1
// 	registerWrite(0x9168, 0x68010100);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 1 registers
// 	registerWrite(0x9168, 0x68020100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68120600);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68130900);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68143B00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68152800);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68170600);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68180900);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68193B00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681A2800);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683F1000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68400000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68414000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6842FF00);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 2
// 	registerWrite(0x9168, 0x68010200);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 2 registers
// 	registerWrite(0x9168, 0x68060000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68086400);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68090000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680E0100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68100000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68110000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68126400);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68130000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68140000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68150000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68160000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68170000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68180100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68190000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681C6400);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68200000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68210000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68220100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68230000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68240000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68250000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68266400);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68270000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68280000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68290000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682C0100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68310B00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68320B00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68330B00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68340B00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68350000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68360000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68370000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68388000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6839D400);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683EC000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68500000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68510000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68520000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68530000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68540000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68550000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x686B5200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x686C6500);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x686D7600);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x686E3100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x686F2000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68702000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68712000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68722000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x688A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x688B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x688C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x688D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x688E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x688F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68900000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68910000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6894B000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68960200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68970200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68990200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689DFA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689E0100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68A9CC00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68AA0400);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68AB0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68B7FF00);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 3
// 	registerWrite(0x9168, 0x68010300);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 3 registers
// 	registerWrite(0x9168, 0x68020000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68030000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68040000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68050000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68061100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68070000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68080000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68090000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680B8000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68100000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68110000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68120000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68130000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68140000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68150000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68160000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68170000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68380000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68391F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68400000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68410000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68420000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68430000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68440000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68450000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68460000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68590000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 4
// 	registerWrite(0x9168, 0x68010400);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 4 registers
// 	registerWrite(0x9168, 0x68870100);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 5
// 	registerWrite(0x9168, 0x68010500);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 5 registers
// 	registerWrite(0x9168, 0x68081000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68091F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680A0C00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680B0B00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680C3F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680D3F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680E1300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680F2700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68100900);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68110800);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68123F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68133F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68150000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68160000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68170000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68180000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x6819A800);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681A0200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681F8000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68212B00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682B0100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682C8700);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682D0300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682E1900);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682F1900);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68310000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68324200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68330300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68340000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68350000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68360000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68370000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68380000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68390000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683A0200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683B0300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683D1100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683E0600);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68890D00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x688A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689BFA00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689D1000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689E2100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x689F0C00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68A00B00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68A13F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68A23F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68A60300);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 8
// 	registerWrite(0x9168, 0x68010800);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 8 registers
// 	registerWrite(0x9168, 0x68023500);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68030500);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68040000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68050000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68060000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68070000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68080000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68090000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x680F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68100000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68110000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68120000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68130000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68140000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68150000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68160000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68170000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68180000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68190000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68200000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68210000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68220000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68230000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68240000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68250000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68260000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68270000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68280000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68290000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x682F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68300000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68310000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68320000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68330000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68340000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68350000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68360000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68370000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68380000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68390000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x683F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68400000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68410000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68420000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68430000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68440000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68450000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68460000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68470000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68480000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68490000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68500000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68510000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68520000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68530000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68540000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68550000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68560000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68570000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68580000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68590000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685B0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685C0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685D0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685F0000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68600000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68610000);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 9
// 	registerWrite(0x9168, 0x68010900);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 9 registers
// 	registerWrite(0x9168, 0x680E0200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68430100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68490F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684A0F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684E4900);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684F0200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x685E0000);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page A
// 	registerWrite(0x9168, 0x68010A00);
// 	registerWrite(0x916c, 0x00000001);

// 	// page A registers
// 	registerWrite(0x9168, 0x68020000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68030100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68040100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68050100);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68140000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x681A0000);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page B
// 	registerWrite(0x9168, 0x68010B00);
// 	registerWrite(0x916c, 0x00000001);

// 	// page B registers
// 	registerWrite(0x9168, 0x68442F00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68460000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68470000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68480000);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x684A0200);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68570E00);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68580100);
// 	registerWrite(0x916c, 0x00000001);

// 	// End configuration registers
// 	//
// 	// Start configuration postamble
// 	// set page 5
// 	registerWrite(0x9168, 0x68010500);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 5 registers
// 	registerWrite(0x9168, 0x68140100);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 0
// 	registerWrite(0x9168, 0x68010000);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 0 registers
// 	registerWrite(0x9168, 0x681C0100);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page 5
// 	registerWrite(0x9168, 0x68010500);
// 	registerWrite(0x916c, 0x00000001);

// 	// page 5 registers
// 	registerWrite(0x9168, 0x68400000);
// 	registerWrite(0x916c, 0x00000001);

// 	// set page B
// 	registerWrite(0x9168, 0x68010B00);
// 	registerWrite(0x916c, 0x00000001);

// 	// page B registers
// 	registerWrite(0x9168, 0x6824C300);
// 	registerWrite(0x916c, 0x00000001);

// 	registerWrite(0x9168, 0x68250200);
// 	registerWrite(0x916c, 0x00000001);

// 	// End configuration postamble

// 	return;
// }

// //==============================================================================
// // GetFirmwareVersion
// std::string CFOandDTCCoreVInterface::GetFirmwareVersion()
// {
// 	return ReadDesignDate
// 	dtc_data_t readData = registerRead(0x9004);
// 	// __FE_COUTV__(readData);

// 	std::stringstream dateSs;
// 	std::vector<std::string> months({"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"});
// 	int mon =  ((readData>>20)&0xF)*10 + ((readData>>16)&0xF);
// 	dateSs << months[mon-1] << "/" <<
// 		((readData>>12)&0xF) << ((readData>>8)&0xF) << "/20" <<
// 		((readData>>28)&0xF) << ((readData>>24)&0xF) << " " <<
// 		((readData>>4)&0xF) << ((readData>>0)&0xF) << ":00   raw-data: 0x" << std::hex << readData << __E__;

// 	return dateSs.str();
// }  // end GetFirmwareVersion()
// //========================================================================
// void CFOandDTCCoreVInterface::GetStatus(__ARGS__)
// {
// 	//call virtual readStatus
// 	__SET_ARG_OUT__("Status",readStatus());
// } //end GetStatus()

// //========================================================================
// void CFOandDTCCoreVInterface::ResetLinkRx(__ARGS__)
// {
// 	uint32_t link = __GET_ARG_IN__(argsIn[0].first /* first arg name */, uint32_t);
// 	link %= 8;
// 	__FE_COUTV__((unsigned int)link);

// 	// 0x9118 controls link resets
// 	//	bit-7:0 SERDES reset
// 	//	bit-15:8 PLL reset
// 	//	bit-23:16 RX reset
// 	//	bit-31:24 TX reset

// 	char reg_0x9118[100];
// 	uint32_t val = registerRead(0x9118);
// 	std::stringstream results;

// 	sprintf(reg_0x9118,"0x%8.8X",val); __FE_COUTV__(reg_0x9118);
// 	results << "reg_0x9118 Starting Value: " << reg_0x9118 << __E__;

// 	val |= 1 << (16 + link);
// 	sprintf(reg_0x9118,"0x%8.8X",val); __FE_COUTV__(reg_0x9118);
// 	results << "Link " << link << " RX reset: " << reg_0x9118 << __E__;
// 	registerWrite(0x9118, val);  // RX reset

// 	sleep(1);

// 	val &= ~(1 << (16 + link));
// 	sprintf(reg_0x9118,"0x%8.8X",val); __FE_COUTV__(reg_0x9118);
// 	results << "Link " << link << " RX unreset: " << reg_0x9118 << __E__;
// 	registerWrite(0x9118, val);  // RX unreset

// 	sleep(1);

// 	val |= 1 << (8 + link);
// 	sprintf(reg_0x9118,"0x%8.8X",val); __FE_COUTV__(reg_0x9118);
// 	results << "Link " << link << " PLL reset: " << reg_0x9118 << __E__;
// 	registerWrite(0x9118, val);  // PLL reset

// 	sleep(1);

// 	val &= ~(1 << (8 + link));
// 	sprintf(reg_0x9118,"0x%8.8X",val); __FE_COUTV__(reg_0x9118);
// 	results << "Link " << link << " PLL unreset: " << reg_0x9118 << __E__;
// 	registerWrite(0x9118, val);  // PLL unreset

// 	__SET_ARG_OUT__("Register Write Results",results.str());
// 	__FE_COUT__ << "Done with reset link Rx: " << link << __E__;

// } //end ResetLinkRx()

// //========================================================================
// // first arg must be link index or '*'
// void CFOandDTCCoreVInterface::ShutdownLinkTx(__ARGS__)
// {
// 	uint32_t link = __GET_ARG_IN__(argsIn[0].first /* first arg name */, uint32_t);
// 	link %= 8;

// 	std::string linkStr = __GET_ARG_IN__(argsIn[0].first /* first arg name */, std::string);
// 	if(linkStr == "*")
// 	{
// 		//do all links!
// 		__FE_COUT__ << "* found, so doing all links!" << __E__;
// 		link = (0xFF<<24);
// 	}
// 	else
// 	{
// 		__FE_COUTV__((unsigned int)link);
// 		link = (1<<(24+link));
// 	}

// 	//0x9118 controls link resets
// 	//	bit-7:0 SERDES reset
// 	//	bit-15:8 PLL reset
// 	//	bit-23:16 RX reset
// 	//	bit-31:24 TX reset

// 	registerWrite(0x9118,link);

// 	uint32_t val = registerRead(0x9118);

// 	std::stringstream rd;
// 	rd << "Link " << link << " SERDES reset (" << (((val>>(0+link))&1)?"RESET":"Not Reset");
// 	rd << "), \nLink " << link << "PLL reset (" << (((val>>(8+link))&1)?"RESET":"Not Reset");
// 	rd << "), \nLink " << link << "RX reset (" << (((val>>(16+link))&1)?"RESET":"Not Reset");
// 	rd << "), \nLink " << link << "TX reset (" << (((val>>(24+link))&1)?"RESET":"Not Reset");
// 	rd << ")";
// 	__SET_ARG_OUT__("Reset Status",rd.str());

// 	char readDataStr[100];
// 	sprintf(readDataStr,"0x%8.8X",val);
// 	__SET_ARG_OUT__("Link Reset Register",readDataStr);

// } //end ShutdownLinkTx()

// //========================================================================
// // first arg must be link index or '*'
// void CFOandDTCCoreVInterface::StartupLinkTx(__ARGS__)
// {
// 	uint32_t link = __GET_ARG_IN__(argsIn[0].first /* first arg name */, uint32_t);
// 	link %= 8;

// 	std::string linkStr = __GET_ARG_IN__(argsIn[0].first /* first arg name */, std::string);
// 	if(linkStr == "*")
// 	{
// 		//do all links!
// 		__FE_COUT__ << "* found, so doing all links!" << __E__;
// 		link = (0xFF<<24);
// 	}
// 	else
// 	{
// 		__FE_COUTV__((unsigned int)link);
// 		link = (1<<(24+link));
// 	}

// 	//0x9118 controls link resets
// 	//	bit-7:0 SERDES reset
// 	//	bit-15:8 PLL reset
// 	//	bit-23:16 RX reset
// 	//	bit-31:24 TX reset

// 	uint32_t val = registerRead(0x9118);
// 	uint32_t mask = ~link;

// 	registerWrite(0x9118, val&mask);

// 	val = registerRead(0x9118);

// 	std::stringstream rd;
// 	rd << "Control Link SERDES reset (" << (((val>>(0+6))&1)?"RESET":"Not Reset");
// 	rd << "), \nControl Link PLL reset (" << (((val>>(8+6))&1)?"RESET":"Not Reset");
// 	rd << "), \nControl Link RX reset (" << (((val>>(16+6))&1)?"RESET":"Not Reset");
// 	rd << "), \nControl Link TX reset (" << (((val>>(24+6))&1)?"RESET":"Not Reset");
// 	rd << ")";
// 	__SET_ARG_OUT__("Reset Status",rd.str());

// 	char readDataStr[100];
// 	sprintf(readDataStr,"0x%8.8X",val);
// 	__SET_ARG_OUT__("Link Reset Register",readDataStr);

// } //end StartupLinkTx()

// //========================================================================
// // first arg must be link index or '*'
// void CFOandDTCCoreVInterface::ShutdownFireflyTx(__ARGS__)
// {
// 	uint32_t link = __GET_ARG_IN__(argsIn[0].first /* first arg name */, uint32_t);
// 	link %= 8;

// 	std::string linkStr = __GET_ARG_IN__(argsIn[0].first /* first arg name */, std::string);
// 	if(linkStr == "*")
// 	{
// 		//do all links!
// 		__FE_COUT__ << "* found, so doing all links!" << __E__;
// 		link = (0xFF<<8);
// 	}
// 	else
// 	{
// 		__FE_COUTV__((unsigned int)link);
// 		link = (1<<(8+link));
// 	}

// 	// #turn off Firefly TX
// 	// my_cntl write 0x93a0 0x00000100
// 	// my_cntl write 0x9288 0x5052ff00
// 	// my_cntl write 0x928c 0x00000001
// 	// my_cntl write 0x93a0 0x00000000
// 	// #my_cntl read 0x9288
// 	registerWrite(0x93a0,0x00000100);
// 	registerWrite(0x9288,0x50530000 | link);
// 	registerWrite(0x928c,0x00000001);
// 	registerWrite(0x93a0,0x00000000);

// 	// my_cntl write 0x93a0 0x00000100
// 	// my_cntl write 0x9288 0x5053ff00
// 	// my_cntl write 0x928c 0x00000001
// 	// my_cntl write 0x93a0 0x00000000
// 	// #my_cntl read 0x9288
// 	usleep(1000*100 /* 100 ms */);
// 	registerWrite(0x93a0,0x00000100);
// 	registerWrite(0x9288,0x50530000 | link);
// 	registerWrite(0x928c,0x00000001);
// 	registerWrite(0x93a0,0x00000000);

// 	usleep(1000*100 /* 100 ms */);
// 	uint32_t val = registerRead(0x9288);

// 	std::stringstream rd;
// 	rd << "Link shutdown 0x" << std::hex << link <<
// 		" result 0x" << (val & 0x0FF) << std::dec;
// 	__SET_ARG_OUT__("Shutdown Status",rd.str());

// } //end ShutdownFireflyTx()

// //========================================================================
// // first arg must be link index or '*'
// void CFOandDTCCoreVInterface::StartupFireflyTx(__ARGS__)
// {
// 	uint32_t link = __GET_ARG_IN__(argsIn[0].first /* first arg name */, uint32_t);
// 	link %= 8;

// 	std::string linkStr = __GET_ARG_IN__(argsIn[0].first /* first arg name */, std::string);
// 	if(linkStr == "*")
// 	{
// 		//do all links!
// 		__FE_COUT__ << "* found, so doing all links!" << __E__;
// 		link = (0xFF<<8);
// 	}
// 	else
// 	{
// 		__FE_COUTV__((unsigned int)link);
// 		link = (1<<(8+link));
// 	}

// 	uint32_t val = registerRead(0x9288);
// 	uint32_t mask = ~link;

// 	// #turn on Firefly TX
// 	// my_cntl write 0x93a0 0x00000100
// 	// my_cntl write 0x9288 0x50520000
// 	// my_cntl write 0x928c 0x00000001
// 	// my_cntl write 0x93a0 0x00000000
// 	// #my_cntl read 0x9288
// 	registerWrite(0x93a0,0x00000100);
// 	registerWrite(0x9288,0x50520000 | (val&mask));
// 	registerWrite(0x928c,0x00000001);
// 	registerWrite(0x93a0,0x00000000);

// 	// my_cntl write 0x93a0 0x00000100
// 	// my_cntl write 0x9288 0x50530000
// 	// my_cntl write 0x928c 0x00000001
// 	// my_cntl write 0x93a0 0x00000000
// 	// #my_cntl read 0x9288
// 	usleep(1000*100 /* 100 ms */);
// 	registerWrite(0x93a0,0x00000100);
// 	registerWrite(0x9288,0x50530000 | (val&mask));
// 	registerWrite(0x928c,0x00000001);
// 	registerWrite(0x93a0,0x00000000);

// 	usleep(1000*100 /* 100 ms */);
// 	val = registerRead(0x9288);

// 	std::stringstream rd;
// 	rd << "Link startup 0x" << std::hex << link <<
// 		" result 0x" << (val & 0x0FF) << std::dec;
// 	__SET_ARG_OUT__("Startup Status",rd.str());

// } //end StartupFireflyTx()

//========================================================================
void CFOandDTCCoreVInterface::recordTimeAlive()
{
	lastTimeAliveValue_    = getCFOandDTCRegisters()->FormatDeviceTimeAlive().value;
	lastTimeAliveReadTime_ = time(0);
	__FE_COUT__ << "Recorded Time Alive register value: " << lastTimeAliveValue_ << __E__;
}  //end recordTimeAlive()

//========================================================================
void CFOandDTCCoreVInterface::testAndUpdateTimeAlive(const std::string& transitionName)
{
	// only re-read the Time Alive register if at least 2 seconds have elapsed since the last read.
	// this avoids false "board rebooted" alarms when macros or rapid transitions read the register
	// within the same wall-clock second (the 40 MHz counter may not advance enough to be visible).
	if((time(0) - lastTimeAliveReadTime_) < 2)
	{
		__FE_COUT__ << "Time Alive check skipped during '" << transitionName
		            << "' (cached within 2 seconds; last value: " << lastTimeAliveValue_
		            << ")" << __E__;
		return;
	}

	uint32_t currentTimeAliveValue =
	    getCFOandDTCRegisters()->FormatDeviceTimeAlive().value;
	if(currentTimeAliveValue < lastTimeAliveValue_)
	{
		__FE_SS__ << "Time Alive register value DECREASED during '" << transitionName
		          << "' transition! Current value: " << currentTimeAliveValue
		          << ", last recorded value: " << lastTimeAliveValue_
		          << ". This indicates the board has rebooted." << __E__;
		__FE_SS_THROW__;
	}

	if(currentTimeAliveValue == lastTimeAliveValue_)
	{
		__FE_SS__ << "Time Alive register has not increased in 2+ seconds during '"
		          << transitionName << "' (value: " << currentTimeAliveValue
		          << "). Board clock is hung or not running." << __E__;
		__FE_SS_THROW__;
	}

	lastTimeAliveValue_    = currentTimeAliveValue;
	lastTimeAliveReadTime_ = time(0);
	__FE_COUT__ << "Time Alive check passed during '" << transitionName
	            << "', updated value: " << lastTimeAliveValue_ << __E__;
}  //end testAndUpdateTimeAlive()

//========================================================================
void CFOandDTCCoreVInterface::testRTFClockInEventBuildingMode(
    const std::string& transitionName)
{
	if(operatingMode_ != CONFIG_MODE_EVENT_BUILDING_AND_SYNC)
		return;

	uint32_t jaCSRValue      = getCFOandDTCRegisters()->FormatJitterAttenuatorCSR().value;
	bool     rtfClockMissing = (jaCSRValue >> 10) & 1;
	if(rtfClockMissing)
	{
		__FE_SS__ << "RTF (RJ45) clock is missing during '" << transitionName
		          << "' transition! JA CSR register value: 0x" << std::hex << jaCSRValue
		          << std::dec << __E__;
		__FE_SS_THROW__;
	}
	__FE_COUT__ << "RTF clock check passed during '" << transitionName << "'" << __E__;
}  //end testRTFClockInEventBuildingMode()
