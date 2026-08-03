// AudioCapture.h
//
// Captures live PCM from Haiku's default audio input (BMediaRecorder) for the Cast mirror's audio
// stream. It negotiates 48 kHz stereo (the rate Cast Streaming and Opus want; the Haiku audio input
// reports exactly that on this hardware), converts whatever sample format the node delivers (float /
// int32 / int16 / uint8) to interleaved int16, and hands the caller fixed 20 ms frames (960
// samples/channel) via a callback - the exact frame OpusAudioEncoder wants.
//
// Capture source: the caller can prefer the SYSTEM AUDIO (the hardware's output loopback / monitor
// source, which HD Audio codecs expose in the input source selector under a label such as "Speaker",
// "Loopback", "Mix" or "Monitor") or the MIC/LINE input. Prefer-system-audio is the default, so the
// mirror carries the sound of what is playing on the desktop when the hardware offers a loopback. If
// no loopback source exists (some cards do not), it falls back to the default input and reports which
// source it actually selected via SourceName() - it captures a real input either way and fakes
// nothing. Selecting the source changes the audio input node's source setting (a normal, reversible
// media setting), which is why it is only done on the caller's request.
//
// BeAPI-dependent, so it lives here (compiled into the app and the dev tool), not in MirrorSession.

#ifndef CAMPIELLO_CAST_AUDIOCAPTURE_H
#define CAMPIELLO_CAST_AUDIOCAPTURE_H

#include <cstdint>
#include <string>
#include <vector>

#include <MediaNode.h>

class BMediaRecorder;

namespace campiello {
namespace cast {

class AudioCapture {
public:
	// Which hardware input source to record from.
	enum SourcePref {
		kSystemAudio, // prefer the output loopback/monitor ("Speaker"/"Loopback"/"Mix"/"Monitor")
		kMicLine,     // prefer the microphone / line-in
		kDefault      // leave the source as the hardware default
	};

	// Called on the media thread for each complete 20 ms frame: `samplesPerChannel` (=960) interleaved
	// int16 samples across `channels` (=2). Keep it short; it must be thread-safe against the caller's
	// other work (MirrorSession::SendAudio already is).
	typedef void (*FrameFunc)(void* cookie, const int16_t* pcm, int samplesPerChannel, int channels);

	AudioCapture() = default;
	~AudioCapture();
	AudioCapture(const AudioCapture&) = delete;
	AudioCapture& operator=(const AudioCapture&) = delete;

	// Connect to the audio input at 48 kHz stereo and start delivering 20 ms frames to `func`. `pref`
	// picks the recording source (default: the system-audio loopback when the hardware offers one).
	// Returns false on failure (see Error()). Needs the media_server running and a BApplication alive.
	bool Start(FrameFunc func, void* cookie, SourcePref pref = kSystemAudio);
	void Stop();

	bool IsRunning() const { return fRunning; }
	int SampleRate() const { return 48000; }
	int Channels() const { return fChannels; }
	// The input source actually selected (e.g. "Speaker", "Mic in"), for the UI/logs. Empty if the
	// hardware exposes no selectable source.
	const std::string& SourceName() const { return fSourceName; }
	const char* Error() const { return fError.c_str(); }

	// Internal: the BMediaRecorder record hook forwards here. Public only so a free C callback can
	// reach it; not part of the intended API.
	void HandleBuffer(void* data, size_t size, int format, int channels, int byteOrder);

private:
	// Select the recording source on the input node per `pref` (best-effort; sets fSourceName).
	void SelectSource(SourcePref pref);

	BMediaRecorder* fRecorder = nullptr;
	media_node fInputNode;         // the system audio input (producer) we record from
	bool fInputStarted = false;    // we explicitly started the producer node and must stop it
	FrameFunc fCallback = nullptr;
	void* fCookie = nullptr;
	int fChannels = 2;
	int fInRate = 48000;   // negotiated capture rate; audio requires 48 kHz (Cast timeBase 1/48000)
	bool fRunning = false;
	std::string fError;
	std::string fSourceName;

	// Accumulates converted int16 samples until a full 960-sample/channel frame is ready.
	std::vector<int16_t> fAccum;
};

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_AUDIOCAPTURE_H
