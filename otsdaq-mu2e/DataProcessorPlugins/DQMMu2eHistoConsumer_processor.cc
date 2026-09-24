#include <TBufferFile.h>
#include <TDirectory.h>
#include <TFile.h>
#include <TH1F.h>
#include <TTree.h>
#include <chrono>
#include <thread>
#include "otsdaq-mu2e/DataProcessorPlugins/DQMMu2eHistoConsumer.h"
#include "otsdaq/Macros/CoutMacros.h"
#include "otsdaq/Macros/ProcessorPluginMacros.h"
#include "otsdaq/MessageFacility/MessageFacility.h"

using namespace ots;

//========================================================================================================================
DQMMu2eHistoConsumer::DQMMu2eHistoConsumer(
    std::string              supervisorApplicationUID,
    std::string              bufferUID,
    std::string              processorUID,
    const ConfigurationTree& theXDAQContextConfigTree,
    const std::string&       configurationPath)
    : WorkLoop(processorUID)
    , DQMHistosConsumerBase(
          supervisorApplicationUID, bufferUID, processorUID, LowConsumerPriority)
    , Configurable(theXDAQContextConfigTree, configurationPath)
    , saveFile_(theXDAQContextConfigTree.getNode(configurationPath)
                    .getNode("SaveFile")
                    .getValue<bool>())
    , filePath_(theXDAQContextConfigTree.getNode(configurationPath)
                    .getNode("FilePath")
                    .getValue<std::string>())
    , radixFileName_(theXDAQContextConfigTree.getNode(configurationPath)
                         .getNode("RadixFileName")
                         .getValue<std::string>())

{
	// std::cout << "[In DQMMu2eHistoConsumer () ] Initiating ..." << std::endl;
}

//========================================================================================================================
DQMMu2eHistoConsumer::~DQMMu2eHistoConsumer(void) { DQMHistosBase::closeFile(); }
//========================================================================================================================
void DQMMu2eHistoConsumer::startProcessingData(std::string runNumber)
{
	// std::cout << __PRETTY_FUNCTION__
	//           << filePath_ + "/" + radixFileName_ + "_Run" + runNumber + ".root"
	//           << std::endl;
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		runNumber_       = runNumber;
		runStartEpochMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
		                       std::chrono::system_clock::now().time_since_epoch())
		                       .count();
		streams_.clear();
		lastPacketTime_ = std::chrono::steady_clock::time_point{};
	}
	DQMHistosBase::openFile(filePath_ + "/" + radixFileName_ + "_Run" + runNumber +
	                        ".root");
	DQMHistosBase::myDirectory_ =
	    DQMHistosBase::theFile_->mkdir("Mu2eHistos", "Mu2eHistos");
	DQMHistosBase::myDirectory_->cd();

	// for (int station = 0; station < 1; station++) {
	//   for (int plane = 0; plane < 2; plane++) {
	//     for (int panel = 0; panel < 6; panel++) {
	//       for (int straw = 0; straw < 96; straw++) {
	//         testHistos_.BookHistos(DQMHistosBase::myDirectory_,
	//                                "Pedestal" + std::to_string(station) + " " +
	//                                    std::to_string(plane) + " " +
	//                                    std::to_string(panel) + " " +
	//                                    std::to_string(straw));
	//       }
	//     }
	//   }
	// }

	// for (int station = 0; station < 1; station++) {
	//   for (int plane = 0; plane < 2; plane++) {
	//     for (int panel = 0; panel < 6; panel++) {
	//       testHistos_.BookHistos(DQMHistosBase::myDirectory_,
	//                              "Straw Hits" + std::to_string(station) + " " +
	//                                  std::to_string(plane) + " " +
	//                                  std::to_string(panel) + " " +
	//                                  std::to_string(0));
	//     }
	//   }
	// }

	// testHistos_.BookHistos(DQMHistosBase::myDirectory_, "Pedestal");
	// testHistos_.BookHistos(DQMHistosBase::myDirectory_, "deltaTT");
	// std::cout << __PRETTY_FUNCTION__ << "Starting!" << std::endl;
	DataConsumer::startProcessingData(runNumber);
	// std::cout << __PRETTY_FUNCTION__ << "Started!" << std::endl;
}

