#ifndef _ots_DTCFrontEndInterface_h_
#define _ots_DTCFrontEndInterface_h_

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include "dtcInterfaceLib/DTC.h"
#include "dtcInterfaceLib/DTCSoftwareCFO.h"
#include "mu2e_driver/mu2e_mmap_ioctl.h"  // m_ioc_cmd_t, m_ioc_reg_access_t, dtc_address_t, dtc_data_t
#include "otsdaq-mu2e/CFOandDTCCore/CFOandDTCCoreVInterface.h"
#include "otsdaq-mu2e/ROCCore/ROCCoreVInterface.h"

namespace ots
{

class DTCFrontEndInterface : public CFOandDTCCoreVInterface
{
  public:
	DTCFrontEndInterface(const std::string&       interfaceUID,
	                     const ConfigurationTree& theXDAQContextConfigTree,
	                     const std::string&       interfaceConfigurationPath);
	virtual ~DTCFrontEndInterface(void);
	void setParentPointers(CoreSupervisorBase*   supervisor,
	                       FEVInterfacesManager* manager) override;

	void DTCInstantiate();

	// specialized ROC handling slow controls
	//----------------
	virtual void                   configureSlowControls(void) override;
	virtual void                   resetSlowControlsChannelIterator(void) override;
	virtual FESlowControlsChannel* getNextSlowControlsChannel(void) override;
	virtual unsigned int           getSlowControlsChannelCount(void) override;

  public:
	// state machine
	//----------------
	void         configure(void) override;
	void         halt(void) override;
	void         pause(void) override;
	void         resume(void) override;
	void         start(std::string runNumber) override;
	void         stop(void) override;
	bool         running(void) override;
	unsigned int getMinReadyForEventGenerationStartIteration(void) const override;

	// emulator handlers
	//----------------
	void emulatorConfigure(void);

	// hardware access
	//----------------
	virtual mu2edev* getDevice(void) override
	{
		if(!thisDTC_)
		{
			__SS__ << "thisDTC_ pointer has not been initialized! "
			       << StringMacros::stackTrace();
			__SS_THROW__;
		}
		return thisDTC_->GetDevice();
	};
	virtual CFOandDTC_Registers* getCFOandDTCRegisters(void) override
	{
		if(!thisDTC_)
		{
			__SS__ << "thisDTC_ pointer has not been initialized! "
			       << StringMacros::stackTrace();
			__SS_THROW__;
		}
		return thisDTC_;
	};
	inline DTCLib::DTC* getDTC(void)
	{
		if(!thisDTC_)
		{
			__SS__ << "thisDTC_ pointer has not been initialized! "
			       << StringMacros::stackTrace();
			__SS_THROW__;
		}
		return thisDTC_;
	};

	// DTC specific items
	//----------------
	void configureHardwareDevMode(void);
	void configureEventBuildingMode(int step = -1);
	void configureLoopbackMode(int step = -1);
	void configureForTimingChain(int step = -1);
	void configureCommon(void);

	void loopbackTest(int step = -1);

	DTCLib::DTC* thisDTC_;

	struct DetachedBufferTestThreadStruct
	{
		std::mutex        lock_;
		std::atomic<bool> running_            = false;
		std::atomic<bool> exitThread_         = false;
		std::atomic<bool> resetStartEventTag_ = false;
		std::atomic<bool> releaseAllComplete_ = false;  ///< Set true by the detached buffer-test thread immediately after its ReleaseAllBuffers() returns; consumers (e.g. SetCFOEmulatorFixedWidthEmulation) wait on this before enabling CFO emulation so emulation does not start while the driver is still draining DMA buffers.

		DTCLib::DTC* thisDTC_;

