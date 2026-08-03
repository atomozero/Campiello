// AudioCapture.cpp
//
// See AudioCapture.h. Records from the Haiku audio input via BMediaRecorder, converts to interleaved
// int16 stereo, and delivers fixed 20 ms frames. Prefers the system-audio (output loopback) source.

#include "AudioCapture.h"

#include <cctype>
#include <cstring>
#include <strings.h>

#include <MediaDefs.h>
#include <MediaNode.h>
#include <MediaRecorder.h>
#include <MediaRoster.h>
#include <ParameterWeb.h>
#include <TimeSource.h>

namespace campiello {
namespace cast {

namespace {

const int kFrameSamples = 960; // 20 ms at 48 kHz

// Case-insensitive substring test.
bool ContainsCI(const char* hay, const char* needle)
{
	if (hay == nullptr || needle == nullptr)
		return false;
	size_t nl = std::strlen(needle);
	for (const char* p = hay; *p != '\0'; ++p)
		if (strncasecmp(p, needle, nl) == 0)
			return true;
	return false;
}

// Names HD Audio / other codecs use for the output loopback ("what you hear") record source.
bool LooksLikeSystemAudio(const char* name)
{
	static const char* kWords[] = {"speaker", "loopback", "monitor", "stereo mix", "wave out",
		"what u hear", "what you hear", "mix", "output"};
	for (const char* w : kWords)
		if (ContainsCI(name, w))
			return true;
	return false;
}

bool LooksLikeMicLine(const char* name)
{
	static const char* kWords[] = {"mic", "line"};
	for (const char* w : kWords)
		if (ContainsCI(name, w))
			return true;
	return false;
}

} // namespace

AudioCapture::~AudioCapture()
{
	Stop();
}

void AudioCapture::SelectSource(SourcePref pref)
{
	fSourceName.clear();
	if (pref == kDefault)
		return;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == nullptr)
		return;
	media_node in;
	if (roster->GetAudioInput(&in) != B_OK)
		return;

	BParameterWeb* web = nullptr;
	if (roster->GetParameterWebFor(in, &web) != B_OK || web == nullptr)
		return;

	// The recording source is a discrete "Input" multiplexer parameter. Some codecs expose more than
	// one (one per ADC); set each to the best-matching source so whichever is used is right.
	for (int32 i = 0; i < web->CountParameters(); ++i) {
		BParameter* p = web->ParameterAt(i);
		if (p == nullptr || p->Type() != BParameter::B_DISCRETE_PARAMETER)
			continue;
		const char* kind = p->Kind();
		if (kind == nullptr || strcasecmp(kind, "Input") != 0)
			continue;
		BDiscreteParameter* mux = static_cast<BDiscreteParameter*>(p);

		int32 best = -1;
		for (int32 j = 0; j < mux->CountItems(); ++j) {
			const char* name = mux->ItemNameAt(j);
			bool match = (pref == kSystemAudio) ? LooksLikeSystemAudio(name)
											    : LooksLikeMicLine(name);
			if (match) { best = j; break; }
		}
		if (best < 0)
			continue;

		int32 value = mux->ItemValueAt(best);
		if (mux->SetValue(&value, sizeof(value), 0) == B_OK)
			fSourceName = mux->ItemNameAt(best);
	}
	delete web;
}

// BMediaRecorder record hook: forwards each captured buffer to the AudioCapture instance.
static void RecordHook(void* cookie, bigtime_t /*timestamp*/, void* data, size_t size,
	const media_format& format)
{
	AudioCapture* self = static_cast<AudioCapture*>(cookie);
	if (self == nullptr || data == nullptr || size == 0)
		return;
	const media_raw_audio_format& raw = format.u.raw_audio;
	self->HandleBuffer(data, size, static_cast<int>(raw.format),
		static_cast<int>(raw.channel_count), static_cast<int>(raw.byte_order));
}

bool AudioCapture::Start(FrameFunc func, void* cookie, SourcePref pref)
{
	if (fRunning) {
		fError = "cattura audio gia' avviata";
		return false;
	}
	if (func == nullptr) {
		fError = "callback nullo";
		return false;
	}
	fCallback = func;
	fCookie = cookie;
	fAccum.clear();

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == nullptr) {
		fError = "media_server non attivo";
		return false;
	}
	if (roster->GetAudioInput(&fInputNode) != B_OK) {
		fError = "nessun ingresso audio di sistema";
		return false;
	}
	media_node inputNode = fInputNode;

	// Point the hardware at the requested source (system-audio loopback by default) before connecting.
	SelectSource(pref);

	fRecorder = new BMediaRecorder("campiello-audio", B_MEDIA_RAW_AUDIO);
	if (fRecorder->InitCheck() != B_OK) {
		fError = "creazione BMediaRecorder fallita";
		Stop();
		return false;
	}
	fRecorder->SetHooks(RecordHook, nullptr, this);

