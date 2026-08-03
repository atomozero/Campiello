// OpusEncoder.h
//
// Audio milestone of true Cast Streaming (screen mirroring): a real-time Opus audio encoder built on
// libopus (BSD-3-Clause, from HaikuPorts). It takes captured interleaved 16-bit PCM (the format the
// Cast mirroring OFFER advertises: codecName "opus") and produces one Opus packet per 20 ms frame -
// the audio counterpart to the VP8 video path (VpxEncoder). No faked encoding: the output is verified
// to decode back to PCM of the right length (see test_opus.cpp).
//
// It is capture-agnostic (the caller feeds PCM), so this module has no BeAPI dependency; AudioCapture
// does the BMediaRecorder capture and MirrorSession does the AES/RTP/UDP send.
//
// Optional-only: links libopus in the campiello_cast package; the MIT core never depends on it.

#ifndef CAMPIELLO_CAST_OPUSENCODER_H
#define CAMPIELLO_CAST_OPUSENCODER_H

#include <cstdint>
#include <string>

namespace campiello {
namespace cast {

class OpusAudioEncoder {
public:
	OpusAudioEncoder() = default;
	~OpusAudioEncoder();
	OpusAudioEncoder(const OpusAudioEncoder&) = delete;
	OpusAudioEncoder& operator=(const OpusAudioEncoder&) = delete;

	// Initialise an Opus encoder for `sampleRate` Hz (48000 recommended for Cast) and `channels`
	// (1 or 2), targeting `bitrateBps`. Returns false on failure (see Error()).
	bool Init(int sampleRate, int channels, int bitrateBps);

	// Encode exactly `samplesPerChannel` interleaved int16 samples (a valid Opus frame size for the
	// rate: at 48 kHz one of 120/240/480/960/1920/2880 = 2.5/5/10/20/40/60 ms). Writes the compressed
	// Opus packet to `out`. Returns false on error.
	bool Encode(const int16_t* pcm, int samplesPerChannel, std::string& out);

	int SampleRate() const { return fSampleRate; }
	int Channels() const { return fChannels; }
	const char* Error() const { return fError; }

private:
	void* fEnc = nullptr; // OpusEncoder*
	int fSampleRate = 0;
	int fChannels = 0;
	bool fInited = false;
	const char* fError = nullptr;
};

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_OPUSENCODER_H
