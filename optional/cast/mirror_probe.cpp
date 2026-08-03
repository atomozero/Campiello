// mirror_probe.cpp
//
// Dev tool for Cast Streaming milestone 1: connect to a real Cast device, launch the mirroring
// receiver, send an OFFER and print the device's ANSWER. Sends NO media. Needs a physical Cast
// VIDEO receiver (a Chromecast / Google TV); audio-only devices (Google Home) will not answer a
// mirroring offer. Not shipped in the package - build on demand:
//
//   g++ -std=c++17 mirror_probe.cpp CastMirror.cpp CastChannel.cpp -lssl -lcrypto -lnetwork -o mirror_probe
//   ./mirror_probe 192.168.1.50

#include <cstdio>
#include <string>

#include "CastChannel.h"
#include "CastMirror.h"

using namespace campiello::cast;

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "uso: mirror_probe <ip-del-chromecast>\n");
		return 2;
	}
	CastChannel ch(argv[1]);
	if (!ch.Connect()) {
		std::fprintf(stderr, "connessione CASTv2 fallita: %s\n",
			ch.Error() ? ch.Error() : "?");
		return 1;
	}
	std::printf("Connesso. Avvio negoziazione mirroring (OFFER/ANSWER, nessun media)...\n");

	OfferConfig cfg;
	MirrorResult r = NegotiateMirror(ch, cfg);
	ch.Close();

	std::printf("launched=%d offerSent=%d\n", r.launched, r.offerSent);
	if (!r.error.empty())
		std::printf("errore: %s\n", r.error.c_str());
	if (r.answer.received) {
		std::printf("ANSWER: result=%s udpPort=%d\n", r.answer.result.c_str(), r.answer.udpPort);
		std::printf("  sendIndexes=");
		for (int i : r.answer.sendIndexes) std::printf("%d ", i);
		std::printf("\n  ssrcs=");
		for (long s : r.answer.ssrcs) std::printf("%ld ", s);
		std::printf("\n");
		if (!r.answer.ok)
			std::printf("  error: %s\n", r.answer.errorText.c_str());
		std::printf("  raw: %s\n", r.answer.raw.c_str());
	}
	return r.answer.received ? 0 : 1;
}
