// VpxEncoder.cpp
//
// See VpxEncoder.h. A VP8 encoder over libvpx, with a BGRA -> I420 conversion.

#include "VpxEncoder.h"

#include <cstring>

#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#include <vpx/vpx_image.h>

namespace campiello {
namespace cast {

namespace {

// BT.601 BGRA -> I420. Y is per-pixel; U/V are 4:2:0 (one sample per 2x2 block, averaged).
void BgraToI420(const uint8_t* bgra, int stride, int w, int h, vpx_image_t* img)
{
	uint8_t* yPlane = img->planes[VPX_PLANE_Y];
	uint8_t* uPlane = img->planes[VPX_PLANE_U];
	uint8_t* vPlane = img->planes[VPX_PLANE_V];
	int yStride = img->stride[VPX_PLANE_Y];
	int uStride = img->stride[VPX_PLANE_U];
	int vStride = img->stride[VPX_PLANE_V];

	for (int y = 0; y < h; ++y) {
		const uint8_t* row = bgra + y * stride;
		uint8_t* yr = yPlane + y * yStride;
		for (int x = 0; x < w; ++x) {
			const uint8_t* p = row + x * 4; // B,G,R,A
			int b = p[0], g = p[1], r = p[2];
			int yv = (77 * r + 150 * g + 29 * b) >> 8;
			yr[x] = static_cast<uint8_t>(yv < 0 ? 0 : (yv > 255 ? 255 : yv));
		}
	}
	for (int y = 0; y < h; y += 2) {
		uint8_t* ur = uPlane + (y / 2) * uStride;
		uint8_t* vr = vPlane + (y / 2) * vStride;
		for (int x = 0; x < w; x += 2) {
			int sumR = 0, sumG = 0, sumB = 0, n = 0;
			for (int dy = 0; dy < 2 && (y + dy) < h; ++dy) {
				const uint8_t* row = bgra + (y + dy) * stride;
				for (int dx = 0; dx < 2 && (x + dx) < w; ++dx) {
					const uint8_t* p = row + (x + dx) * 4;
					sumB += p[0]; sumG += p[1]; sumR += p[2]; ++n;
				}
			}
			int r = sumR / n, g = sumG / n, b = sumB / n;
			int u = ((-43 * r - 85 * g + 128 * b) >> 8) + 128;
			int v = ((128 * r - 107 * g - 21 * b) >> 8) + 128;
			ur[x / 2] = static_cast<uint8_t>(u < 0 ? 0 : (u > 255 ? 255 : u));
			vr[x / 2] = static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
		}
	}
}

} // namespace

VpxEncoder::~VpxEncoder()
{
	if (fImage != nullptr) {
		vpx_img_free(static_cast<vpx_image_t*>(fImage));
		delete static_cast<vpx_image_t*>(fImage);
		fImage = nullptr;
	}
	if (fCodec != nullptr) {
		vpx_codec_destroy(static_cast<vpx_codec_ctx_t*>(fCodec));
		delete static_cast<vpx_codec_ctx_t*>(fCodec);
		fCodec = nullptr;
	}
}

bool VpxEncoder::Init(int width, int height, int fps, int bitrateKbps)
{
	if (fInited) {
		fError = "gia inizializzato";
		return false;
	}
	if (width <= 0 || height <= 0 || (width & 1) || (height & 1)) {
		fError = "dimensioni non valide (devono essere pari)";
		return false;
	}

	vpx_codec_ctx_t* codec = new vpx_codec_ctx_t;
	std::memset(codec, 0, sizeof(*codec));
	vpx_codec_enc_cfg_t cfg;
	if (vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK) {
		fError = "config_default fallita";
		delete codec;
		return false;
	}
	cfg.g_w = width;
	cfg.g_h = height;
	cfg.g_timebase.num = 1;
	cfg.g_timebase.den = fps > 0 ? fps : 30;
	cfg.rc_target_bitrate = bitrateKbps > 0 ? bitrateKbps : 4000;
	cfg.rc_end_usage = VPX_CBR;
	cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;
	cfg.g_pass = VPX_RC_ONE_PASS;
	cfg.g_lag_in_frames = 0; // real-time: no lookahead

	if (vpx_codec_enc_init(codec, vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK) {
		fError = vpx_codec_error(codec);
		vpx_codec_destroy(codec);
		delete codec;
		return false;
	}
	// Real-time speed setting.
	vpx_codec_control(codec, VP8E_SET_CPUUSED, 8);

	vpx_image_t* img = new vpx_image_t;
	if (vpx_img_alloc(img, VPX_IMG_FMT_I420, width, height, 1) == nullptr) {
		fError = "vpx_img_alloc fallita";
		vpx_codec_destroy(codec);
		delete codec;
		delete img;
		return false;
	}

	fCodec = codec;
	fImage = img;
	fWidth = width;
	fHeight = height;
	fPts = 0;
	fInited = true;
	return true;
}

bool VpxEncoder::DrainPackets(std::vector<EncodedFrame>& out)
{
	vpx_codec_ctx_t* codec = static_cast<vpx_codec_ctx_t*>(fCodec);
	vpx_codec_iter_t iter = nullptr;
	const vpx_codec_cx_pkt_t* pkt;
	while ((pkt = vpx_codec_get_cx_data(codec, &iter)) != nullptr) {
		if (pkt->kind != VPX_CODEC_CX_FRAME_PKT)
			continue;
		EncodedFrame ef;
		ef.key = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
		ef.data.assign(static_cast<const char*>(pkt->data.frame.buf), pkt->data.frame.sz);
		out.push_back(std::move(ef));
	}
	return true;
}

bool VpxEncoder::EncodeBgra(const uint8_t* bgra, int stride, bool forceKey,
	std::vector<EncodedFrame>& out)
{
	if (!fInited) {
		fError = "non inizializzato";
		return false;
	}
	vpx_image_t* img = static_cast<vpx_image_t*>(fImage);
	BgraToI420(bgra, stride, fWidth, fHeight, img);

	vpx_codec_ctx_t* codec = static_cast<vpx_codec_ctx_t*>(fCodec);
	vpx_enc_frame_flags_t flags = forceKey ? VPX_EFLAG_FORCE_KF : 0;
	if (vpx_codec_encode(codec, img, fPts, 1, flags, VPX_DL_REALTIME) != VPX_CODEC_OK) {
		fError = vpx_codec_error(codec);
		return false;
	}
	++fPts;
	return DrainPackets(out);
}

bool VpxEncoder::Flush(std::vector<EncodedFrame>& out)
{
	if (!fInited)
		return false;
	vpx_codec_ctx_t* codec = static_cast<vpx_codec_ctx_t*>(fCodec);
	// A null image flushes the encoder; drain until no more packets.
	if (vpx_codec_encode(codec, nullptr, fPts, 1, 0, VPX_DL_REALTIME) != VPX_CODEC_OK) {
		fError = vpx_codec_error(codec);
		return false;
	}
	return DrainPackets(out);
}

} // namespace cast
} // namespace campiello
