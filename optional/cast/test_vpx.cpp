// test_vpx.cpp
//
// Milestone 2 proof: encode synthetic BGRA frames to VP8 with VpxEncoder, then DECODE the produced
// bitstream back with libvpx and verify it yields frames of the right size. This proves the encoder
// output is a real, decodable VP8 stream - no faking. Build:
//   g++ -std=c++17 test_vpx.cpp VpxEncoder.cpp -lvpx -o test_vpx && ./test_vpx

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>

#include "VpxEncoder.h"

using namespace campiello::cast;

static int gFail = 0;
#define CHECK(cond) do { if (!(cond)) { \
	std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++gFail; } } while (0)

// Paint a BGRA test frame: a colour gradient plus a moving white box (so inter-frames have motion).
static void PaintFrame(std::vector<uint8_t>& buf, int w, int h, int t)
{
	buf.assign((size_t)w * h * 4, 0);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			uint8_t* p = &buf[((size_t)y * w + x) * 4];
			p[0] = (uint8_t)(x * 255 / w);  // B
			p[1] = (uint8_t)(y * 255 / h);  // G
			p[2] = (uint8_t)((x + y + t * 8) & 0xff); // R
			p[3] = 255;                      // A
		}
	}
	int bx = (t * 11) % (w - 20 > 0 ? w - 20 : 1);
	for (int y = 10; y < 30 && y < h; ++y)
		for (int x = bx; x < bx + 20 && x < w; ++x) {
			uint8_t* p = &buf[((size_t)y * w + x) * 4];
			p[0] = p[1] = p[2] = 255;
		}
}

// Decode a VP8 frame and return the decoded dimensions (0x0 on failure).
static void DecodeCheck(vpx_codec_ctx_t* dec, const std::string& frame, int& outW, int& outH)
{
	outW = outH = 0;
	if (vpx_codec_decode(dec, reinterpret_cast<const uint8_t*>(frame.data()),
			(unsigned)frame.size(), nullptr, 0) != VPX_CODEC_OK)
		return;
	vpx_codec_iter_t iter = nullptr;
	vpx_image_t* img = vpx_codec_get_frame(dec, &iter);
	if (img != nullptr) {
		outW = img->d_w;
		outH = img->d_h;
	}
}

int main()
{
	const int w = 320, h = 240, fps = 30;

	VpxEncoder enc;
	CHECK(enc.Init(w, h, fps, 1000));
	CHECK(enc.Width() == w && enc.Height() == h);

	// Odd dimensions must be rejected.
	{
		VpxEncoder bad;
		CHECK(!bad.Init(w + 1, h, fps, 1000));
	}

	std::vector<EncodedFrame> stream;
	std::vector<uint8_t> frame;
	size_t totalBytes = 0;
	int keyframes = 0;

	for (int t = 0; t < 6; ++t) {
		PaintFrame(frame, w, h, t);
		std::vector<EncodedFrame> out;
		CHECK(enc.EncodeBgra(frame.data(), w * 4, t == 0, out)); // force keyframe on the first
		for (auto& ef : out) {
			CHECK(!ef.data.empty());
			totalBytes += ef.data.size();
			if (ef.key) ++keyframes;
			stream.push_back(ef);
		}
	}
	{
		std::vector<EncodedFrame> out;
		enc.Flush(out);
		for (auto& ef : out) { totalBytes += ef.data.size(); stream.push_back(ef); }
	}

	CHECK(!stream.empty());
	CHECK(stream.front().key);      // first emitted frame is the keyframe
	CHECK(keyframes >= 1);
	CHECK(totalBytes > 0);
	std::printf("encoded %zu VP8 frames, %zu bytes, %d keyframe(s)\n",
		stream.size(), totalBytes, keyframes);

	// Now DECODE the whole stream with libvpx and verify each frame decodes to w x h.
	vpx_codec_ctx_t dec;
	std::memset(&dec, 0, sizeof(dec));
	CHECK(vpx_codec_dec_init(&dec, vpx_codec_vp8_dx(), nullptr, 0) == VPX_CODEC_OK);
	int decoded = 0;
	for (const EncodedFrame& ef : stream) {
		int dw = 0, dh = 0;
		DecodeCheck(&dec, ef.data, dw, dh);
		if (dw == w && dh == h)
			++decoded;
		else
			std::printf("FAIL: frame decoded to %dx%d, expected %dx%d\n", dw, dh, w, h);
	}
	vpx_codec_destroy(&dec);
	CHECK(decoded == (int)stream.size());
	std::printf("decoded %d/%zu frames at %dx%d\n", decoded, stream.size(), w, h);

	if (gFail == 0)
		std::printf("all VP8 encode/decode tests passed\n");
	return gFail == 0 ? 0 : 1;
}
