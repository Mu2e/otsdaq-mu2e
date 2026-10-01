#ifndef _ots_CFOandDTCCoreVInterface_h_
#define _ots_CFOandDTCCoreVInterface_h_

#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include "cfoInterfaceLib/CFO.h"
#include "cfoInterfaceLib/CFO_Compiler.hh"
#include "dtcInterfaceLib/CFOandDTC_Registers.h"
#include "dtcInterfaceLib/mu2edev.h"
#include "mu2e_driver/mu2e_mmap_ioctl.h"  // m_ioc_cmd_t, m_ioc_reg_access_t, dtc_address_t, dtc_data_t

#include "otsdaq/FECore/FEVInterface.h"

namespace ots
{
class CFOandDTCCoreVInterface : public FEVInterface
{
  public:
	CFOandDTCCoreVInterface(const std::string&       interfaceUID,
	                        const ConfigurationTree& theXDAQContextConfigTree,
	                        const std::string&       interfaceConfigurationPath);

	virtual ~CFOandDTCCoreVInterface(void);

	// specialized handling of slow controls
	//----------------
	void outputEpicsPVFile(ConfigurationManager* configManager);

  public:
	static std::string CONFIG_MODE_HARDWARE_DEV;
	static std::string CONFIG_MODE_EVENT_BUILDING;
	static std::string CONFIG_MODE_EVENT_BUILDING_AND_SYNC;
	static std::string CONFIG_MODE_LOOPBACK;

	// state machine
	//----------------
	//	void 								configure					(void);
	//	void 								halt						(void);
	//	void 								pause						(void);
	//	void 								resume						(void);
	//	void 								start						(std::string runNumber);
	//	void 								stop						(void);
	//	bool 								running						(void);

	// emulator handlers
	//----------------
	//	void 								emulatorConfigure			(void);

	// hardware access
	//----------------
	void                                 universalRead(char* address, char* readValue) override;
	void                                 universalWrite(char* address, char* writeValue) override;
	virtual mu2edev*                     getDevice(void)             = 0;
	virtual DTCLib::CFOandDTC_Registers* getCFOandDTCRegisters(void) = 0;

	// DTC specific items
	//----------------
	// void  								configureJitterAttenuator	(void);
	// float 								readTemperature				(void);  // return temperature of FPGA in degC
	// std::string							printVoltages				(void);

	// void 								turnOnLED					(void);  // turn on LED on visible side of timing card
	// void 								turnOffLED					(void);  // turn off LED on visible side of timing card

  protected:
	void     registerCFOandDTCFEMacros(void);
	uint64_t convertEventDurationToClocks(const std::string& eventDuration);
	void     recordTimeAlive();
	void     testAndUpdateTimeAlive(const std::string& transitionName);
	void     testRTFClockInEventBuildingMode(const std::string& transitionName);

  protected:
	int         deviceIndex_           = -1;  //PCIe index
	bool        configure_clock_       = false;
	bool        emulatorMode_          = false;
	bool        skipInit_              = true;
	std::string operatingMode_         = "";
	uint32_t    lastTimeAliveValue_    = 0;
	time_t      lastTimeAliveReadTime_ = 0;  // only re-read if >= +2 seconds have elapsed

	// Configure iteration layout for EventBuildingMode.
	static const int CONFIG_PHASE_ESTABLISH_CLOCKS_A            = 0;   // Phase 1a: Establish Clocks — CFO + DTCs w/o real ROCs
	static const int CONFIG_PHASE_ESTABLISH_CLOCKS_B            = 1;   // Phase 1b: Establish Clocks — DTCs w/ real ROCs
	static const int CONFIG_PHASE_ESTABLISH_TIMING_CHAIN_ENABLE = 2;   // Phase 2a: CFO enables links + clock markers
	static const int CONFIG_PHASE_ESTABLISH_TIMING_CHAIN_CHECK  = 3;   // Phase 2b: CDR lock check
	static const int CONFIG_PHASE_CFO_EDGE_FIX                  = 4;   // Phase 2c: CFO-coordinated iterative edge fix
	static const int CONFIG_PHASE_ESTABLISH_SYNC_A              = 5;   // Phase 3a: Sync — edge fix (no-ROC DTCs)
	static const int CONFIG_PHASE_ESTABLISH_SYNC_B              = 6;   // Phase 3b: Sync — edge fix (ROC DTCs)
	static const int CONFIG_PHASE_ESTABLISH_SYNC_C              = 7;   // Phase 3c: Sync — RTF offset + verify (no-ROC DTCs)
	static const int CONFIG_PHASE_ESTABLISH_SYNC_D              = 8;   // Phase 3d: Sync — RTF offset + verify (ROC DTCs)
	static const int CONFIG_PHASE_ESTABLISH_SYNC_E              = 9;   // Phase 3e: Stop CFO events after sync phases
	static const int CONFIG_PHASE_ESTABLISH_ROC_CONFIG          = 10;  // Phase 4: ROC and DCS Setup — DTCs w/ real ROCs only
	static const int CONFIG_PHASE_ROC_DATA_PATH                 = 11;  // Phase 5: ROC Data Path Setup — DTCs w/ real ROCs only
	static const int CONFIG_PHASE_FINAL_SOFT_RESET              = 12;  // Final SoftReset before enabling CFO operation
	static const int CONFIG_CFO_EVENT_SENDING_START_ITERATION   = 13;  // Phase 6: Enable CFO Operation — last iteration

	bool artdaqMode_ = false;  // true to prevent run data file generation

	const uint64_t FPGAClock_ = CFOLib::CFO_Compiler::
	    FPGAClock_;  //period of FPGA clock in ns (as of Feb 2026, was 25ns)

  public:
	virtual void SoftReset(__ARGS__);
	void         HardReset(__ARGS__);

	void GetFirmwareVersion(__ARGS__);
	void ResetPCIe(__ARGS__);
	void FlashLEDs(__ARGS__);
	void GetStatus(__ARGS__);
	void GetSimpleStatus(__ARGS__);
	void GetLinkLossOfLight(__ARGS__);
	void GetFireflyTemperature(__ARGS__);
	void GetFPGATemperature(__ARGS__);
	void SelectJitterAttenuatorSource(__ARGS__);
	void GetDeviceIndex(__ARGS__);

	// void								ResetLinkRx					(__ARGS__);
	// void								ShutdownLinkTx				(__ARGS__);
	// void								StartupLinkTx				(__ARGS__);
	// void								ShutdownFireflyTx			(__ARGS__);
	// void								StartupFireflyTx			(__ARGS__);
};
}  // namespace ots
#endif
