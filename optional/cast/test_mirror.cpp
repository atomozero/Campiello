// test_mirror.cpp
//
// Unit tests for the Cast Streaming (mirroring) OFFER builder and ANSWER parser (no network). Build:
//   g++ -std=c++17 test_mirror.cpp CastMirror.cpp CastChannel.cpp -lssl -lcrypto -lnetwork -o test_mirror
//   ./test_mirror

#include <cstdio>
#include <string>

#include "CastMirror.h"

using namespace campiello::cast;

static int gFail = 0;
#define CHECK(cond) do { if (!(cond)) { \
	std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++gFail; } } while (0)
static bool Has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

int main()
{
	// OFFER structure.
	{
		OfferConfig cfg; // defaults: 1280x720@30 vp8 + opus
		std::string o = BuildOffer(7, cfg,
			"00112233445566778899aabbccddeeff", "ffeeddccbbaa99887766554433221100",
			"0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f", "f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0");
		CHECK(Has(o, "\"type\":\"OFFER\""));
		CHECK(Has(o, "\"seqNum\":7"));
		CHECK(Has(o, "\"castMode\":\"mirroring\""));
		CHECK(Has(o, "\"type\":\"video_source\""));
		CHECK(Has(o, "\"type\":\"audio_source\""));
		CHECK(Has(o, "\"codecName\":\"vp8\""));
		CHECK(Has(o, "\"codecName\":\"opus\""));
		CHECK(Has(o, "\"rtpPayloadType\":127"));
		CHECK(Has(o, "\"ssrc\":100000"));
		CHECK(Has(o, "\"ssrc\":100001"));
		CHECK(Has(o, "\"width\":1280"));
		CHECK(Has(o, "\"height\":720"));
		CHECK(Has(o, "\"aesKey\":\"00112233445566778899aabbccddeeff\""));
		CHECK(Has(o, "\"aesIvMask\":\"f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0\""));
		CHECK(Has(o, "\"maxFrameRate\":\"30000/1000\""));
	}

	// h264 variant reflects the codec.
	{
		OfferConfig cfg; cfg.videoCodec = "h264"; cfg.width = 1920; cfg.height = 1080;
		std::string o = BuildOffer(1, cfg, "a", "b", "c", "d");
		CHECK(Has(o, "\"codecName\":\"h264\""));
		CHECK(Has(o, "\"width\":1920"));
		CHECK(Has(o, "\"height\":1080"));
	}

	// Real AES hex is 32 lowercase hex chars and varies between calls.
	{
		std::string a = RandomAesHex();
		std::string b = RandomAesHex();
		CHECK(a.size() == 32);
		bool hexOnly = true;
		for (char c : a)
			if (!std::isxdigit((unsigned char)c)) hexOnly = false;
		CHECK(hexOnly);
		CHECK(a != b); // overwhelmingly likely
	}

	// ANSWER (ok) parsing.
	{
		MirrorAnswer a = ParseAnswer(
			"{\"type\":\"ANSWER\",\"seqNum\":1,\"result\":\"ok\",\"answer\":{"
			"\"udpPort\":51706,\"sendIndexes\":[0,1],\"ssrcs\":[100002,100003],"
			"\"receiverGetStatus\":true}}");
		CHECK(a.received);
		CHECK(a.ok);
		CHECK(a.result == "ok");
		CHECK(a.udpPort == 51706);
		CHECK(a.sendIndexes.size() == 2);
		CHECK(a.sendIndexes[0] == 0 && a.sendIndexes[1] == 1);
		CHECK(a.ssrcs.size() == 2);
		CHECK(a.ssrcs[0] == 100002 && a.ssrcs[1] == 100003);
	}

	// ANSWER (error) parsing.
	{
		MirrorAnswer a = ParseAnswer(
			"{\"type\":\"ANSWER\",\"seqNum\":1,\"result\":\"error\","
			"\"error\":{\"code\":42,\"description\":\"unsupported codec\"}}");
		CHECK(a.received);
		CHECK(!a.ok);
		CHECK(a.result == "error");
		CHECK(a.errorText == "unsupported codec");
	}

	// A non-ANSWER payload is not treated as received.
	{
		MirrorAnswer a = ParseAnswer("{\"type\":\"STATUS\"}");
		CHECK(!a.received);
	}

	if (gFail == 0)
		std::printf("all Cast mirror tests passed\n");
	return gFail == 0 ? 0 : 1;
}
