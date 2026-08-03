// test_opus.cpp
//
// Audio-milestone proof: encode synthetic stereo PCM to Opus with OpusAudioEncoder, then DECODE the
// produced packets back with libopus and verify they yield frames of the right length. This proves the
// encoder output is a real, decodable Opus stream - no faking. Build:
//   g++ -std=c++17 test_opus.cpp OpusEncoder.cpp -lopus -o test_opus && ./test_opus

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#include <opus/opus.h>

#include "OpusEncoder.h"

using namespace campiello::cast;

static int gFail = 0;
#define CHECK(cond) do { if (!(cond)) { \
	std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++gFail; } } while (0)

// Fill one 20 ms stereo frame (960 samples/ch @ 48 kHz) with a sine tone that moves over time so
// successive frames are not identical.
static void FillTone(std::vector<int16_t>& buf, int samplesPerCh, int channels, int t)
{
	buf.assign((size_t)samplesPerCh * channels, 0);
	for (int i = 0; i < samplesPerCh; ++i) {
		double phase = (double)(t * samplesPerCh + i) / 48000.0;
		int16_t l = (int16_t)(std::sin(phase * 2.0 * M_PI * 440.0) * 12000.0);
		int16_t r = (int16_t)(std::sin(phase * 2.0 * M_PI * 660.0) * 12000.0);
		buf[(size_t)i * channels + 0] = l;
		if (channels > 1)
			buf[(size_t)i * channels + 1] = r;
	}
}

int main()
{
	const int rate = 48000, channels = 2, frame = 960; // 20 ms

	OpusAudioEncoder enc;
	CHECK(enc.Init(rate, channels, 128000));
	CHECK(enc.SampleRate() == rate && enc.Channels() == channels);

	// Bad parameters must be rejected.
	{
		OpusAudioEncoder bad;
		CHECK(!bad.Init(44100, 2, 128000)); // 44.1 kHz is not an Opus rate
		OpusAudioEncoder bad2;
		CHECK(!bad2.Init(48000, 3, 128000)); // 3 channels invalid
	}

	int decErr = OPUS_OK;
	OpusDecoder* dec = opus_decoder_create(rate, channels, &decErr);
	CHECK(dec != nullptr && decErr == OPUS_OK);

	std::vector<int16_t> pcm;
	std::vector<int16_t> outPcm((size_t)frame * channels);
	size_t totalBytes = 0;
	int decoded = 0;
	for (int t = 0; t < 25; ++t) { // half a second of audio
		FillTone(pcm, frame, channels, t);
		std::string packet;
		CHECK(enc.Encode(pcm.data(), frame, packet));
		CHECK(!packet.empty());
		totalBytes += packet.size();

		int n = opus_decode(dec, reinterpret_cast<const unsigned char*>(packet.data()),
			(opus_int32)packet.size(), outPcm.data(), frame, 0);
		if (n == frame)
			++decoded;
		else
			std::printf("FAIL: frame decoded to %d samples/ch, expected %d\n", n, frame);
	}
	opus_decoder_destroy(dec);

	CHECK(decoded == 25);
	CHECK(totalBytes > 0);
	std::printf("encoded/decoded %d Opus frames, %zu bytes total\n", decoded, totalBytes);

	if (gFail == 0)
		std::printf("all Opus encode/decode tests passed\n");
	return gFail == 0 ? 0 : 1;
}
