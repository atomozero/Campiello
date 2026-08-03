// mirror_rtcp_probe.cpp
//
// Dev diagnostic: negotiate a mirror, send keyframes, and DUMP whatever RTCP the Cast receiver sends
// back over UDP. The receiver's feedback tells us what it expects (timing / frame ACK / NACK), which
// is what we need to make playback advance past the first frame. Sends real frames but is only a
// probe. Build:
//   g++ -std=c++17 mirror_rtcp_probe.cpp MirrorSession.cpp CastChannel.cpp CastMirror.cpp \
//       CastTransport.cpp VpxEncoder.cpp -lbe -lvpx -lssl -lcrypto -lnetwork -o mirror_rtcp_probe
//   ./mirror_rtcp_probe 192.168.1.88

#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>

#include <cstdio>
#include <string>

#include "MirrorSession.h"

using namespace campiello::cast;

// Walk the compound RTCP and print the CAST feedback ack_frame_id / loss_count if present.
static void ParseCast(const std::string& dg)
{
	const unsigned char* p = reinterpret_cast<const unsigned char*>(dg.data());
	size_t n = dg.size(), i = 0;
	while (i + 4 <= n) {
		unsigned pt = p[i + 1];
		size_t words = (size_t(p[i + 2]) << 8) | p[i + 3];
		size_t blockLen = (words + 1) * 4;
		if (blockLen == 0 || i + blockLen > n) break;
		if (pt == 206 && blockLen >= 20 && memcmp(p + i + 12, "CAST", 4) == 0) {
			unsigned ack = p[i + 16], loss = p[i + 17];
			std::printf("  CAST feedback: ack_frame_id=%u loss_count=%u\n", ack, loss);
		}
		i += blockLen;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) { std::fprintf(stderr, "uso: mirror_rtcp_probe <ip>\n"); return 2; }
	BApplication app("application/x-vnd.campiello-mirror-rtcp");

	BScreen screen(B_MAIN_SCREEN_ID);
	BBitmap* probe = nullptr;
	if (screen.GetBitmap(&probe, false) != B_OK || probe == nullptr) return 1;
	int w = ((int)probe->Bounds().Width() + 1) & ~1;
	int h = ((int)probe->Bounds().Height() + 1) & ~1;
	delete probe;

	MirrorSession session(argv[1]);
	session.SetAllKeyframes(true);
	if (!session.Start(w, h, 10)) {
		std::fprintf(stderr, "start fallito: %s\n", session.LastError().c_str());
		return 1;
	}
	std::printf("negoziato udpPort=%d. Invio frame e ascolto il feedback RTCP per ~6s...\n",
		session.UdpPort());

	for (int i = 0; i < 40; ++i) {
		BBitmap* f = nullptr;
		if (screen.GetBitmap(&f, false) == B_OK && f != nullptr) {
			session.SendFrame(reinterpret_cast<const uint8_t*>(f->Bits()), f->BytesPerRow());
			delete f;
		}
		if (i % 4 == 0) {
			std::printf("frame inviati=%u | receiver ack_frame_id=%u loss_count=%u\n",
				session.SentFrames(), session.LastAck(), session.LastLoss());
			std::fflush(stdout);
		}
		snooze(66000);
	}
	std::printf("FINE: inviati=%u, ultimo ack=%u\n", session.SentFrames(), session.LastAck());
	std::printf("Se ack avanza con i frame -> ricezione OK, problema decodifica/decrittazione.\n");
	std::printf("Se ack resta basso/fermo -> il receiver NON accetta i frame successivi.\n");
	session.Stop();
	return 0;
}
