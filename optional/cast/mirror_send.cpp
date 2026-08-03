// mirror_send.cpp
//
// Milestone 4 dev tool: actually mirror this Haiku desktop to a real Cast device. It captures the
// screen with BScreen and drives a MirrorSession (negotiate -> VP8 encode -> AES -> Cast RTP -> UDP)
// for a few seconds. Needs a physical Cast video receiver; watch the TV to see whether the media
// renders (that is the only way to confirm the wire-level format end to end). Not shipped - build:
//
//   g++ -std=c++17 mirror_send.cpp MirrorSession.cpp CastChannel.cpp CastMirror.cpp \
//       CastTransport.cpp VpxEncoder.cpp -lbe -lvpx -lssl -lcrypto -lnetwork -o mirror_send
//   ./mirror_send 192.168.1.88 [seconds] [fps]

#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>

#include <cstdio>
#include <cstdlib>

#include "MirrorSession.h"

using namespace campiello::cast;

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "uso: mirror_send <ip-del-chromecast> [secondi] [fps]\n");
		return 2;
	}
	const char* host = argv[1];
	int seconds = argc > 2 ? std::atoi(argv[2]) : 12;
	int fps = argc > 3 ? std::atoi(argv[3]) : 15;
	if (fps <= 0) fps = 15;

	BApplication app("application/x-vnd.campiello-mirror-send");

	BScreen screen(B_MAIN_SCREEN_ID);
	if (!screen.IsValid()) {
		std::fprintf(stderr, "schermo non valido\n");
		return 1;
	}
	BBitmap* probe = nullptr;
	if (screen.GetBitmap(&probe, false) != B_OK || probe == nullptr) {
		std::fprintf(stderr, "cattura schermo fallita\n");
		return 1;
	}
	int w = ((int)probe->Bounds().Width() + 1) & ~1;
	int h = ((int)probe->Bounds().Height() + 1) & ~1;
	delete probe;
	std::printf("schermo %dx%d, %d fps, verso %s\n", w, h, fps, host);

	MirrorSession session(host);
	if (argc > 4 && std::string(argv[4]) == "key") {
		session.SetAllKeyframes(true);
		std::printf("(diagnostica: tutti keyframe)\n");
	}
	if (!session.Start(w, h, fps)) {
		std::fprintf(stderr, "negoziazione/avvio fallito: %s\n", session.LastError().c_str());
		return 1;
	}
	std::printf("negoziato: udpPort=%d, videoAccettato=%d\nANSWER: %s\n",
		session.UdpPort(), session.VideoAccepted(), session.AnswerRaw().c_str());
	std::printf("invio %d secondi di video... GUARDA LA TV.\n", seconds);

	int frames = seconds * fps;
	int sent = 0;
	for (int i = 0; i < frames; ++i) {
		BBitmap* f = nullptr;
		if (screen.GetBitmap(&f, false) == B_OK && f != nullptr) {
			if (session.SendFrame(reinterpret_cast<const uint8_t*>(f->Bits()), f->BytesPerRow()))
				++sent;
			delete f;
		}
		snooze(1000000 / fps);
	}
	std::printf("inviati %d/%d frame. Stop.\n", sent, frames);
	session.Stop();
	return 0;
}