	// Ask for 48 kHz stereo; leave the sample format wildcard so we accept whatever the codec gives
	// (this hardware delivers 32-bit int) and convert it ourselves.
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	format.u.raw_audio.frame_rate = 48000;
	format.u.raw_audio.channel_count = 2;

	if (fRecorder->Connect(inputNode, nullptr, &format) != B_OK) {
		fError = "connessione all'ingresso audio fallita";
		Stop();
		return false;
	}

	const media_raw_audio_format& got = fRecorder->Format().u.raw_audio;
	fInRate = static_cast<int>(got.frame_rate);
	fChannels = got.channel_count >= 2 ? 2 : 1;
	if (fInRate != 48000) {
		// Cast's audio timeBase is 1/48000; a different capture rate would break timing. Audio is
		// optional, so bail out cleanly and let the caller keep the video mirror.
		fError = "l'ingresso audio non e' a 48 kHz; audio disabilitato";
		Stop();
		return false;
	}

	if (fRecorder->Start() != B_OK) {
		fError = "avvio cattura audio fallito";
		Stop();
		return false;
	}

	// The recorder node is running, but the system audio input (the producer) must be started too or
	// no buffers ever flow - this was the missing step. Start it on its own time source.
	BTimeSource* ts = roster->MakeTimeSourceFor(fInputNode);
	bigtime_t perf = (ts != nullptr) ? ts->Now() : 0;
	if (roster->StartNode(fInputNode, perf) == B_OK)
		fInputStarted = true;
	if (ts != nullptr)
		ts->Release();

	fRunning = true;
	return true;
}

void AudioCapture::HandleBuffer(void* data, size_t size, int format, int channels, int /*byteOrder*/)
{
	if (fCallback == nullptr || channels <= 0)
		return;

	// Bytes per sample for the delivered format (host endian assumed on Haiku/x86).
	int bps = 0;
	switch (format) {
		case media_raw_audio_format::B_AUDIO_FLOAT: bps = 4; break;
		case media_raw_audio_format::B_AUDIO_INT:   bps = 4; break;
		case media_raw_audio_format::B_AUDIO_SHORT: bps = 2; break;
		case media_raw_audio_format::B_AUDIO_UCHAR: bps = 1; break;
		default: return; // unknown format
	}
	size_t totalSamples = size / static_cast<size_t>(bps);
	size_t frames = totalSamples / static_cast<size_t>(channels);

	for (size_t f = 0; f < frames; ++f) {
		// Read up to two channels, convert to int16, and pack as stereo (duplicate if mono).
		int16_t out[2] = {0, 0};
		int use = channels < 2 ? 1 : 2;
		for (int c = 0; c < use; ++c) {
			size_t idx = f * static_cast<size_t>(channels) + static_cast<size_t>(c);
			int32_t v = 0;
			switch (format) {
				case media_raw_audio_format::B_AUDIO_FLOAT: {
					float s = static_cast<const float*>(data)[idx];
					if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
					v = static_cast<int32_t>(s * 32767.0f);
					break;
				}
				case media_raw_audio_format::B_AUDIO_INT:
					v = static_cast<const int32_t*>(data)[idx] >> 16; // 32-bit -> 16-bit
					break;
				case media_raw_audio_format::B_AUDIO_SHORT:
					v = static_cast<const int16_t*>(data)[idx];
					break;
				case media_raw_audio_format::B_AUDIO_UCHAR:
					v = (static_cast<int>(static_cast<const uint8_t*>(data)[idx]) - 128) << 8;
					break;
			}
			if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
			out[c] = static_cast<int16_t>(v);
		}
		if (use == 1)
			out[1] = out[0];
		fAccum.push_back(out[0]);
		fAccum.push_back(out[1]);

		if (fAccum.size() >= static_cast<size_t>(kFrameSamples) * 2) {
			fCallback(fCookie, fAccum.data(), kFrameSamples, 2);
			fAccum.erase(fAccum.begin(), fAccum.begin() + kFrameSamples * 2);
		}
	}
}

void AudioCapture::Stop()
{
	if (fRecorder != nullptr) {
		if (fRecorder->IsRunning())
			fRecorder->Stop();
		fRecorder->Disconnect();
		delete fRecorder;
		fRecorder = nullptr;
	}
	// Stop the producer only if we started it, to leave the system input as we found it.
	if (fInputStarted) {
		BMediaRoster* roster = BMediaRoster::Roster();
		if (roster != nullptr)
			roster->StopNode(fInputNode, 0, true);
		fInputStarted = false;
	}
	fRunning = false;
	fAccum.clear();
}

} // namespace cast
} // namespace campiello
