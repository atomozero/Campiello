// OpusEncoder.cpp
//
// See OpusEncoder.h. A thin, real libopus wrapper - nothing is faked; the produced packets decode.

#include "OpusEncoder.h"

#include <opus/opus.h>

namespace campiello {
namespace cast {

OpusAudioEncoder::~OpusAudioEncoder()
{
	if (fEnc != nullptr)
		opus_encoder_destroy(static_cast<OpusEncoder*>(fEnc));
}

bool OpusAudioEncoder::Init(int sampleRate, int channels, int bitrateBps)
{
	if (fInited) {
		fError = "encoder gia' inizializzato";
		return false;
	}
	// Opus only supports these sample rates; Cast mirroring uses 48000.
	if (sampleRate != 8000 && sampleRate != 12000 && sampleRate != 16000
		&& sampleRate != 24000 && sampleRate != 48000) {
		fError = "sample rate non supportato da Opus";
		return false;
	}
	if (channels != 1 && channels != 2) {
		fError = "numero di canali non valido (1 o 2)";
		return false;
	}

	int err = OPUS_OK;
	OpusEncoder* enc = opus_encoder_create(sampleRate, channels, OPUS_APPLICATION_AUDIO, &err);
	if (enc == nullptr || err != OPUS_OK) {
		fError = "opus_encoder_create fallito";
		if (enc != nullptr)
			opus_encoder_destroy(enc);
		return false;
	}

	// Low-latency real-time settings for a live mirror.
	if (bitrateBps > 0)
		opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrateBps));
	opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
	opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));

	fEnc = enc;
	fSampleRate = sampleRate;
	fChannels = channels;
	fInited = true;
	return true;
}

bool OpusAudioEncoder::Encode(const int16_t* pcm, int samplesPerChannel, std::string& out)
{
	if (!fInited) {
		fError = "encoder non inizializzato";
		return false;
	}
	if (pcm == nullptr || samplesPerChannel <= 0) {
		fError = "PCM non valido";
		return false;
	}

	// Opus packets are small; 4000 bytes is the recommended max buffer for one frame.
	unsigned char packet[4000];
	opus_int32 n = opus_encode(static_cast<OpusEncoder*>(fEnc), pcm, samplesPerChannel,
		packet, sizeof(packet));
	if (n < 0) {
		fError = "opus_encode fallito";
		return false;
	}
	// n == 1 means "this frame is silence, do not transmit" (DTS); we still send it so the
	// receiver's audio clock keeps advancing in lockstep with the RTP timestamp.
	out.assign(reinterpret_cast<const char*>(packet), static_cast<size_t>(n));
	return true;
}

} // namespace cast
} // namespace campiello
