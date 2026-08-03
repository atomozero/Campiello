// MirrorSession.cpp
//
// See MirrorSession.h. Ties the negotiation (m1), VP8 encoder (m2) and RTP/AES/UDP transport (m3)
// into the live send loop (m4).

#include "MirrorSession.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

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

	bool forceKey = (fFrameId == 0) || (fFrameId % static_cast<uint32_t>(fFps) == 0);
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
		std::vector<std::string> packets = Packetize(fVideoSsrc, fVideoPayloadType, fSeq,
			static_cast<uint8_t>(fFrameId & 0xff), ef.key, fRtpTimestamp, hasRef, refId, enc, 1400);
		for (const std::string& pkt : packets) {
			if (!fUdp.Send(pkt))
				okAll = false;
			++fSeq;
			++fPacketCount;
			fOctetCount += static_cast<uint32_t>(pkt.size());
		}
		++fFrameId;
		fRtpTimestamp += fRtpStep;
		++fFramesSinceReport;
	}

	// One RTCP sender report per second (lip-sync/timing).
	if (fFramesSinceReport >= fFps) {
		fFramesSinceReport = 0;
		uint32_t ntpSeconds = static_cast<uint32_t>(std::time(nullptr)) + 2208988800u; // NTP epoch
		std::string sr = BuildSenderReport(fVideoSsrc, ntpSeconds, 0, fRtpTimestamp, fPacketCount,
			fOctetCount);
		fUdp.Send(sr);
	}
	return okAll;
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