		bool                                                       inSubeventMode_  = false;
		bool                                                       inEVBMode_       = false;
		uint8_t                                                    evbNumDestNodes_ = 1;
		std::set<uint8_t>                                          evbSourcesSeenForTag_;        // EVB mode: source DTC IDs that have delivered the current expected tag
		bool                                                       evbTagSynced_ = false;        // EVB mode: expected tag has been synced to the first subevent seen after (re)start
		std::map<uint8_t /*source_dtc_id*/, std::vector<uint64_t>> evbRocFragmentsBySource_;     // EVB mode: ROC fragment counts per source DTC, per link
		std::map<uint8_t /*source_dtc_id*/, std::vector<uint64_t>> evbRocPayloadBytesBySource_;  // EVB mode: ROC payload bytes per source DTC, per link
		bool                                                       activeMatch_      = false;
		std::atomic<uint64_t>                                      expectedEventTag_ = -1, nextEventWindowTag_ = -1;
		bool                                                       saveBinaryData_                  = false;
		bool                                                       saveSubeventHeadersToBinaryData_ = false;
		bool                                                       doNotResetCounters_              = false;
		bool                                                       skipBy32_                        = false;

		std::atomic<uint64_t>                      eventsCount_;
		std::atomic<uint64_t>                      subeventsCount_;
		std::atomic<uint64_t>                      mismatchedEventTagsCount_;
		std::vector<std::pair<uint64_t, uint64_t>> mismatchedEventTagJumps_;

		std::atomic<uint64_t> subrunTransitionCount_;
		bool                  lastSubrunBit_ = false;

		std::vector<uint64_t> rocFragmentsCount_, rocFragmentTimeoutsCount_,
		    rocFragmentErrorsCount_, rocPayloadEmptyCount_, rocHeaderTimeoutsCount_,
		    rocPayloadByteCount_;
		std::atomic<uint64_t> evbChunksCount_{0};
		std::atomic<uint64_t> evbTotalDataWordsRead_{0};
		std::atomic<uint64_t> evbCloseFillersCount_{0};
		std::atomic<uint64_t> evbFramingErrors_{0};
		std::atomic<uint32_t> evbStickyErrorsSeen_{0};
		uint32_t             evbStickyIgnoreMask_{0};
		std::atomic<bool> evbTrafficStarted_{false};
		std::atomic<bool> evbStatusReadFailed_{false};
		// 0x9370 sampled on the first idle iteration after the last subevent arrived (~1 loop
		// iteration late, vs ~2 s late for the timeout snapshot); re-armed whenever data resumes
		std::atomic<bool>                                  evbErrAtStallOnsetValid_{false};
		std::atomic<uint32_t>                              evbErrAtStallOnset_{0};
		std::atomic<uint64_t>                              evbErrAtStallOnsetIter_{0};
		std::chrono::time_point<std::chrono::steady_clock> evbErrAtStallOnsetTime_;

		uint64_t                                           totalSubeventBytesTransferred_;
		std::chrono::time_point<std::chrono::steady_clock> transferStartTime_,
		    transferEndTime_;

		FILE* fp_ = nullptr;

		std::string error_;
		std::string saveBinaryDataFilename_;

		unsigned int          packetThresholdToSave_;
		std::atomic<uint64_t> savedCount_;

		std::map<DTCLib::DTC_Link_ID, bool> rocLinkEnabledLatch_;

	};  // end DetachedBufferTestThreadStruct declaration

	static std::string getDetachedBufferTestStatus(
	    std::shared_ptr<DTCFrontEndInterface::DetachedBufferTestThreadStruct>
	        threadStruct);
	static std::string getDetachedBufferTestEVBStatus(
	    std::shared_ptr<DTCFrontEndInterface::DetachedBufferTestThreadStruct>
	        threadStruct);
	static uint64_t getDetachedBufferTestReceivedCount(
	    std::shared_ptr<DTCFrontEndInterface::DetachedBufferTestThreadStruct>
	        threadStruct);
	static void handleDetachedSubevent(
	    const DTCLib::DTC_SubEvent& subevent,
	    std::shared_ptr<DTCFrontEndInterface::DetachedBufferTestThreadStruct>
	        threadStruct);

