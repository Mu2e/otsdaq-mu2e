#ifndef _ots_DQMMu2eHistoConsumer_h_
#define _ots_DQMMu2eHistoConsumer_h_

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include "otsdaq-mu2e/ArtModules/HistoReceiver.hh"
#include "otsdaq-mu2e/DataProcessorPlugins/DQMHistoContainer.h"
#include "otsdaq/Configurable/Configurable.h"
#include "otsdaq/DataManager/DQMHistosConsumerBase.h"

class TFile;
class TDirectory;
class TH1F;
class TTree;

namespace ots
{

class DQMMu2eHistoConsumer : public DQMHistosConsumerBase, public Configurable
{
  public:
	DQMMu2eHistoConsumer(std::string              supervisorApplicationUID,
	                     std::string              bufferUID,
	                     std::string              processorUID,
	                     const ConfigurationTree& theXDAQContextConfigTree,
	                     const std::string&       configurationPath);
	virtual ~DQMMu2eHistoConsumer(void);

	void startProcessingData(std::string runNumber) override;
	void stopProcessingData(void) override;
	void pauseProcessingData(void) override;
	void resumeProcessingData(void) override;
	void load(std::string fileName) override { ; }

	/// Adds "runNumber" and "runStartEpochMs" (from the last Start), "objects"
	/// (cumulative histograms and graphs unpacked), "lastPacketAgeMs" (time since the
	/// last packet was unpacked),
	/// and one "stream_<topdir>" entry per histogram stream seen this run with
	/// "<packets>,<msSinceLast>", e.g. stream_crv = "412,830".
	std::map<std::string, std::string> getExtraStatus(void) const override;

  private:
	bool workLoopThread(toolbox::task::WorkLoop* workLoop) override;
	void fastRead(void);
	void slowRead(void);

	// For fast read
	std::string*                        dataP_;
	std::map<std::string, std::string>* headerP_;

	bool        saveFile_;  // yes or no
	std::string filePath_;
	std::string radixFileName_;

	HistoReceiver histReceiver_;

	// Status (work-loop thread writes, web thread reads; guarded by statusMutex_)
	struct StreamStat
	{
		uint64_t                              packets = 0;
		std::chrono::steady_clock::time_point last{};
	};
	mutable std::mutex                    statusMutex_;
	std::string                           runNumber_;
	int64_t                               runStartEpochMs_ = 0;  ///< wall clock at Start
	uint64_t                              objects_ = 0;  ///< cumulative, not reset per run
	std::chrono::steady_clock::time_point lastPacketTime_{};
	std::map<std::string, StreamStat>     streams_;
	//TH1F *hGauss_; // TODO need to add our own class here
	//TTree *testTree_;
	// ProtoTypeHistos* testHistos_ = new ProtoTypeHistos("Default");
	// ProtoTypeHistos                    testHistos_;
	// HistoContainer testHistos_;
};
}  // namespace ots

#endif
