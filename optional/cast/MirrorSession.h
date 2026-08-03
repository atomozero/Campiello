// MirrorSession.h
//
// Milestone 4 of true Cast Streaming (screen mirroring): the live loop that combines milestones 1-3
// into an actual mirror. It negotiates the OFFER/ANSWER (keeping the video AES key), then for each
// captured BGRA frame it VP8-encodes (milestone 2), AES-128-CTR encrypts, Cast-RTP packetizes and
// sends the packets over UDP to the receiver's udpPort (milestone 3), and periodically emits an RTCP
// sender report.
//
// Capture stays OUT of this class (the caller feeds BGRA frames), so MirrorSession has no BeAPI
// dependency: it is exercised by the mirror_send dev tool, which does the BScreen capture.
//
// Honest status: the OFFER/ANSWER negotiation is validated live against a real Chromecast (it
// launches the mirroring receiver, accepts the offer and returns a udpPort). The media path
// (encode/encrypt/packetize/send) is built on unit-tested pieces, but whether the receiver actually
// renders the video depends on byte-exact wire conventions that can only be confirmed by watching the
// TV; this tool is how that end-to-end check is done, and it fakes nothing.

#ifndef CAMPIELLO_CAST_MIRRORSESSION_H
#define CAMPIELLO_CAST_MIRRORSESSION_H

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "CastChannel.h"
#include "CastTransport.h"
#include "OpusEncoder.h"
#include "VpxEncoder.h"

namespace campiello {
namespace cast {

class MirrorSession {
public:
	explicit MirrorSession(const std::string& host);
	~MirrorSession();

	// Connect, negotiate the mirroring OFFER/ANSWER (keeping the video AES key), init the VP8 encoder
	// for width x height at fps, and open the UDP sender to the receiver's udpPort. Returns false on
	// failure (see LastError()).
	bool Start(int width, int height, int fps);

	// Encode/encrypt/packetize/send one BGRA frame (width*height, `stride` bytes/row). Also emits an
	// RTCP sender report every second. Returns false on a send error.
	bool SendFrame(const uint8_t* bgra, int stride);

	// Initialise the Opus encoder for the audio stream, if the receiver accepted it in the ANSWER.
	// Safe to call after Start(); returns false (and does nothing) if audio was not accepted. Audio is
	// optional: the video mirror runs fine without it.
	bool StartAudio(int sampleRate, int channels, int bitrateBps);

	// Encode/encrypt/packetize/send one PCM frame (`samplesPerChannel` interleaved int16 samples, a
	// valid Opus frame size for the rate). No-op returning true if audio is not started. Thread-safe
	// against SendFrame, so the capture thread can call this directly. Returns false on a send error.
	bool SendAudio(const int16_t* pcm, int samplesPerChannel);

	void Stop();

	// Diagnostic: force every frame to be a keyframe (independently decodable). Useful to separate a
	// delta-frame/reference problem from a transport/timing problem.
	void SetAllKeyframes(bool on) { fAllKeyframes = on; }

	// Diagnostic: read one datagram of the receiver's RTCP feedback from the UDP socket (up to
	// timeoutMs). Returns false on timeout.
	bool PollFeedback(std::string& out, int timeoutMs) { return fUdp.Receive(out, timeoutMs); }

	int UdpPort() const { return fUdpPort; }
	bool VideoAccepted() const { return fVideoAccepted; }
	bool AudioAccepted() const { return fAudioAccepted; }
	bool AudioStarted() const { return fAudioStarted; }
	const std::string& LastError() const { return fError; }
	const std::string& AnswerRaw() const { return fAnswerRaw; }

	// Diagnostics reflecting the receiver's latest CAST feedback vs frames sent.
	uint32_t SentFrames() const { return fFrameId; }
	uint32_t SentAudioFrames() const { return fAudioFrameId; }
	uint8_t LastAck() const { return fLastAck; }
	uint8_t LastLoss() const { return fLastLoss; }

private:
	CastChannel fChannel;
	VpxEncoder fEncoder;
	OpusAudioEncoder fAudio;
	UdpSender fUdp;
	std::mutex fSendMutex; // serialises the send path (SendFrame + SendAudio + RTCP)

	std::string fHost;
	uint8_t fVideoKey[16];
	uint8_t fVideoIv[16];
	uint32_t fVideoSsrc = 100000;
	uint8_t fVideoPayloadType = 127;

	// Audio stream (Opus). Its AES material is generated in Start() and kept (unlike before, when it
	// was discarded); the RTP timestamp runs on the audio's own 48 kHz sample clock.
	uint8_t fAudioKey[16];
	uint8_t fAudioIv[16];
	uint32_t fAudioSsrc = 100001;
	uint8_t fAudioPayloadType = 96;
	uint32_t fAudioFrameId = 0;
	uint16_t fAudioSeq = 0;
	uint32_t fAudioRtpTimestamp = 0;
	uint32_t fAudioPacketCount = 0;
	uint32_t fAudioOctetCount = 0;
	bool fAudioAccepted = false;
	bool fAudioStarted = false;
	std::map<uint8_t, std::vector<std::string>> fAudioSentPackets;
	std::deque<uint8_t> fAudioCacheOrder;

	int fFps = 30;
	uint32_t fFrameId = 0;      // increments per emitted VP8 frame
	uint16_t fSeq = 0;          // RTP sequence number
	uint32_t fRtpTimestamp = 0; // 90 kHz video clock
	uint32_t fRtpStep = 3000;   // 90000 / fps
	uint32_t fPacketCount = 0;
	uint32_t fOctetCount = 0;
	int fFramesSinceReport = 0;

	// Read the receiver's RTCP feedback and retransmit any packets it NACKs; also send a Sender Report
	// on a timer. Called from SendFrame so both the GUI and the dev tools get it for free.
	void ServiceRtcp();
	void Retransmit(std::map<uint8_t, std::vector<std::string>>& cache,
		uint8_t frameId8, uint16_t packetId, uint8_t bitmask);
	// Send an RTCP Sender Report (with DLRR echo for the video SSRC) for one stream.
	void SendSenderReport(uint32_t ssrc, uint32_t rtpTimestamp, uint32_t packetCount,
		uint32_t octetCount, bool withDlrr);

	int fUdpPort = 0;
	bool fVideoAccepted = false;
	bool fAllKeyframes = false;
	bool fStarted = false;
	std::string fError;
	std::string fAnswerRaw;

	// Recently-sent RTP packets, keyed by the 8-bit frame id, for retransmission on NACK.
	std::map<uint8_t, std::vector<std::string>> fSentPackets;
	std::deque<uint8_t> fCacheOrder;
	int64_t fLastSrUs = 0; // last Sender Report time (us)

	// Receiver Reference Time echo, for the DLRR playout-clock sync.
	bool fHaveRr = false;
	uint32_t fReceiverSsrc = 0;
	uint32_t fLastRrLrr = 0;    // middle-32 of the receiver's last RRTR NTP
	int64_t fLastRrRecvUs = 0;  // when we received it (us)

	uint8_t fLastAck = 0xff;   // receiver's last CAST ack_frame_id
	uint8_t fLastLoss = 0;     // receiver's last CAST loss_count
};

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_MIRRORSESSION_H