//========================================================================================================================
void DQMMu2eHistoConsumer::stopProcessingData(void)
{
	// std::cout << "[In DQMMu2eHistoConsumer () ] Stopping ..." << std::endl;

	DataConsumer::stopProcessingData();
	if(saveFile_)
	{
		// std::cout << "[In DQMMu2eHistoConsumer () ] Saving ..." << std::endl;
		DQMHistosBase::save();
	}
	closeFile();
}

//========================================================================================================================
void DQMMu2eHistoConsumer::pauseProcessingData(void)
{
	// std::cout << "[In DQMMu2eHistoConsumer () ] Pausing ..." << std::endl;
	DataConsumer::stopProcessingData();
}

//========================================================================================================================
void DQMMu2eHistoConsumer::resumeProcessingData(void)
{
	// std::cout << "[DQMMu2eHistoConsumer::resumeProcessingData] Resuming ..." << std::endl;
	DataConsumer::startProcessingData("");
}

//========================================================================================================================
bool DQMMu2eHistoConsumer::workLoopThread(toolbox::task::WorkLoop* workLoop)
{
	// std::cout<<"[In DQMMu2eHistoConsumer () ] CallingFastRead ..."<<std::endl;
	fastRead();
	return WorkLoop::continueWorkLoop_;
}

//========================================================================================================================
void DQMMu2eHistoConsumer::fastRead(void)
{
	if(DataConsumer::read(dataP_, headerP_) < 0 || dataP_ == nullptr ||
	   headerP_ == nullptr)  // is there something in the buffer?
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 10
		//__CFG_COUT__ << "There is nothing in the buffer" << std::endl;
		return;
	}
	// std::cout << "[DQMMu2eHistoConsumer::fastRead] reading BUFFER..." << std::endl;
	const uint64_t bytes = dataP_->size();
	auto           t0    = std::chrono::steady_clock::now();
	std::vector<std::string> topDirs =
	    histReceiver_.readPacket(DQMHistosBase::myDirectory_, dataP_);
	auto t1 = std::chrono::steady_clock::now();
	countBusyNanos(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
	countPacket(bytes);
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		lastPacketTime_ = t1;
		objects_ += histReceiver_.lastObjectCount();
		for(auto const& top : topDirs)
		{
			StreamStat& s = streams_[top];
			++s.packets;
			s.last = t1;
		}
	}
	DataConsumer::setReadSubBuffer<std::string, std::map<std::string, std::string>>();
}

//========================================================================================================================
std::map<std::string, std::string> DQMMu2eHistoConsumer::getExtraStatus(void) const
{
	std::map<std::string, std::string> status;
	auto                               now = std::chrono::steady_clock::now();
	auto ageMs = [&now](std::chrono::steady_clock::time_point t) -> std::string {
		if(t == std::chrono::steady_clock::time_point{})
			return "-1";  // never
		return std::to_string(
		    std::chrono::duration_cast<std::chrono::milliseconds>(now - t).count());
	};

	std::lock_guard<std::mutex> lock(statusMutex_);
	status["runNumber"]       = runNumber_;
	status["runStartEpochMs"] = std::to_string(runStartEpochMs_);
	status["objects"]         = std::to_string(objects_);
	status["lastPacketAgeMs"] = ageMs(lastPacketTime_);
	for(auto const& stream : streams_)
		status["stream_" + stream.first] =
		    std::to_string(stream.second.packets) + "," + ageMs(stream.second.last);
	return status;
}

DEFINE_OTS_PROCESSOR(DQMMu2eHistoConsumer)