	void initDetachedBufferTest(uint64_t           initialEventWindowTag,
	                            bool               saveBinaryDataToFile,
	                            const std::string& filename,
	                            bool               saveSubeventHeadersToDataFile,
	                            bool               doNotResetCounters,
	                            bool               skipBy32,
	                            uint32_t           packetThresholdToSave,
	                            bool               inEVBMode = false);

	std::shared_ptr<DTCFrontEndInterface::DetachedBufferTestThreadStruct>
	    bufferTestThreadStruct_;

  private:
	void        createROCs(void);
	void        registerFEMacros(void);
	std::string getEVBWireParity(void);

	int                     timing_chain_first_substep_ = -1;
	bool                    rtfPhaseEdgeRetried_        = false;
	std::string             rtfPhaseEdgeRetryDetail_;  // populated when edge-flip retry runs, included in error if verify still fails
	int                     dtc_location_in_chain_ = -1;
	unsigned int            runningCallCount_      = 0;
	unsigned int            roc_mask_              = 0;
	unsigned int            roc_emulated_mask_     = 0;
	bool                    has_real_roc_flow_     = false;
	std::string             real_roc_flow_reason_;
	bool                    emulate_cfo_ = true;
	DTCLib::DTCSoftwareCFO* EmulatedCFO_;
	uint64_t                next_starting_cfoem_event_window_tag_ = 0;

	std::ofstream datafile_[8];

	struct EVBBRAMSnapshot
	{
		std::chrono::steady_clock::time_point         timestamp;
		std::map<uint16_t /*type<<8|slot*/, uint32_t> values;
	};
	EVBBRAMSnapshot evbBRAMSnapshot_;

	std::map<std::string /*ROC UID*/, std::unique_ptr<ROCCoreVInterface>> rocs_;
	std::map<DTCLib::DTC_Link_ID, bool>                                   rocRunningStatus_;

	std::map<std::string /*DTC's FEMacro name*/,
	         std::pair<std::string /*ROC UID*/, std::string /*ROC's FEMacro name*/>>
	    rocFEMacroMap_;

	static void detachedBufferTestThread(
	    std::shared_ptr<DTCFrontEndInterface::DetachedBufferTestThreadStruct>
	        threadStruct);

  public:
	void        SetupROCs(__ARGS__);
	std::string SetupROCs(DTCLib::DTC_Link_ID            rocLinkIndex,
	                      bool                           rocRxTxEnable,
	                      bool                           rocTimingEnable,
	                      bool                           rocEmulationEnable,
	                      DTCLib::DTC_ROC_Emulation_Type rocEmulationType,
	                      uint32_t                       size,
	                      bool                           blockNullHeartbeats     = false,
	                      bool                           resequenceNonNullEvents = false,
	                      bool                           autoGenDRPPerROC        = false);
	void        ReadROC(__ARGS__);
	void        ROCFirmwareInventory(__ARGS__);
	void        ListFirmwareDirectory(__ARGS__);
	void        WriteROC(__ARGS__);
	void        BlockReadROC(__ARGS__);
	void        BlockWriteROC(__ARGS__);
	void        WriteExternalROCRegister(__ARGS__);
	void        ReadExternalROCRegister(__ARGS__);
	void        DTCHighRateBlockCheck(__ARGS__);

	void DTCHighRateDCSCheck(__ARGS__);
	void RunROCFEMacro(__ARGS__);
	void DTCSendHeartbeatAndDataRequest(__ARGS__);
	void ResetLossOfLockCounter(__ARGS__);
	void ReadLossOfLockCounter(__ARGS__);
	void SpyBuffer(__ARGS__);
	void ReleaseAllDAQBuffers(__ARGS__);
	void GetLinkLockStatus(__ARGS__);
	void SelectJitterAttenuatorSource(__ARGS__);
	void WriteDTC(__ARGS__);
	void ReadDTC(__ARGS__);
	void SetCFOEventModeRequiredMask(__ARGS__);
	void ReadCFOEventModeRequiredMask(__ARGS__);

	void configureHardwareDevMode(__ARGS__);
	void ConfigureForTimingChain(__ARGS__);

