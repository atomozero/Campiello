// VpxEncoder.h
//
// Milestone 2 of true Cast Streaming (screen mirroring): a real-time VP8 video encoder built on
// libvpx (BSD-3-Clause, from HaikuPorts). It takes captured BGRA screen frames (Haiku B_RGB32),
// converts them to I420 (YUV 4:2:0) and encodes them to VP8 - the codec offered in the mirroring
// OFFER (milestone 1). This is the piece that was missing on the system: no faked encoding, a genuine
// libvpx pipeline whose output is verified to decode back to a frame (see test_vpx.cpp).
//
// It is NOT yet wired into the live mirror: packetizing the encoded frames into Cast RTP with AES and
// sending them over UDP is milestone 3, and the 30 fps capture->encode->send loop is milestone 4 (see
// docs/addons/cast.md). This module only encodes; it fakes nothing beyond that.
//
// Optional-only: links libvpx in the campiello_cast package; the MIT core never depends on it.

#ifndef CAMPIELLO_CAST_VPXENCODER_H
#define CAMPIELLO_CAST_VPXENCODER_H

#include <cstdint>
#include <string>
#include <vector>

namespace campiello {
namespace cast {

// One compressed VP8 frame produced by the encoder.
struct EncodedFrame {
	bool key = false;     // a keyframe (decodable on its own)
	std::string data;     // the VP8 bitstream for this frame
};

class VpxEncoder {
public:
	VpxEncoder() = default;
	~VpxEncoder();
	VpxEncoder(const VpxEncoder&) = delete;
	VpxEncoder& operator=(const VpxEncoder&) = delete;

	// Initialise a VP8 encoder for w x h at `fps`, targeting `bitrateKbps`. Returns false on failure.
	bool Init(int width, int height, int fps, int bitrateKbps);

	// Encode one BGRA frame (`bgra` is width*height pixels, `stride` bytes per row). Appends any VP8
	// packets produced to `out` (usually one). forceKey requests a keyframe. Returns false on error.
	bool EncodeBgra(const uint8_t* bgra, int stride, bool forceKey, std::vector<EncodedFrame>& out);

	// Flush the encoder (drain buffered frames). Appends remaining packets to `out`.
	bool Flush(std::vector<EncodedFrame>& out);

	int Width() const { return fWidth; }
	int Height() const { return fHeight; }
	const char* Error() const { return fError; }

private:
	bool DrainPackets(std::vector<EncodedFrame>& out);

	void* fCodec = nullptr; // vpx_codec_ctx_t*
	void* fImage = nullptr; // vpx_image_t*
	int fWidth = 0;
	int fHeight = 0;
	long fPts = 0;
	bool fInited = false;
	const char* fError = nullptr;
};

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_VPXENCODER_H
