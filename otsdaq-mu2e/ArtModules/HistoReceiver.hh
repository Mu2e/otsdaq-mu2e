#include <TDirectory.h>
#include <TGraph.h>
#include <TH1.h>
#include <TObject.h>
#include <string>
#include <vector>
#include "otsdaq/NetworkUtilities/TCPSendClient.h"

namespace ots
{

class HistoReceiver
{
  public:
	enum
	{
		kAdd,
		kReplace
	};
	void addHistogram(TH1* h, TDirectory* subdir, int mode);
	void addGraph(TGraph* g, TDirectory* subdir, int mode);
	void addObject(TObject* readObject, TDirectory* subdir, int mode);
	/// Unpacks one packet into dir. Returns the top-level directory name of every
	/// block in the packet (e.g. "crv", "CaloDQM"), which identifies the stream.
	std::vector<std::string> readPacket(TDirectory* dir, std::string* buf);
	int                      parseMode(std::string mode);
	/// Histograms and graphs placed into dir by the most recent readPacket call.
	unsigned int lastObjectCount(void) const { return lastObjectCount_; }

  private:
	unsigned int lastObjectCount_ = 0;
};
}  // namespace ots