	std::string getCFORTFSettingsStatusAndErrors();

	void DTCCounters(__ARGS__);
	void readRxDiagFIFO(__ARGS__);
	void readTxDiagFIFO(__ARGS__);
	void GetLinkErrors(__ARGS__);
	void GetRTFInterfaceStatus(__ARGS__);
	void RTFMarkerOffsetApply(__ARGS__);
	void FixCFOClockEdge(__ARGS__);
	void EVBHighLevelCounters(__ARGS__);
	void ROCResetLink(__ARGS__);
	void HeaderFormatTest(__ARGS__);

	void DTCInstantiate(__ARGS__);
	void ResetDTCLinks(__ARGS__);
	void EnableDTCLink(__ARGS__);

	void SoftReset(__ARGS__) override;

	void ResetPCIe(__ARGS__);
	void ResetCFOLinkRx(__ARGS__);
	void ResetCFOLinkTx(__ARGS__);
	void ResetCFOLinkRxPLL(__ARGS__);
	void ResetCFOLinkTxPLL(__ARGS__);

	void GetDTCIdAndEVBInfo(__ARGS__);
	void SetDTCIdAndEVBInfo(__ARGS__);
	void EVBInit(__ARGS__);
	void EVBStatus(__ARGS__);

	// void 								ResetEVBLinkRx						(__ARGS__);
	// void 								ResetEVBLinkTx						(__ARGS__);
	// void 								ResetEVBLinkRxTxPLL					(__ARGS__);

	void        SetupCFOInterface(__ARGS__);
	std::string SetupCFOInterface(int  forceCFOedge,
	                              bool useCFOemulator,
	                              bool alsoSetupJA,
	                              bool cfoRxTxEnable,
	                              bool enableAutogenDRP,
	                              int  permanentOffset     = 0,
	                              int  idelayTapValue      = -1,
	                              int  rtfPunchedClockEdge = 0);
	void        SetCFOEmulatorOnOffSpillEmulation(__ARGS__);
	std::string SetCFOEmulatorOnOffSpillEmulation(bool               enable,
	                                              bool               useDetachedBufferTest,
	                                              uint32_t           numberOfSuperCycles,
	                                              uint64_t           initialEventWindowTag,
	                                              bool               enableClockMarkers,
	                                              bool               enableAutogenDRP,
	                                              bool               saveBinaryDataToFile,
	                                              const std::string& filename,
	                                              bool               saveSubeventHeadersToDataFile,
	                                              bool               doNotResetCounters,
	                                              bool               skipBy32,
	                                              uint32_t           packetThresholdToSave,
	                                              bool               inEVBMode = false);
	void        SetCFOEmulatorFixedWidthEmulation(__ARGS__);
	std::string SetCFOEmulatorFixedWidthEmulation(bool               enable,
	                                              bool               useDetachedBufferTest,
	                                              const std::string& eventDuration,
	                                              uint32_t           numberOfEventWindowMarkers,
	                                              uint64_t           initialEventWindowTag,
	                                              uint64_t           eventWindowMode,
	                                              bool               enableClockMarkers,
	                                              bool               enableAutogenDRP,
	                                              bool               saveBinaryDataToFile,
	                                              const std::string& filename,
	                                              bool               saveSubeventHeadersToDataFile,
	                                              bool               doNotResetCounters,
	                                              bool               skipBy32,
	                                              uint32_t           packetThresholdToSave,
	                                              bool               inEVBMode = false);

	void BufferTest(__ARGS__);
	void PatternTest(__ARGS__);
	void BufferTest_detached(__ARGS__);

	void SoftwareDataRequest(__ARGS__);
	void PunchedClock(__ARGS__);

	void CFOEmulatorLoopbackTest(__ARGS__);
	void CFOEmulatorLoopbackTests(__ARGS__);
	void ManualLoopbackSetup(__ARGS__);

	void ProgramROCs(__ARGS__);

	void ValidateDTCControlRegisters(__ARGS__);
};
}  // namespace ots
#endif
