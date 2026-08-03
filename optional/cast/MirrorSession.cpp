// MirrorSession.cpp
//
// See MirrorSession.h. Ties the negotiation (m1), VP8 encoder (m2) and RTP/AES/UDP transport (m3)
// into the live send loop (m4).

#include "MirrorSession.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include <sys/time.h>

#include <openssl/rand.h>

#include "CastMirror.h"

namespace campiello {
namespace cast {

namespace {
const char* const kNsWebrtc = "urn:x-cast:com.google.cast.webrtc";

std::string ToHex(const uint8_t* p, size_t n)
{
	static const char* h = "0123456789abcdef";
	std::string out;
	out.reserve(n * 2);
	for (size_t i = 0; i < n; ++i) { out += h[p[i] >> 4]; out += h[p[i] & 0xf]; }
	return out;
}
} // namespace

MirrorSession::MirrorSession(const std::string& host) : fChannel(host), fHost(host)
{
	std::memset(fVideoKey, 0, sizeof(fVideoKey));
	std::memset(fVideoIv, 0, sizeof(fVideoIv));
}

MirrorSession::~MirrorSession()
{
	Stop();
}

bool MirrorSession::Start(int width, int height, int fps)
{
	if (!fChannel.Connect()) {
		fError = fChannel.Error() != nullptr ? fChannel.Error() : "connessione CASTv2 fallita";
		return false;
	}

	// Real per-stream AES material; keep the video key/iv for encrypting frames.
	uint8_t audioKey[16], audioIv[16];
	if (RAND_bytes(fVideoKey, 16) != 1 || RAND_bytes(fVideoIv, 16) != 1
		|| RAND_bytes(audioKey, 16) != 1 || RAND_bytes(audioIv, 16) != 1) {
		fError = "generazione chiavi AES fallita";
		return false;
	}

	OfferConfig cfg;
	cfg.width = width;
	cfg.height = height;
	cfg.frameRate = fps;
	cfg.videoSsrc = static_cast<int>(fVideoSsrc);
	cfg.videoPayloadType = fVideoPayloadType;

	std::string transportId;
	if (!fChannel.LaunchAppById(kMirroringAppId, transportId)) {
		fError = fChannel.Error() != nullptr ? fChannel.Error() : "avvio receiver mirroring fallito";
		return false;
	}

	std::string offer = BuildOffer(1, cfg, ToHex(fVideoKey, 16), ToHex(fVideoIv, 16),
		ToHex(audioKey, 16), ToHex(audioIv, 16));
	if (!fChannel.Send(kNsWebrtc, transportId, offer)) {
		fError = "invio OFFER fallito";
		return false;
	}

	std::string payload = fChannel.Receive(kNsWebrtc, "ANSWER", 8000);
	if (payload.empty()) {
		fError = "nessuna risposta ANSWER";
		return false;
	}
	fAnswerRaw = payload;
	MirrorAnswer ans = ParseAnswer(payload);
	if (!ans.ok) {
		fError = "ANSWER negativa: " + (ans.errorText.empty() ? ans.result : ans.errorText);
		return false;
	}
	fUdpPort = ans.udpPort;
	for (int idx : ans.sendIndexes)
		if (idx == 0)
			fVideoAccepted = true;
	if (fUdpPort <= 0 || !fVideoAccepted) {
		fError = "il ricevitore non ha accettato lo stream video";
		return false;
	}

	if (!fEncoder.Init(width, height, fps, 6000)) {
		fError = fEncoder.Error() != nullptr ? fEncoder.Error() : "init encoder VP8 fallito";
		return false;
	}
	if (!fUdp.Open(fHost, fUdpPort)) {
		fError = "apertura socket UDP fallita";
		return false;
	}

	fFps = fps > 0 ? fps : 30;
	fRtpStep = 90000u / static_cast<uint32_t>(fFps);
	fFrameId = 0;
	fSeq = 0;
	fRtpTimestamp = 0;
	fPacketCount = 0;
	fOctetCount = 0;
	fFramesSinceReport = 0;
	fStarted = true;
	return true;
}

bool MirrorSession::SendFrame(const uint8_t* bgra, int stride)
{
	if (!fStarted)
		return false;

	bool forceKey = fAllKeyframes || (fFrameId == 0)
		|| (fFrameId % static_cast<uint32_t>(fFps) == 0);
	std::vector<EncodedFrame> frames;
	if (!fEncoder.EncodeBgra(bgra, stride, forceKey, frames)) {
		fError = fEncoder.Error() != nullptr ? fEncoder.Error() : "encode fallito";
		return false;
	}

	FrameCrypto crypto(fVideoKey, fVideoIv);
	bool okAll = true;
	for (const EncodedFrame& ef : frames) {
		std::string enc = crypto.Encrypt(fFrameId, ef.data);
		bool hasRef = !ef.key;
		uint8_t refId = static_cast<uint8_t>((fFrameId - 1) & 0xff);
		uint8_t frameId8 = static_cast<uint8_t>(fFrameId & 0xff);
		std::vector<std::string> packets = Packetize(fVideoSsrc, fVideoPayloadType, fSeq,
			frameId8, ef.key, fRtpTimestamp, hasRef, refId, enc, 1400);
		for (const std::string& pkt : packets) {
			if (!fUdp.Send(pkt))
				okAll = false;
			++fSeq;
			++fPacketCount;
			fOctetCount += static_cast<uint32_t>(pkt.size());
		}
		// Cache the packets for retransmission on NACK; keep a small recent window.
		fSentPackets[frameId8] = packets;
		fCacheOrder.push_back(frameId8);
		while (fCacheOrder.size() > 60) {
			uint8_t old = fCacheOrder.front();
			fCacheOrder.pop_front();
			// Only erase if no newer frame reused this 8-bit id.
			bool stillReferenced = false;
			for (uint8_t id : fCacheOrder)
				if (id == old) { stillReferenced = true; break; }
			if (!stillReferenced)
				fSentPackets.erase(old);
		}
		++fFrameId;
		fRtpTimestamp += fRtpStep;
	}

	// Service the receiver's RTCP (timing + retransmissions) after sending.
	ServiceRtcp();
	return okAll;
}

namespace {
int64_t NowUs()
{
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	return static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}
} // namespace

void MirrorSession::SendSenderReport()
{
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	uint32_t ntpSeconds = static_cast<uint32_t>(tv.tv_sec) + 2208988800u; // NTP epoch (1900)
	uint32_t ntpFraction = static_cast<uint32_t>((static_cast<double>(tv.tv_usec) / 1000000.0)
		* 4294967296.0);
	std::string sr = BuildSenderReport(fVideoSsrc, ntpSeconds, ntpFraction, fRtpTimestamp,
		fPacketCount, fOctetCount);
	// If the receiver has sent a Reference Time, echo it back as DLRR so it can lock the playout
	// clock (delay in units of 1/65536 s). Sent as one compound RTCP datagram with the SR.
	if (fHaveRr) {
		int64_t elapsedUs = NowUs() - fLastRrRecvUs;
		if (elapsedUs < 0) elapsedUs = 0;
		uint32_t delay = static_cast<uint32_t>((static_cast<double>(elapsedUs) / 1000000.0) * 65536.0);
		sr += BuildXrDlrr(fVideoSsrc, fReceiverSsrc, fLastRrLrr, delay);
	}
	fUdp.Send(sr);
}

void MirrorSession::Retransmit(uint8_t frameId8, uint16_t packetId, uint8_t bitmask)
{
	auto it = fSentPackets.find(frameId8);
	if (it == fSentPackets.end())
		return;
	const std::vector<std::string>& pkts = it->second;
	auto resend = [&](uint32_t pid) {
		if (pid < pkts.size())
			fUdp.Send(pkts[pid]);
	};
	if (packetId == 0xffff) { // the whole frame was lost
		for (size_t i = 0; i < pkts.size(); ++i)
			resend(static_cast<uint32_t>(i));
		return;
	}
	resend(packetId);
	for (int b = 0; b < 8; ++b)
		if (bitmask & (1 << b))
			resend(static_cast<uint32_t>(packetId) + 1 + b);
}

void MirrorSession::ServiceRtcp()
{
	// Send a Sender Report roughly every 200 ms so the receiver can build its playout clock.
	int64_t now = NowUs();
	if (now - fLastSrUs >= 200000) {
		fLastSrUs = now;
		SendSenderReport();
	}

	// Drain the receiver's RTCP and honour its CAST NACKs.
	std::string dg;
	int guard = 0;
	while (guard++ < 64 && fUdp.Receive(dg, 0)) {
		const uint8_t* p = reinterpret_cast<const uint8_t*>(dg.data());
		size_t n = dg.size();
		size_t i = 0;
		while (i + 4 <= n) {
			uint8_t pt = p[i + 1];
			size_t words = (static_cast<size_t>(p[i + 2]) << 8) | p[i + 3];
			size_t blockLen = (words + 1) * 4;
			if (blockLen == 0 || i + blockLen > n)
				break;
			// PSFB (206) carrying a "CAST" application feedback FCI.
			if (pt == 206 && blockLen >= 20 && std::memcmp(p + i + 12, "CAST", 4) == 0) {
				fLastAck = p[i + 16];
				uint8_t lossCount = p[i + 17];
				fLastLoss = lossCount;
				size_t fci = i + 20; // 12 header + "CAST"(4) + ack(1) + lossCount(1) + playout(2)
				for (uint8_t L = 0; L < lossCount && fci + 4 <= i + blockLen; ++L) {
					uint8_t fid = p[fci];
					uint16_t pid = (static_cast<uint16_t>(p[fci + 1]) << 8) | p[fci + 2];
					uint8_t bitmask = p[fci + 3];
					Retransmit(fid, pid, bitmask);
					fci += 4;
				}
			}
			// XR (207): look for a Receiver Reference Time block (BT=4) to echo back as DLRR.
			if (pt == 207 && blockLen >= 8) {
				uint32_t xrSsrc = (static_cast<uint32_t>(p[i + 4]) << 24)
					| (static_cast<uint32_t>(p[i + 5]) << 16)
					| (static_cast<uint32_t>(p[i + 6]) << 8) | p[i + 7];
				size_t j = i + 8;
				while (j + 4 <= i + blockLen) {
					uint8_t bt = p[j];
					size_t blkWords = (static_cast<size_t>(p[j + 2]) << 8) | p[j + 3];
					size_t blkBytes = (blkWords + 1) * 4;
					if (blkBytes == 0 || j + blkBytes > i + blockLen)
						break;
					if (bt == 4 && blkBytes >= 12) {
						uint32_t msw = (static_cast<uint32_t>(p[j + 4]) << 24)
							| (static_cast<uint32_t>(p[j + 5]) << 16)
							| (static_cast<uint32_t>(p[j + 6]) << 8) | p[j + 7];
						uint32_t lsw = (static_cast<uint32_t>(p[j + 8]) << 24)
							| (static_cast<uint32_t>(p[j + 9]) << 16)
							| (static_cast<uint32_t>(p[j + 10]) << 8) | p[j + 11];
						fLastRrLrr = ((msw & 0xffff) << 16) | ((lsw >> 16) & 0xffff);
						fReceiverSsrc = xrSsrc;
						fLastRrRecvUs = NowUs();
						fHaveRr = true;
					}
					j += blkBytes;
				}
			}
			i += blockLen;
		}
	}
}

void MirrorSession::Stop()
{
	if (fUdp.IsOpen())
		fUdp.Close();
	fChannel.Close();
	fStarted = false;
}

} // namespace cast
} // namespace campiello
