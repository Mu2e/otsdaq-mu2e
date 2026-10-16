// RocFlashProgrammer.h — safe and fast remote programming of DiRAC ROCs over the DTC DCS link.
//
// Header only, no OTS dependency: the DTC front-end wraps each ROC in a RocPort (register read, block
// write/read on the DCS link) and calls FlashSession::run(). The same code runs against a simulated MiV
// in test_roc_flash.cc.
//
// What it guarantees (2026-10-15, after the incident of the 1.7 flash):
//   - the image file is the expected one (SHA-256 given by the operator) before anything is sent;
//   - the image is written in 64 kB segments; each segment is erased, written and VERIFIED against the file
//     (CRC-32 computed by the MiV on the flash content, or read back word by word with an older MiV) and
//     retried up to maxSegmentAttempts times; a ROC whose segment cannot be verified is dropped;
//   - a ROC is programmed only if every segment verified AND (new MiV) a CRC of the whole image read from
//     its flash matches the file (the MiV does NOT authenticate the bitstream: the PolarFire service hung,
//     2026-10-15, so the check from the file is the protection);
//   - nothing is ever written below protectedBelow (directory and golden image); the directory is only read;
//   - every command waits for an unambiguous completion: a tagged reply with the new MiV, the reply word
//     count for reads, and DONE 1 -> 0 -> 1 plus a minimum time for writes with an older MiV (any doubt there
//     is caught by the read-back);
//   - after the IAP the ROC must come back with the expected firmware version.
//
// MiV protocol (dirac-soft/ECAL_MiV, REMOTEPROG command, words written to ROC register 384):
//   [action, p1 lo, p1 hi, p2 lo, p2 hi, payload..., (tag)]
//   3 erase p1=address p2=size            8 write p1=address p2=words, payload = words (<= 512)
//   5 IAP by SPI address p1                7 read p1=address p2=bytes (<= 1016) -> reply payload
//   10 CRC-32 p1=address p2=bytes -> reply [tag, crc lo, crc hi, error]           (solid-programming MiV)
//   tag (solid-programming MiV): actions 3 and 8 reply [tag, result, cmd_fail]
//   register 128: bit 15 DONE, 0x4000 executing; 129: reply FIFO word count (low 11 bits, = payload + 4);
//   132: PROG_RETURN (0 ok, 0xE1xx parameters, 0xEF00 flash timeout, 0x00FE system service timeout,
//        0xEE00+cmd_fail command not executed)

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace rocflash
{
//==================================================================================================
// One ROC on the DCS link. Implementations must throw std::exception on a communication error.
class RocPort
{
  public:
	virtual ~RocPort() = default;
	virtual std::string name() const                                       = 0;
	virtual uint16_t    readRegister(uint16_t address)                     = 0;
	virtual void        writeBlock(const std::vector<uint16_t>& words, uint16_t address) = 0;
	virtual void        readBlock(std::vector<uint16_t>& words, uint16_t address, uint16_t count) = 0;
	// Re-initialise the DTC side of this ROC's link (SERDES TX/RX reset of that link only). Returns false if
	// not available. Needed after an IAP: the ROC restarts, and its link does not always come back by itself
	// (2 of 6 ROCs on 2026-10-16; a manual link reset brought them back).
	virtual bool resyncLink(std::string& info) { (void)info; return false; }
};

// DCS lock of the DTC: the DTC library's own transaction lock (mu2edev::begin_dcs_transaction /
// end_dcs_transaction: library thread lock + driver M_IOC_DCS_LOCK, re-entrant for the owning thread), the
// same lock every single DCS register/block access takes. FlashSession holds it only around "check idle +
// send command" and around "read the reply", never while waiting for the MiV, so no other DCS user can slip
// a command between our idle check and our command, or read our reply, and nobody waits more than a few ms
// (other users time out after 1 s).
class DcsLock
{
  public:
	virtual ~DcsLock() = default;
	virtual void lock()   = 0;
	virtual void unlock() = 0;
};
class NoDcsLock : public DcsLock
{
  public:
	void lock() override {}
	void unlock() override {}
};

enum : uint16_t
{
	REG_DONE    = 128,
	REG_COUNT   = 129,
	REG_RETURN  = 132,
	REG_COMMAND = 384,
	REG_FW_VERSION = 246  // DRACRegisters 0xF6: [15:8] major, [7:0] minor
};
enum : uint16_t
{
	ACT_ERASE = 3,
	ACT_PROG_ADDR = 5,
	ACT_READ = 7,
	ACT_WRITE = 8,
	ACT_CRC = 10
};

//==================================================================================================
// CRC-32 as zlib crc32() and as the MiV action 10
inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0)
{
	static uint32_t tbl[256];
	static bool     init = false;
	if(!init)
	{
		for(uint32_t i = 0; i < 256; ++i)
		{
			uint32_t c = i;
			for(int k = 0; k < 8; ++k)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			tbl[i] = c;
		}
		init = true;
	}
	crc = ~crc;
	for(size_t i = 0; i < n; ++i)
		crc = tbl[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

// SHA-256 (FIPS 180-4), to check the image file against the archived checksum
inline std::string sha256hex(const std::vector<uint8_t>& data)
{
	static const uint32_t K[64] = {
	    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
	uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	std::vector<uint8_t> m(data);
	uint64_t             bits = uint64_t(data.size()) * 8;
	m.push_back(0x80);
	while(m.size() % 64 != 56)
		m.push_back(0);
	for(int i = 7; i >= 0; --i)
		m.push_back(uint8_t(bits >> (8 * i)));
	auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
	for(size_t off = 0; off < m.size(); off += 64)
	{
		uint32_t w[64];
		for(int i = 0; i < 16; ++i)
			w[i] = (uint32_t(m[off + 4 * i]) << 24) | (uint32_t(m[off + 4 * i + 1]) << 16) |
			       (uint32_t(m[off + 4 * i + 2]) << 8) | uint32_t(m[off + 4 * i + 3]);
		for(int i = 16; i < 64; ++i)
		{
			uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i]        = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for(int i = 0; i < 64; ++i)
		{
			uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			uint32_t ch = (e & f) ^ (~e & g);
			uint32_t t1 = hh + S1 + ch + K[i] + w[i];
			uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
			uint32_t t2 = S0 + mj;
			hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}
	char out[65];
	for(int i = 0; i < 8; ++i)
		std::snprintf(out + 8 * i, 9, "%08x", h[i]);
	return std::string(out, 64);
}

//==================================================================================================
struct Options
{
	uint32_t    startAddress       = 0x01010000;  // where the image goes in the SPI flash (64 kB aligned)
	uint32_t    protectedBelow     = 0x01010000;  // never erase/write below (directory, golden image)
	std::string expectedSha256;                   // mandatory: SHA-256 of the image file
	bool        writeImage         = true;        // false: do not erase/write, only verify the flash against the file
	bool        program            = false;       // IAP after a fully verified upload
	bool        finalImageCrc      = true;        // new MiV: CRC of the whole image before the IAP
	int         maxSegmentAttempts = 5;
	uint16_t    expectedFwVersion  = 0;           // register REG_FW_VERSION after the IAP; 0 = do not check
	// timing (ms)
	int pollMs              = 1;
	int writeTimeoutMs      = 3000;
	int eraseTimeoutMs      = 15000;   // one 64 kB block
	int readTimeoutMs       = 3000;
	int crcSegmentTimeoutMs = 10000;
	int crcImageTimeoutMs   = 600000;
	int legacyWriteMinMs    = 120;     // older MiV: DONE accepted after this if its drop was not seen (1 kB record)
	int legacyEraseMinMs    = 3000;    // older MiV: same for the erase of a segment (two 64 kB blocks)
	int authTimeoutMs       = 180000;  // IAP request -> link drop (normally seconds; MiV busy 0x4000 meanwhile)
	int linkBackTimeoutMs   = 300000;  // after the IAP, until the ROC answers again
	int linkResyncAfterMs   = 60000;   // ROC not answering this long after the IAP: reset its link, again after
	int linkResyncTries     = 3;       // the same time, at most this many times
};

enum class RocState
{
	Selected,
	Uploading,
	Verified,
	Programmed,  // came back with the expected version (or version check disabled)
	Failed
};

struct RocResult
{
	RocState    state          = RocState::Selected;
	bool        newMiv         = false;  // supports tags and CRC (solid-programming MiV)
	uint32_t    readBytes      = 1016;   // older MiV: read-back piece (1016 bytes since 30bc2f4, 508 before)
	int         segmentRetries = 0;
	std::string error;
	std::vector<std::string> log;
};

inline const char* stateName(RocState s)
{
	switch(s)
	{
	case RocState::Selected: return "selected";
	case RocState::Uploading: return "uploading";
	case RocState::Verified: return "verified, not programmed";
	case RocState::Programmed: return "programmed";
	case RocState::Failed: return "FAILED";
	}
	return "?";
}

//==================================================================================================
// Programs a set of ROCs of ONE DTC in lock-step (each step sent to every ROC, then all waited for).
class FlashSession
{
  public:
	using Logger   = std::function<void(const std::string&)>;
	using Progress = std::function<void(int percent)>;

	FlashSession(std::vector<RocPort*> rocs, std::vector<uint8_t> image, Options opt, Logger log = nullptr,
	             Progress progress = nullptr, DcsLock* dcsLock = nullptr)
	    : rocs_(std::move(rocs)), image_(std::move(image)), opt_(opt), log_(log), progress_(progress),
	      dcs_(dcsLock ? dcsLock : &noLock_)
	{
	}

	const std::map<RocPort*, RocResult>& results() const { return res_; }

	// Returns true when every selected ROC reached the requested end state (Verified, or Programmed).
	bool run()
	{
		res_.clear();
		for(auto r : rocs_)
			res_[r] = RocResult();
		preflight();  // throws: nothing is sent to any ROC before this passes
		probeAll();
		upload();
		if(opt_.program)
			activate();
		bool ok = true;
		for(auto& kv : res_)
			ok = ok && kv.second.state == (opt_.program ? RocState::Programmed : RocState::Verified);
		return ok;
	}

	std::string summary() const
	{
		std::ostringstream ss;
		for(auto r : rocs_)
		{
			const RocResult& x = res_.at(r);
			ss << r->name() << ": " << stateName(x.state)
			   << (x.newMiv ? " (MiV with CRC)" : " (older MiV, read-back " + std::to_string(x.readBytes) + " B)")
			   << ", segment retries " << x.segmentRetries;
			if(!x.error.empty())
				ss << ", " << x.error;
			ss << "\n";
		}
		return ss.str();
	}

  private:
	using clock = std::chrono::steady_clock;

	std::vector<RocPort*>         rocs_;
	std::vector<uint8_t>          image_;
	Options                       opt_;
	Logger                        log_;
	Progress                      progress_;
	std::map<RocPort*, RocResult> res_;
	uint16_t                      tag_ = 0;
	std::map<RocPort*, uint16_t>  verBefore_;
	NoDcsLock                     noLock_;
	DcsLock*                      dcs_;

	struct Held  // scoped DCS lock
	{
		DcsLock* l;
		explicit Held(DcsLock* x) : l(x) { l->lock(); }
		~Held() { l->unlock(); }
	};

	// Send a command when the MiV is idle: wait (unlocked), then check idle and send under the DCS lock, so
	// nothing can be sent to this ROC between the check and the command.
	bool sendWhenIdle(RocPort* r, const std::vector<uint16_t>& c, int timeoutMs)
	{
		auto t0 = clock::now();
		while(true)
		{
			int left = timeoutMs - msSince(t0);
			if(left <= 0 || !waitIdle(r, left))
				return false;
			Held h(dcs_);
			uint16_t done  = r->readRegister(REG_DONE);
			uint16_t count = r->readRegister(REG_COUNT) & 0x7ff;
			if(count == 0 && (done == 0 || (done & 0x8000)))
			{
				r->writeBlock(c, REG_COMMAND);
				return true;
			}
		}
	}

	// Read a reply of n payload words under the DCS lock (count checked again inside the lock)
	// A failed block read (DCS timeout, short packet) counts as a lost reply: the caller asks again.
	bool readReply(RocPort* r, uint16_t n, std::vector<uint16_t>& out)
	{
		try
		{
			Held h(dcs_);
			if((r->readRegister(REG_COUNT) & 0x7ff) != n + 4)
				return false;
			r->readBlock(out, REG_COMMAND, n);
			return out.size() == n;
		}
		catch(const std::exception& e)
		{
			note(r, std::string("reply read failed, will ask again: ") + e.what());
			return false;
		}
	}

	void note(const std::string& s)
	{
		if(log_)
			log_(s);
	}
	void note(RocPort* r, const std::string& s)
	{
		res_[r].log.push_back(s);
		note(r->name() + ": " + s);
	}
	void fail(RocPort* r, const std::string& why)
	{
		res_[r].state = RocState::Failed;
		res_[r].error = why;
		note(r, "FAILED: " + why);
	}
	bool alive(RocPort* r) const { return res_.at(r).state != RocState::Failed; }
	static std::string hex(uint32_t v)
	{
		char b[16];
		std::snprintf(b, sizeof b, "0x%x", v);
		return b;
	}
	uint16_t nextTag()
	{
		if(++tag_ == 0)
			tag_ = 1;
		return tag_;
	}
	void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
	static int msSince(clock::time_point t0)
	{
		return int(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count());
	}
	static std::vector<uint16_t> cmd(uint16_t action, uint32_t p1, uint32_t p2)
	{
		return {action, uint16_t(p1), uint16_t(p1 >> 16), uint16_t(p2), uint16_t(p2 >> 16)};
	}

	//----------------------------------------------------------------------------------------------
	void preflight()
	{
		if(opt_.expectedSha256.size() != 64)
			throw std::runtime_error("expected SHA-256 of the image is mandatory (64 hex digits)");
		if(image_.empty())
			throw std::runtime_error("image is empty");
		std::string sha = sha256hex(image_);
		std::string want = opt_.expectedSha256;
		std::transform(want.begin(), want.end(), want.begin(), ::tolower);
		if(sha != want)
			throw std::runtime_error("image SHA-256 " + sha + " differs from the expected " + want +
			                         ": wrong or damaged file, nothing sent");
		if(opt_.startAddress % 0x10000)
			throw std::runtime_error("start address " + hex(opt_.startAddress) + " not 64 kB aligned");
		if(opt_.startAddress < opt_.protectedBelow)
			throw std::runtime_error("start address " + hex(opt_.startAddress) +
			                         " inside the protected area (directory/golden) below " +
			                         hex(opt_.protectedBelow));
		if(image_.size() % 2)
			image_.push_back(0xFF);  // words of two bytes; 0xFF = erased flash
		note("image " + std::to_string(image_.size()) + " bytes, SHA-256 OK, to " + hex(opt_.startAddress) +
		     ", " + std::to_string(rocs_.size()) + " ROCs");
	}

	// Idle: DONE set or nothing ever run, and an empty reply FIFO (stale words are drained, logged).
	bool waitIdle(RocPort* r, int timeoutMs)
	{
		auto t0 = clock::now();
		while(true)
		{
			uint16_t done  = r->readRegister(REG_DONE);
			uint16_t count = r->readRegister(REG_COUNT) & 0x7ff;
			if(count >= 4)
			{
				std::vector<uint16_t> junk;
				{
					Held h(dcs_);
					count = r->readRegister(REG_COUNT) & 0x7ff;
					if(count >= 4)
						r->readBlock(junk, REG_COMMAND, uint16_t(count - 4));
				}
				note(r, "drained " + std::to_string(count - 4) + " stale reply words");
				continue;
			}
			if(count == 0 && (done == 0 || (done & 0x8000)))
				return true;
			if(msSince(t0) > timeoutMs)
				return false;
			sleepMs(opt_.pollMs);
		}
	}

	// New MiV: send a tagged command (tag appended); false if the MiV did not become idle.
	bool sendTagged(RocPort* r, std::vector<uint16_t> c, uint16_t tag)
	{
		c.push_back(tag);
		return sendWhenIdle(r, c, 2000);
	}

	// Wait for the reply of nPayload words to a tagged command and check the tag; empty on failure.
	std::vector<uint16_t> collectTagged(RocPort* r, uint16_t tag, uint16_t nPayload, int timeoutMs, std::string& why)
	{
		auto t0 = clock::now();
		while(true)
		{
			uint16_t count = r->readRegister(REG_COUNT) & 0x7ff;
			if(count == nPayload + 4)
				break;
			if(count > nPayload + 4)
			{
				why = "unexpected reply size " + std::to_string(count);
				return {};
			}
			if(msSince(t0) > timeoutMs)
			{
				why = "no reply after " + std::to_string(timeoutMs) + " ms (reg128=" +
				      hex(r->readRegister(REG_DONE)) + " reg132=" + hex(r->readRegister(REG_RETURN)) + ")";
				return {};
			}
			sleepMs(opt_.pollMs);
		}
		std::vector<uint16_t> reply;
		if(!readReply(r, nPayload, reply))
		{
			why = "reply lost";
			return {};
		}
		if(reply.size() != nPayload || reply[0] != tag)
		{
			why = "reply tag " + (reply.empty() ? std::string("none") : hex(reply[0])) + " expected " + hex(tag);
			return {};
		}
		return reply;
	}

	// New MiV: send a tagged command, wait for its reply of nPayload words, check the tag.
	std::vector<uint16_t> tagged(RocPort* r, std::vector<uint16_t> c, uint16_t nPayload, int timeoutMs,
	                             std::string& why)
	{
		uint16_t tag = nextTag();
		if(!sendTagged(r, c, tag))
		{
			why = "MiV not idle before the command";
			return {};
		}
		return collectTagged(r, tag, nPayload, timeoutMs, why);
	}

	// New MiV: CRC of [addr, addr+len) on several ROCs at once: the command goes to every ROC, then the
	// replies are collected, so the time does not grow with the number of ROCs. A lost or damaged reply is
	// asked again (tries). Returns "" per ROC when the CRC equals want, else the reason.
	std::map<RocPort*, std::string> parallelCrc(const std::vector<RocPort*>& rs, uint32_t addr, uint32_t len,
	                                            uint32_t want, int timeoutMs, int tries)
	{
		std::map<RocPort*, std::string> res;
		std::vector<RocPort*>           todo = rs;
		for(int k = 0; k < tries && !todo.empty(); ++k)
		{
			std::map<RocPort*, uint16_t> tags;
			std::vector<RocPort*>        again;
			for(auto r : todo)
			{
				try
				{
					uint16_t tag = nextTag();
					if(sendTagged(r, cmd(ACT_CRC, addr, len), tag))
						tags[r] = tag;
					else
					{
						res[r] = "CRC: MiV not idle";
						again.push_back(r);
					}
				}
				catch(const std::exception& e)
				{
					res[r] = std::string("CRC: ") + e.what();
					again.push_back(r);
				}
			}
			for(auto& kv : tags)
			{
				RocPort*    r = kv.first;
				std::string why;
				try
				{
					auto rp = collectTagged(r, kv.second, 4, timeoutMs, why);
					if(rp.empty())
						res[r] = "CRC: " + why;
					else if(rp[3] != 0)
						res[r] = "CRC refused, error " + hex(rp[3]);
					else if((uint32_t(rp[2]) << 16 | rp[1]) != want)
						res[r] = "CRC " + hex(uint32_t(rp[2]) << 16 | rp[1]) + " != file " + hex(want);
					else
						res[r] = "";
				}
				catch(const std::exception& e)
				{
					res[r] = std::string("CRC: ") + e.what();
				}
				if(!res[r].empty())
					again.push_back(r);
			}
			todo = again;
		}
		return res;
	}

	// Detect the MiV of every ROC at once: a tagged CRC of 2 bytes at the start address. The new MiV answers
	// within milliseconds; the older MiV answers "unknown action" (DONE + cmd_fail bit 6) and sends no reply,
	// so a ROC without reply after 1 s is an older MiV (the wait is paid once, not once per ROC).
	void probeAll()
	{
		std::map<RocPort*, uint16_t> tags;
		for(auto r : rocs_)
		{
			try
			{
				if(!waitIdle(r, 3000))
				{
					fail(r, "MiV busy or not answering (reg128=" + hex(r->readRegister(REG_DONE)) + ")");
					continue;
				}
				uint16_t tag = nextTag();
				if(sendTagged(r, cmd(ACT_CRC, opt_.startAddress, 2), tag))
					tags[r] = tag;
				else
					fail(r, "probe: MiV not idle");
			}
			catch(const std::exception& e)
			{
				fail(r, std::string("probe: ") + e.what());
			}
		}
		auto t0 = clock::now();
		std::map<RocPort*, bool> answered;
		while(msSince(t0) < 1000 && answered.size() < tags.size())
		{
			for(auto& kv : tags)
			{
				RocPort* r = kv.first;
				if(answered.count(r))
					continue;
				try
				{
					if((r->readRegister(REG_COUNT) & 0x7ff) == 4 + 4)
					{
						std::vector<uint16_t> rp;
						answered[r]    = true;
						res_[r].newMiv = readReply(r, 4, rp) && rp[0] == kv.second && rp[3] == 0;
					}
				}
				catch(const std::exception&)
				{
				}
			}
			sleepMs(opt_.pollMs);
		}
		for(auto& kv : tags)
		{
			RocPort* r = kv.first;
			try
			{
				if(!res_[r].newMiv)
					probeLegacyReadSize(r);
				if(alive(r))
					note(r, std::string("MiV ") + (res_[r].newMiv ? "with tags/CRC"
					                                               : "without CRC (older): read-back verify"));
			}
			catch(const std::exception& e)
			{
				fail(r, std::string("probe: ") + e.what());
			}
		}
	}

	// read-back size of an older MiV: 508 words since 30bc2f4, 254 words before
	void probeLegacyReadSize(RocPort* r)
	{
		waitIdle(r, 2000);  // let it finish the unknown command
		if(!sendWhenIdle(r, cmd(ACT_READ, opt_.startAddress, 1016), 2000))
			return fail(r, "probe: MiV not idle");
		auto     t0    = clock::now();
		uint16_t count = 0;
		while(msSince(t0) < opt_.readTimeoutMs)
		{
			count = r->readRegister(REG_COUNT) & 0x7ff;
			if(count == 508 + 4 || count == 254 + 4)
			{
				sleepMs(5);  // make sure the count is final
				count = r->readRegister(REG_COUNT) & 0x7ff;
				break;
			}
			sleepMs(opt_.pollMs);
		}
		if(count == 508 + 4)
			res_[r].readBytes = 1016;
		else if(count == 254 + 4)
			res_[r].readBytes = 508;
		else
			return fail(r, "probe: SPI read answered with " + std::to_string(count) + " words");
		waitIdle(r, 2000);  // drains the probe reply
	}

	//----------------------------------------------------------------------------------------------
	void upload()
	{
		const uint32_t seg   = 0x10000;
		const size_t   nSeg  = (image_.size() + seg - 1) / seg;
		auto           t0    = clock::now();
		for(auto r : rocs_)
			if(alive(r))
				res_[r].state = RocState::Uploading;

		for(size_t s = 0; s < nSeg; ++s)
		{
			const uint32_t off  = uint32_t(s * seg);
			const uint32_t len  = uint32_t(std::min<size_t>(seg, image_.size() - off));
			std::vector<RocPort*> todo;
			for(auto r : rocs_)
				if(alive(r))
					todo.push_back(r);
			if(todo.empty())
				break;
			if(!opt_.writeImage)
			{
				// verify only: the flash must already hold this file; nothing is erased or written
				std::map<RocPort*, std::string> err;
				verifySegment(todo, off, len, err);
				for(auto r : todo)
					if(alive(r) && err.count(r))
						fail(r, "flash content differs from the file at segment " + hex(opt_.startAddress + off) + ": " +
						            err[r] + " (nothing written; use write + program)");
				todo.clear();
			}
			for(int attempt = 1; attempt <= opt_.maxSegmentAttempts && !todo.empty(); ++attempt)
			{
				std::map<RocPort*, std::string> err;
				eraseSegment(todo, off, err);
				writeSegment(todo, off, len, err);
				verifySegment(todo, off, len, err);
				std::vector<RocPort*> again;
				for(auto r : todo)
				{
					if(!alive(r))
						continue;
					if(err.count(r))
					{
						++res_[r].segmentRetries;
						note(r, "segment " + hex(opt_.startAddress + off) + " attempt " + std::to_string(attempt) +
						            ": " + err[r]);
						if(attempt == opt_.maxSegmentAttempts)
							fail(r, "segment " + hex(opt_.startAddress + off) + " not verified after " +
							            std::to_string(attempt) + " attempts: " + err[r]);
						else
							again.push_back(r);
					}
				}
				todo = again;
			}
			if(progress_)
				progress_(int(90.0 * (s + 1) / nSeg));
			if(s % 16 == 15 || s + 1 == nSeg)
				note("segment " + std::to_string(s + 1) + "/" + std::to_string(nSeg) + " done, " +
				     std::to_string(msSince(t0) / 1000) + " s");
		}

		// whole image, read from each flash by its MiV, all ROCs at once
		if(opt_.finalImageCrc)
		{
			std::vector<RocPort*> crcRocs;
			for(auto r : rocs_)
				if(alive(r) && res_[r].newMiv)
					crcRocs.push_back(r);
			if(!crcRocs.empty())
			{
				auto t0  = clock::now();
				auto chk = parallelCrc(crcRocs, opt_.startAddress, uint32_t(image_.size()),
				                       crc32(image_.data(), image_.size()), opt_.crcImageTimeoutMs, 2);
				for(auto r : crcRocs)
					if(!chk[r].empty())
						fail(r, "whole-image " + chk[r]);
				note("whole-image CRC on " + std::to_string(crcRocs.size()) + " ROCs: " +
				     std::to_string(msSince(t0) / 1000) + " s");
			}
		}
		for(auto r : rocs_)
			if(alive(r))
			{
				res_[r].state = RocState::Verified;
				note(r, "image written and verified");
			}
	}

	void eraseSegment(const std::vector<RocPort*>& rs, uint32_t off, std::map<RocPort*, std::string>& err)
	{
		const uint32_t addr = opt_.startAddress + off;
		// send to all, then wait for all (erase of a 64 kB block takes 0.1-1 s)
		std::map<RocPort*, uint16_t>          tags;
		std::map<RocPort*, clock::time_point> sent;
		for(auto r : rs)
		{
			try
			{
				// new MiV: exactly the 64 kB block. Older MiV: it erases 1 + size/64k + 1 blocks, so size 1 gives
				// this block and the next one (written right after, segments go up), never more
				auto c = cmd(ACT_ERASE, addr, res_[r].newMiv ? 0x10000 : 1);
				if(res_[r].newMiv)
				{
					tags[r] = nextTag();
					c.push_back(tags[r]);
				}
				if(!sendWhenIdle(r, c, 2000))
				{
					err[r] = "MiV not idle before erase";
					continue;
				}
				sent[r] = clock::now();
			}
			catch(const std::exception& e)
			{
				err[r] = std::string("erase: ") + e.what();
			}
		}
		std::map<RocPort*, clock::time_point> legacy;
		for(auto& kv : sent)
		{
			RocPort* r = kv.first;
			if(!res_[r].newMiv)
			{
				legacy[r] = kv.second;
				continue;
			}
			try
			{
				err_if(r, waitTaggedResult(r, tags[r], opt_.eraseTimeoutMs), "erase", err);
			}
			catch(const std::exception& e)
			{
				err[r] = std::string("erase: ") + e.what();
			}
		}
		// older MiV: a missed drop is accepted after legacyEraseMinMs (two 64 kB blocks take < 2 s); an
		// incomplete erase shows up in the read-back
		waitLegacyAll(legacy, opt_.eraseTimeoutMs, opt_.legacyEraseMinMs, "erase", err);
	}

	void err_if(RocPort* r, const std::string& why, const char* what, std::map<RocPort*, std::string>& err)
	{
		if(!why.empty() && !err.count(r))
			err[r] = std::string(what) + ": " + why;
	}

	// tagged erase/write reply [tag, result, cmd_fail]; result 0 = OK
	std::string waitTaggedResult(RocPort* r, uint16_t tag, int timeoutMs)
	{
		auto t0 = clock::now();
		while(true)
		{
			uint16_t count = r->readRegister(REG_COUNT) & 0x7ff;
			if(count == 3 + 4)
				break;
			if(count > 3 + 4)
				return "unexpected reply size " + std::to_string(count);
			if(msSince(t0) > timeoutMs)
				return "no reply after " + std::to_string(timeoutMs) + " ms (reg128=" + hex(r->readRegister(REG_DONE)) +
				       " reg132=" + hex(r->readRegister(REG_RETURN)) + ")";
			sleepMs(opt_.pollMs);
		}
		std::vector<uint16_t> rep;
		if(!readReply(r, 3, rep))
			return "reply vanished";
		if(rep.size() != 3 || rep[0] != tag)
			return "reply tag mismatch";
		if(rep[1] != 0)
			return "MiV result " + hex(rep[1]) + " cmd_fail " + hex(rep[2]);
		return "";
	}

	// older MiV (no tag): the command is complete when DONE has dropped (command taken) and is set again.
	// If the drop was missed (command faster than the polling), DONE is accepted after minMs; minMs = 0
	// requires the drop. A wrong guess is caught by the read-back verification.
	std::string waitLegacyDone(RocPort* r, clock::time_point sent, int timeoutMs, int minMs)
	{
		bool sawBusy = false;
		while(true)
		{
			uint16_t done = r->readRegister(REG_DONE);
			if(!(done & 0x8000))
				sawBusy = true;
			// cmd_fail in DONE is not used: with the older MiV it may belong to another DCS user's command
			// that ran after ours; the read-back of every segment is the only verdict that counts
			else if(sawBusy || (minMs > 0 && msSince(sent) >= minMs))
				return "";
			if(msSince(sent) > timeoutMs)
				return "DONE not seen after " + std::to_string(timeoutMs) + " ms (reg128=" + hex(done) + ")";
			sleepMs(opt_.pollMs);
		}
	}

	// older MiV, several ROCs at once: all are polled in turn from the moment the commands were sent, so the
	// DONE drop of each one is seen even when they finish together (waiting for one ROC at a time misses the
	// others' drop). A ROC whose drop was missed is accepted after minMs; the read-back decides anyway.
	void waitLegacyAll(const std::map<RocPort*, clock::time_point>& sent, int timeoutMs, int minMs,
	                   const std::string& what, std::map<RocPort*, std::string>& err)
	{
		std::map<RocPort*, bool> sawBusy, done;
		size_t                   left = sent.size();
		while(left)
		{
			for(auto& kv : sent)
			{
				RocPort* r = kv.first;
				if(done[r])
					continue;
				try
				{
					uint16_t d = r->readRegister(REG_DONE);
					if(!(d & 0x8000))
						sawBusy[r] = true;
					else if(sawBusy[r] || msSince(kv.second) >= minMs)
					{
						done[r] = true;
						--left;
						continue;
					}
					if(msSince(kv.second) > timeoutMs)
					{
						if(!err.count(r))
							err[r] = what + ": DONE not seen after " + std::to_string(timeoutMs) + " ms (reg128=" + hex(d) + ")";
						done[r] = true;
						--left;
					}
				}
				catch(const std::exception& e)
				{
					if(!err.count(r))
						err[r] = what + ": " + e.what();
					done[r] = true;
					--left;
				}
			}
			if(left)
				sleepMs(opt_.pollMs);
		}
	}

	void writeSegment(const std::vector<RocPort*>& rs, uint32_t off, uint32_t len, std::map<RocPort*, std::string>& err)
	{
		for(uint32_t rec = 0; rec < len; rec += 1024)
		{
			const uint32_t n     = std::min<uint32_t>(1024, len - rec);
			const uint32_t addr  = opt_.startAddress + off + rec;
			auto           c     = cmd(ACT_WRITE, addr, n / 2);
			for(uint32_t i = 0; i < n; i += 2)
				c.push_back(uint16_t(image_[off + rec + i] | (image_[off + rec + i + 1] << 8)));
			std::map<RocPort*, uint16_t>          tags;
			std::map<RocPort*, clock::time_point> sent;
			for(auto r : rs)
			{
				if(!alive(r) || err.count(r))
					continue;
				try
				{
					auto cc = c;
					if(res_[r].newMiv)
					{
						tags[r] = nextTag();
						cc.push_back(tags[r]);
					}
					if(!sendWhenIdle(r, cc, 2000))
					{
						err[r] = "write: MiV not idle";
						continue;
					}
					sent[r] = clock::now();
				}
				catch(const std::exception& e)
				{
					err[r] = std::string("write: ") + e.what();
				}
			}
			std::map<RocPort*, clock::time_point> legacy;
			for(auto& kv : sent)
			{
				RocPort* r = kv.first;
				if(!res_[r].newMiv)
				{
					legacy[r] = kv.second;
					continue;
				}
				try
				{
					err_if(r, waitTaggedResult(r, tags[r], opt_.writeTimeoutMs), ("write " + hex(addr)).c_str(), err);
				}
				catch(const std::exception& e)
				{
					err[r] = "write " + hex(addr) + ": " + e.what();
				}
			}
			waitLegacyAll(legacy, opt_.writeTimeoutMs, opt_.legacyWriteMinMs, "write " + hex(addr), err);
		}
	}

	// Verify a segment on all ROCs at once: CRC (new MiV) or read-back piece by piece (older MiV), each
	// request sent to every ROC before the replies are collected.
	void verifySegment(const std::vector<RocPort*>& rs, uint32_t off, uint32_t len, std::map<RocPort*, std::string>& err)
	{
		const uint32_t        addr = opt_.startAddress + off;
		std::vector<RocPort*> crcRocs, rbRocs;
		for(auto r : rs)
			if(alive(r) && !err.count(r))
				(res_[r].newMiv ? crcRocs : rbRocs).push_back(r);

		if(!crcRocs.empty())
		{
			auto chk = parallelCrc(crcRocs, addr, len, crc32(image_.data() + off, len), opt_.crcSegmentTimeoutMs, 2);
			for(auto r : crcRocs)
				if(!chk[r].empty())
					err[r] = chk[r];
		}

		// older MiV: pieces of readBytes compared with the file; a piece is read up to 4 times (a word damaged
		// on the link is not a flash error; a flash error repeats)
		uint32_t piece = 1016;
		for(auto r : rbRocs)
			piece = std::min(piece, res_[r].readBytes);
		for(uint32_t p = 0; p < len && !rbRocs.empty(); p += piece)
		{
			const uint32_t n = std::min<uint32_t>(piece, len - p) + (std::min<uint32_t>(piece, len - p) % 2);
			std::vector<RocPort*>              pending = rbRocs;
			std::map<RocPort*, std::string>    last;
			for(int k = 0; k < 4 && !pending.empty(); ++k)
			{
				std::vector<RocPort*> sent, again;
				for(auto r : pending)
				{
					try
					{
						if(sendWhenIdle(r, cmd(ACT_READ, addr + p, n), 2000))
							sent.push_back(r);
						else
						{
							last[r] = "read-back: MiV not idle";
							again.push_back(r);
						}
					}
					catch(const std::exception& e)
					{
						last[r] = std::string("read-back: ") + e.what();
						again.push_back(r);
					}
				}
				for(auto r : sent)
				{
					try
					{
						last[r] = collectPieceMismatch(r, off + p, n);
					}
					catch(const std::exception& e)
					{
						last[r] = std::string("read-back: ") + e.what();
					}
					if(!last[r].empty())
						again.push_back(r);
				}
				pending = again;
			}
			for(auto r : pending)
				err[r] = last[r];
			std::vector<RocPort*> still;
			for(auto r : rbRocs)
				if(!err.count(r))
					still.push_back(r);
			rbRocs = still;
		}
	}

	// reply of one read-back piece (older MiV), already requested: "" if equal to the file
	std::string collectPieceMismatch(RocPort* r, uint32_t fileOff, uint32_t n)
	{
		const uint32_t addr = opt_.startAddress + fileOff;
		auto           t0   = clock::now();
		while(true)
		{
			uint16_t count = r->readRegister(REG_COUNT) & 0x7ff;
			if(count == n / 2 + 4)
				break;
			if(msSince(t0) > opt_.readTimeoutMs)
				return "read-back timeout at " + hex(addr) + " (count " + std::to_string(count) + ")";
			sleepMs(opt_.pollMs);
		}
		std::vector<uint16_t> w;
		if(!readReply(r, uint16_t(n / 2), w))
			return "read-back reply lost at " + hex(addr);
		for(uint32_t i = 0; i < n; i += 2)
		{
			size_t   k   = fileOff + i;
			uint16_t exp = uint16_t(image_[k] | ((k + 1 < image_.size() ? image_[k + 1] : 0xFF) << 8));
			if(i / 2 >= w.size() || w[i / 2] != exp)
				return "read-back mismatch at " + hex(addr + i);
		}
		return "";
	}

	//----------------------------------------------------------------------------------------------
	void activate()
	{
		std::vector<RocPort*> go;
		for(auto r : rocs_)
			if(res_[r].state == RocState::Verified)
				go.push_back(r);
		std::map<RocPort*, clock::time_point> sent;
		for(auto r : go)
		{
			try
			{
				verBefore_[r] = r->readRegister(REG_FW_VERSION);
				if(!sendWhenIdle(r, cmd(ACT_PROG_ADDR, opt_.startAddress, 0), 2000))
				{
					fail(r, "MiV not idle before the IAP");
					continue;
				}
				sent[r] = clock::now();
				note(r, "IAP requested");
			}
			catch(const std::exception& e)
			{
				fail(r, std::string("IAP request: ") + e.what());
			}
		}
		// each ROC: link down (programming) -> link back with the new firmware;
		// or DONE with an error from the IAP service (not programmed, old firmware keeps running).
		// All requested ROCs are polled in turn, so none can go down and come back unobserved.
		std::map<RocPort*, bool> wentDown, finished;
		std::map<RocPort*, int>  resyncs;
		while(true)
		{
			bool pending = false;
			for(auto& kv : sent)
			{
				RocPort* r = kv.first;
				if(finished[r])
					continue;
				pending = true;
				int      t = msSince(kv.second);
				uint16_t done = 0, ret = 0, ver = 0;
				bool     ok = true;
				try
				{
					done = r->readRegister(REG_DONE);
					ret  = r->readRegister(REG_RETURN);
					ver  = r->readRegister(REG_FW_VERSION);
				}
				catch(const std::exception&)
				{
					ok = false;
				}
				if(!ok || done == 0xffff)
					wentDown[r] = true;
				else if(wentDown[r] || (opt_.expectedFwVersion && ver == opt_.expectedFwVersion &&
				                        ver != verBefore_[r]))
				{
					if(opt_.expectedFwVersion && ver != opt_.expectedFwVersion)
					{
						if(t > opt_.linkBackTimeoutMs)
						{
							fail(r, "back after IAP with firmware version " + hex(ver) + ", expected " +
							            hex(opt_.expectedFwVersion));
							finished[r] = true;
						}
					}
					else
					{
						res_[r].state = RocState::Programmed;
						note(r, "back after IAP in " + std::to_string(t / 1000) + " s" +
						            (resyncs[r] ? " (after " + std::to_string(resyncs[r]) + " link reset)" : "") +
						            ", firmware version " + hex(ver));
						finished[r] = true;
					}
				}
				// the MiV clears PROG_RETURN when it takes the command: a non-zero value with DONE after
				// 200 ms is the result of this IAP request (authentication refused, or IAP service error)
				else if((done & 0x8000) && ret != 0 && t > 200)
				{
					fail(r, "IAP refused, PROG_RETURN " + hex(ret) +
					            ((ret & 0xFF00) == 0xA000
					                 ? " (image authentication failed: NOT programmed, old firmware running)"
					                 : ""));
					finished[r] = true;
				}
				if(!finished[r] && !wentDown[r] && t > opt_.authTimeoutMs)
				{
					fail(r, "IAP: link never dropped and no result after " + std::to_string(t / 1000) + " s" +
					            (ok && done == 0x4000 ? " (register 128 = 0x4000: the MiV is stuck in a system service, "
					                                    "power-cycle the ROC; the verified image stays in the flash)"
					                                  : ""));
					finished[r] = true;
				}
				// no answer for a while after the IAP: re-initialise this ROC's link on the DTC and keep polling
				if(!finished[r] && wentDown[r] && !ok && resyncs[r] < opt_.linkResyncTries &&
				   t > opt_.linkResyncAfterMs * (resyncs[r] + 1))
				{
					std::string info;
					++resyncs[r];
					bool done_ = false;
					try
					{
						done_ = r->resyncLink(info);
					}
					catch(const std::exception& e)
					{
						info = e.what();
					}
					note(r, "not answering " + std::to_string(t / 1000) + " s after the IAP: link reset " +
					            std::to_string(resyncs[r]) + (done_ ? " done" : " not available") +
					            (info.empty() ? "" : " (" + info + ")"));
				}
				if(!finished[r] && wentDown[r] && t > opt_.linkBackTimeoutMs)
				{
					fail(r, "IAP: ROC not answering " + std::to_string(t / 1000) + " s after the request, also after " +
					            std::to_string(resyncs[r]) + " link reset(s): power-cycle the ROC (the verified image is "
					            "in its flash; on 2026-10-16 such a ROC came back after a power cycle)");
					finished[r] = true;
				}
			}
			if(!pending)
				break;
			sleepMs(100);
		}
		if(progress_)
			progress_(100);
	}
};

}  // namespace rocflash
