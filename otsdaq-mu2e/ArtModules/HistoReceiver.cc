#include "otsdaq-mu2e/ArtModules/HistoReceiver.hh"
#include "TBufferFile.h"
#include "TH1.h"
#include "otsdaq/Macros/CoutMacros.h"

#include <algorithm>

namespace ots
{
namespace
{
constexpr UInt_t kHistoPacketMagic = 0x4f545331;  // 'OTS1'
}

void HistoReceiver::addHistogram(TH1* h, TDirectory* subdir, int mode)
{
	h->SetDirectory(nullptr);
	TH1* h_out = (TH1*)subdir->FindObjectAny(h->GetName());
	if(h_out == nullptr)
	{
		h->SetDirectory(subdir);
		return;
	}
	if(mode == kAdd)
		h_out->Add(h);
	else if(mode == kReplace)
	{
		h_out->Reset();
		h_out->Add(h);
	}
	delete h;
}
void HistoReceiver::addGraph(TGraph* g, TDirectory* subdir, int mode)
{
	TGraph* g_out = (TGraph*)subdir->FindObjectAny(g->GetName());
	if(g_out == nullptr)
	{
		TGraph* g_copy = (TGraph*)g->Clone(g->GetName());
		subdir->Append(g_copy);
		subdir->WriteTObject(g_copy, g_copy->GetName(), "Overwrite");
	}
	else
	{
		const int npoints = g->GetN();
		g_out->Set(0);
		for(int ipoint = 0; ipoint < npoints; ++ipoint)
			g_out->AddPoint(g->GetX()[ipoint], g->GetY()[ipoint]);
		subdir->WriteTObject(g_out, g_out->GetName(), "Overwrite");
	}
	delete g;
}

void HistoReceiver::addObject(TObject* readObject, TDirectory* subdir, int mode)
{
	if(readObject->InheritsFrom(TH1::Class()))
	{
		addHistogram((TH1*)readObject, subdir, mode);
		++lastObjectCount_;
		return;
	}
	if(readObject->InheritsFrom(TGraph::Class()))
	{
		addGraph((TGraph*)readObject, subdir, mode);
		++lastObjectCount_;
		return;
	}
	__COUT__ << "[HistoReceiver::" << __func__ << "] Unknown object type with name "
	         << readObject->GetName() << std::endl;
	delete readObject;
}

std::vector<std::string> HistoReceiver::readPacket(TDirectory* dir, std::string* buf)
{
	std::vector<std::string> topDirs;
	lastObjectCount_ = 0;

	TBufferFile message(TBuffer::kWrite);        // prepare message
	message.WriteBuf(buf->data(), buf->size());  // copy buffer
	message.SetReadMode();
	message.SetBufferOffset(0);  // move pointer

	std::string directoryNameStdString;
	while(message.Length() < message.BufferSize())
	{
		message.ReadStdString(directoryNameStdString);
		if(directoryNameStdString.empty())
			break;
		// check if the plotting mode was defined
		int mode(kAdd);
		if(directoryNameStdString.find(":") != std::string::npos)
		{
			mode = parseMode(
			    directoryNameStdString.substr(directoryNameStdString.find(":") + 1));
			directoryNameStdString =
			    directoryNameStdString.substr(0, directoryNameStdString.find(":"));
		}
		{
			std::string top = directoryNameStdString.substr(
			    0, directoryNameStdString.find('/'));  // npos -> whole string
			if(!top.empty() &&
			   std::find(topDirs.begin(), topDirs.end(), top) == topDirs.end())
				topDirs.push_back(top);
		}
		TString directoryName(directoryNameStdString);

		__COUT__ << "[HistoReceiver::readPacket] Moving in dir: "
		         << directoryNameStdString << std::endl;

		TDirectory* subdir = dir;
		TString     dirStr;
		Ssiz_t      from = 0;
		while(directoryName.Tokenize(dirStr, from, "/"))
		{
			subdir = subdir->mkdir(dirStr.Data(), "", kTRUE);
		}

		// New framing: magic + object count per directory block.
		auto   payloadOffset   = message.Length();
		UInt_t magic           = 0;
		UInt_t objectCount     = 0;
		bool   useCountFraming = false;
		if(static_cast<size_t>(message.BufferSize() - message.Length()) >=
		   sizeof(UInt_t) * 2)
		{
			message >> magic;
			if(magic == kHistoPacketMagic)
			{
				message >> objectCount;
				useCountFraming = true;
			}
			else
			{
				message.SetBufferOffset(payloadOffset);
			}
		}

		if(useCountFraming)
		{
			for(UInt_t i = 0; i < objectCount; ++i)
			{
				TObject* readObject = (TObject*)message.ReadObjectAny(TObject::Class());
				if(readObject == nullptr)
				{
					__COUT__
					    << "[HistoReceiver::" << __func__
					    << "] Unexpected null object in counted payload for directory "
					    << directoryNameStdString << ", index " << i << std::endl;
					break;
				}
				addObject(readObject, subdir, mode);
			}
		}
		else
		{
			// Legacy fallback: probe until non-object boundary is reached.
			TObject* readObject = nullptr;
			do
			{
				auto lengthBefore = message.Length();
				readObject        = (TObject*)message.ReadObjectAny(TObject::Class());
				if(readObject != nullptr)
				{
					addObject(readObject, subdir, mode);
				}
				else
				{
					message.SetBufferOffset(lengthBefore);
				}
			} while(readObject != nullptr);
		}

		dir->cd();
	}
	buf = nullptr;
	return topDirs;
}

int HistoReceiver::parseMode(std::string mode)
{
	if(mode == "replace")
		return kReplace;
	if(mode == "add")
		return kAdd;
	return kAdd;  //default to adding
}

}  // namespace ots
