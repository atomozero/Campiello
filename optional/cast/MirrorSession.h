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
#include <string>

#include "CastChannel.h"
#include "CastTransport.h"
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

	void Stop();

	int UdpPort() const { return fUdpPort; }
	bool VideoAccepted() const { return fVideoAccepted; }
	const std::string& LastError() const { return fError; }
	const std::string& AnswerRaw() const { return fAnswerRaw; }

private:
	CastChannel fChannel;
	VpxEncoder fEncoder;
	UdpSender fUdp;

	std::string fHost;
	uint8_t fVideoKey[16];
	uint8_t fVideoIv[16];
	uint32_t fVideoSsrc = 100000;
	uint8_t fVideoPayloadType = 127;

	int fFps = 30;
	uint32_t fFrameId = 0;      // increments per emitted VP8 frame
	uint16_t fSeq = 0;          // RTP sequence number
	uint32_t fRtpTimestamp = 0; // 90 kHz video clock
	uint32_t fRtpStep = 3000;   // 90000 / fps
	uint32_t fPacketCount = 0;
	uint32_t fOctetCount = 0;
	int fFramesSinceReport = 0;

	int fUdpPort = 0;
	bool fVideoAccepted = false;
	bool fStarted = false;
	std::string fError;
	std::string fAnswerRaw;
};

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_MIRRORSESSION_H
