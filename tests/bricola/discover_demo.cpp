// discover_demo.cpp
//
// A hands-on demo that proves Campiello discovery works on a single machine, no second PC or
// VM needed. It starts two advertising nodes ("Studio" and "Cucina") and one browser, all on
// the loopback interface, and prints peers as they appear and leave. Stopping a node triggers
// its goodbye, so you see it disappear at once.
//
//   make discover_demo && ./discover_demo            # uses 127.0.0.1 (same-host)
//   ./discover_demo 192.168.1.42                     # bind to a real LAN interface instead
//
// On Haiku, same-host multicast is delivered on the loopback interface (INADDR_ANY is not, for
// lack of a default multicast route), which is why this works with no network at all.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "../../src/bricola/mdns/Bricola.h"

using namespace campiello::bricola::mdns;

// Prints every discovery event as it happens (on the worker thread; stdout is fine).
class PrintObserver : public PeerObserver {
public:
	void PeerFound(const Peer& p) override { Print("FOUND  ", p); }
	void PeerUpdated(const Peer& p) override { Print("UPDATE ", p); }
	void PeerLost(const Peer& p) override { Print("LOST   ", p); }

private:
	static void Print(const char* tag, const Peer& p)
	{
		std::string addr = p.addresses.empty() ? std::string("?") : p.addresses.front();
		std::printf("  [%s] %-8s host=%-14s port=%u addr=%s fp=%s\n", tag,
			p.instance.c_str(), p.hostname.c_str(), (unsigned)p.port, addr.c_str(),
			p.fingerprintHex.empty() ? "-" : p.fingerprintHex.c_str());
		std::fflush(stdout);
	}
};

static ServiceInfo MakeSelf(const std::string& instance, const std::string& host, uint16_t port)
{
	ServiceInfo s;
	s.instance = instance;
	s.hostname = host;
	s.port = port;
	s.protocolVersion = 1;
	s.bfsAttrs = true;
	s.fingerprintHex = instance == "Studio" ? "1111aaaa" : "2222bbbb";
	return s;
}

int main(int argc, char** argv)
{
	const char* iface = (argc > 1) ? argv[1] : "127.0.0.1";
	std::printf("Campiello discovery demo on interface %s\n", iface);
	std::printf("(two nodes advertise, one browser watches; Ctrl-C to abort)\n\n");

	PrintObserver observer;
	Bricola browser;
	if (!browser.StartBrowsing(&observer, iface)) {
		std::printf("browser failed to start: %s\n", browser.Error() ? browser.Error() : "?");
		return 1;
	}

	Bricola studio, cucina;
	if (!studio.Start(MakeSelf("Studio", "studio.local", 7735), nullptr, iface)
		|| !cucina.Start(MakeSelf("Cucina", "cucina.local", 7736), nullptr, iface)) {
		std::printf("advertiser failed to start\n");
		return 1;
	}

	std::printf("-- Studio and Cucina are advertising; watching for 3s --\n");
	std::this_thread::sleep_for(std::chrono::seconds(3));

	std::printf("\n-- stopping Studio (sends a goodbye) --\n");
	studio.Stop();
	std::this_thread::sleep_for(std::chrono::seconds(2));

	std::printf("\n-- browser sees these peers right now: --\n");
	for (const Peer& p : browser.Peers())
		std::printf("  * %s (%s:%u)\n", p.instance.c_str(), p.hostname.c_str(), (unsigned)p.port);

	std::printf("\n-- stopping everything --\n");
	cucina.Stop();
	browser.Stop();
	std::printf("done.\n");
	return 0;
}
