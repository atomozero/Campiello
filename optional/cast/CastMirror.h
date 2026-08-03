// CastMirror.h
//
// Milestone 1 of true Cast Streaming (screen mirroring): the OFFER/ANSWER negotiation over the
// `urn:x-cast:com.google.cast.webrtc` namespace, with NO media. This is the handshake a real mirror
// begins with, reimplemented from Google's openscreen `offer_messages`/`answer_messages`:
//
//   sender -> LAUNCH the mirroring receiver app (0F5096E8), open a virtual connection to it
//   sender -> {"type":"OFFER","seqNum":N,"offer":{"castMode":"mirroring","supportedStreams":[...]}}
//   device -> {"type":"ANSWER","seqNum":N,"result":"ok","answer":{"udpPort":P,"sendIndexes":[...],
//              "ssrcs":[...]}}   (or result:"error")
//
// The ANSWER tells us the UDP port to send Cast RTP to and which offered streams were accepted. This
// module builds the OFFER and parses the ANSWER (both pure and unit-tested) and runs the live
// handshake over a connected CastChannel. It does NOT capture, encode, packetize or send any media -
// those are milestones 2-4 (see docs/addons/cast.md), and this file fakes none of them.
//
// AES keys are real (OpenSSL RAND_bytes); links libcrypto in the optional package only.

#ifndef CAMPIELLO_CAST_CASTMIRROR_H
#define CAMPIELLO_CAST_CASTMIRROR_H

#include <string>
#include <vector>

#include "CastChannel.h"

namespace campiello {
namespace cast {

// The Cast Streaming mirroring receiver appId (the one Chrome/openscreen launch to mirror).
extern const char* const kMirroringAppId;

// Parameters of the streams we offer. Defaults are a common 720p30 VP8 + stereo Opus pair.
struct OfferConfig {
	std::string videoCodec = "vp8"; // "vp8" or "h264"
	int width = 1280;
	int height = 720;
	int frameRate = 30;
	int videoSsrc = 100000;
	int audioSsrc = 100001;
	int videoPayloadType = 127;
	int audioPayloadType = 96;
	int videoMaxBitRate = 5000000;
	int audioBitRate = 128000;
	int audioChannels = 2;
};

// 16 random bytes as a 32-char lowercase hex string (OpenSSL RAND_bytes).
std::string RandomAesHex();

// Build the OFFER JSON. Keys/IVs are passed in (32 hex chars each) so the builder is deterministic
// and unit-testable; NegotiateMirror generates them with RandomAesHex.
std::string BuildOffer(int seqNum, const OfferConfig& cfg,
	const std::string& videoAesKey, const std::string& videoAesIv,
	const std::string& audioAesKey, const std::string& audioAesIv);

// The device's ANSWER, decoded.
struct MirrorAnswer {
	bool received = false;          // an ANSWER message arrived
	bool ok = false;                // result == "ok"
	std::string result;             // "ok" / "error"
	int udpPort = 0;                // where Cast RTP would be sent
	std::vector<int> sendIndexes;   // accepted stream indexes
	std::vector<long> ssrcs;        // receiver-side SSRCs
	std::string errorText;          // on result == "error"
	std::string raw;                // the full payload, for logging
};

MirrorAnswer ParseAnswer(const std::string& json);

// The outcome of one negotiation attempt.
struct MirrorResult {
	bool launched = false;   // the mirroring receiver app launched
	bool offerSent = false;  // the OFFER was sent
	MirrorAnswer answer;     // the device's reply (answer.received == got a reply)
	std::string error;       // human-readable failure reason, if any
};

// Run milestone 1 over an already-connected CastChannel: launch the mirroring receiver, send the
// OFFER, read and parse the ANSWER. Sends no media. Needs a real Cast video receiver to answer.
MirrorResult NegotiateMirror(CastChannel& channel, const OfferConfig& cfg);

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_CASTMIRROR_H
