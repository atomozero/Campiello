// CastMirror.cpp
//
// See CastMirror.h. OFFER builder, ANSWER parser, and the live milestone-1 handshake.

#include "CastMirror.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <openssl/rand.h>

namespace campiello {
namespace cast {

const char* const kMirroringAppId = "0F5096E8";

namespace {
const char* const kNsWebrtc = "urn:x-cast:com.google.cast.webrtc";

// Parse a JSON integer array value for `key` (e.g. "sendIndexes":[0,1]) into out. Best-effort.
template <typename T>
void ParseIntArray(const std::string& json, const std::string& key, std::vector<T>& out)
{
	std::string needle = "\"" + key + "\"";
	size_t k = json.find(needle);
	if (k == std::string::npos)
		return;
	size_t open = json.find('[', k);
	if (open == std::string::npos)
		return;
	size_t close = json.find(']', open);
	if (close == std::string::npos)
		return;
	std::string body = json.substr(open + 1, close - open - 1);
	size_t i = 0;
	while (i < body.size()) {
		while (i < body.size() && (std::isspace((unsigned char)body[i]) || body[i] == ','))
			++i;
		size_t start = i;
		while (i < body.size() && (std::isdigit((unsigned char)body[i]) || body[i] == '-'))
			++i;
		if (i > start)
			out.push_back(static_cast<T>(std::strtol(body.substr(start, i - start).c_str(), nullptr, 10)));
		else
			++i;
	}
}
} // namespace

std::string RandomAesHex()
{
	unsigned char buf[16];
	if (RAND_bytes(buf, sizeof(buf)) != 1) {
		// Extremely unlikely; return a fixed non-secret value so callers still form a valid message.
		std::memset(buf, 0, sizeof(buf));
	}
	static const char* hex = "0123456789abcdef";
	std::string out;
	out.reserve(32);
	for (unsigned char c : buf) {
		out += hex[c >> 4];
		out += hex[c & 0xf];
	}
	return out;
}

std::string BuildOffer(int seqNum, const OfferConfig& cfg,
	const std::string& videoAesKey, const std::string& videoAesIv,
	const std::string& audioAesKey, const std::string& audioAesIv)
{
	char buf[2048];
	std::snprintf(buf, sizeof(buf),
		"{\"type\":\"OFFER\",\"seqNum\":%d,\"offer\":{\"castMode\":\"mirroring\","
		"\"receiverGetStatus\":true,\"supportedStreams\":["
		// video stream
		"{\"index\":0,\"type\":\"video_source\",\"codecName\":\"%s\",\"rtpProfile\":\"cast\","
		"\"rtpPayloadType\":%d,\"ssrc\":%d,\"maxFrameRate\":\"%d000/1000\",\"timeBase\":\"1/90000\","
		"\"maxBitRate\":%d,\"profile\":\"main\",\"level\":\"4\","
		"\"aesKey\":\"%s\",\"aesIvMask\":\"%s\","
		"\"resolutions\":[{\"width\":%d,\"height\":%d}]},"
		// audio stream
		"{\"index\":1,\"type\":\"audio_source\",\"codecName\":\"opus\",\"rtpProfile\":\"cast\","
		"\"rtpPayloadType\":%d,\"ssrc\":%d,\"bitRate\":%d,\"timeBase\":\"1/48000\","
		"\"channels\":%d,\"aesKey\":\"%s\",\"aesIvMask\":\"%s\"}"
		"]}}",
		seqNum,
		cfg.videoCodec.c_str(), cfg.videoPayloadType, cfg.videoSsrc, cfg.frameRate,
		cfg.videoMaxBitRate, videoAesKey.c_str(), videoAesIv.c_str(), cfg.width, cfg.height,
		cfg.audioPayloadType, cfg.audioSsrc, cfg.audioBitRate, cfg.audioChannels,
		audioAesKey.c_str(), audioAesIv.c_str());
	return buf;
}

MirrorAnswer ParseAnswer(const std::string& json)
{
	MirrorAnswer a;
	a.raw = json;
	if (json.find("\"ANSWER\"") == std::string::npos)
		return a;
	a.received = true;
	a.result = CastJsonString(json, "result");
	a.ok = (a.result == "ok");
	double port = 0;
	if (CastJsonNumber(json, "udpPort", port))
		a.udpPort = static_cast<int>(port);
	ParseIntArray(json, "sendIndexes", a.sendIndexes);
	ParseIntArray(json, "ssrcs", a.ssrcs);
	if (!a.ok) {
		// error may be a string or an object with a description.
		std::string desc = CastJsonString(json, "description");
		a.errorText = desc.empty() ? CastJsonString(json, "error") : desc;
	}
	return a;
}

MirrorResult NegotiateMirror(CastChannel& channel, const OfferConfig& cfg)
{
	MirrorResult r;

	std::string transportId;
	if (!channel.LaunchAppById(kMirroringAppId, transportId)) {
		r.error = channel.Error() != nullptr ? channel.Error() : "avvio ricevitore mirroring fallito";
		return r;
	}
	r.launched = true;

	std::string offer = BuildOffer(1, cfg, RandomAesHex(), RandomAesHex(), RandomAesHex(),
		RandomAesHex());
	if (!channel.Send(kNsWebrtc, transportId, offer)) {
		r.error = "invio OFFER fallito";
		return r;
	}
	r.offerSent = true;

	std::string payload = channel.Receive(kNsWebrtc, "ANSWER", 8000);
	if (payload.empty()) {
		r.error = "nessuna risposta ANSWER dal dispositivo";
		return r;
	}
	r.answer = ParseAnswer(payload);
	return r;
}

} // namespace cast
} // namespace campiello
