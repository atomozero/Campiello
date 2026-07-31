// test_responder.cpp
//
// Tests the advertising path. The strongest check is a full loop: the Responder's own
// AnnouncePacket, fed to a Browser, discovers a Peer with exactly the advertised fields. Also
// checks query matching (answer our service, ignore others and responses), the unconfigured
// guard, goodbye, and SetPort/SetAddress. Pure standard C++, no framework.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/bricola/mdns/Browser.h"
#include "../../src/bricola/mdns/MdnsWire.h"
#include "../../src/bricola/mdns/Responder.h"

using namespace campiello::bricola::mdns;

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                            \
	do {                                                                       \
		++gChecks;                                                             \
		if (!(cond)) {                                                         \
			++gFailures;                                                       \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
		}                                                                      \
	} while (0)

struct Event {
	enum Kind { kFound, kUpdated, kLost } kind;
	Peer peer;
};

class Recorder : public PeerObserver {
public:
	void PeerFound(const Peer& p) override { events.push_back({Event::kFound, p}); }
	void PeerUpdated(const Peer& p) override { events.push_back({Event::kUpdated, p}); }
	void PeerLost(const Peer& p) override { events.push_back({Event::kLost, p}); }
	std::vector<Event> events;
};

static ServiceInfo MakeSelf()
{
	ServiceInfo s;
	s.instance = "Studio";
	s.hostname = "studio.local";
	s.address = "192.168.1.7";
	s.port = 7735;
	s.protocolVersion = 1;
	s.bfsAttrs = true;
	s.fingerprintHex = "abcd";
	return s;
}

static void Feed(Browser& b, const std::string& pkt, const std::string& srcIp, int64_t nowMs)
{
	b.OnPacket(reinterpret_cast<const uint8_t*>(pkt.data()), pkt.size(), srcIp, nowMs);
}

// The headline test: what we advertise is exactly what a browser discovers.
static void TestAnnounceIsDiscoverable()
{
	Responder responder(MakeSelf());
	CHECK(responder.InstanceFqdn() == "Studio._campiello._tcp.local");

	Recorder rec;
	Browser browser(&rec);
	Feed(browser, responder.AnnouncePacket(), "192.168.1.7", 1000);

	CHECK(rec.events.size() == 1);
	if (!rec.events.empty()) {
		const Peer& p = rec.events[0].peer;
		CHECK(rec.events[0].kind == Event::kFound);
		CHECK(p.key == "Studio._campiello._tcp.local");
		CHECK(p.instance == "Studio");
		CHECK(p.hostname == "studio.local");
		CHECK(p.port == 7735);
		CHECK(p.addresses.size() == 1 && p.addresses[0] == "192.168.1.7");
		CHECK(p.protocolVersion == 1);
		CHECK(p.bfsAttrs);
		CHECK(p.fingerprintHex == "abcd");
	}
}

static void TestRespondsToServiceQuery()
{
	Responder responder(MakeSelf());

	// A PTR query for our service gets an answer that a browser can consume.
	std::string query = BuildQuery("_campiello._tcp.local");
	std::string answer = responder.ResponseTo(
		reinterpret_cast<const uint8_t*>(query.data()), query.size());
	CHECK(!answer.empty());

	Recorder rec;
	Browser browser(&rec);
	Feed(browser, answer, "192.168.1.7", 1000);
	CHECK(rec.events.size() == 1 && !rec.events.empty()
		&& rec.events[0].kind == Event::kFound);
}

static void TestIgnoresNonMatching()
{
	Responder responder(MakeSelf());

	// A query for a different service: no answer.
	std::string other = BuildQuery("_http._tcp.local");
	CHECK(responder.ResponseTo(
		reinterpret_cast<const uint8_t*>(other.data()), other.size()).empty());

	// A response (not a query) must not be answered, or two responders would ping-pong.
	std::string ann = responder.AnnouncePacket();
	CHECK(responder.ResponseTo(
		reinterpret_cast<const uint8_t*>(ann.data()), ann.size()).empty());

	// Garbage in, empty out.
	uint8_t junk[] = {1, 2, 3};
	CHECK(responder.ResponseTo(junk, sizeof(junk)).empty());
}

static void TestUnconfiguredGuardAndSetters()
{
	ServiceInfo s = MakeSelf();
	s.port = 0;
	s.address.clear();
	Responder responder(s);

	// With no port there is nothing to advertise: a matching query gets no answer.
	std::string query = BuildQuery("_campiello._tcp.local");
	CHECK(responder.ResponseTo(
		reinterpret_cast<const uint8_t*>(query.data()), query.size()).empty());

	// Once the real bound port and address are set, it advertises them.
	responder.SetPort(7735);
	responder.SetAddress("10.0.0.9");
	CHECK(responder.Info().port == 7735);

	Recorder rec;
	Browser browser(&rec);
	Feed(browser, responder.AnnouncePacket(), "10.0.0.9", 1000);
	CHECK(rec.events.size() == 1);
	if (!rec.events.empty()) {
		CHECK(rec.events[0].peer.port == 7735);
		CHECK(rec.events[0].peer.addresses.size() == 1
			&& rec.events[0].peer.addresses[0] == "10.0.0.9");
	}
}

static void TestGoodbyeDropsPeer()
{
	Responder responder(MakeSelf());
	Recorder rec;
	Browser browser(&rec);

	Feed(browser, responder.AnnouncePacket(), "192.168.1.7", 1000);
	CHECK(rec.events.size() == 1);   // Found

	Feed(browser, responder.GoodbyePacket(), "192.168.1.7", 1500);
	CHECK(rec.events.size() == 2);
	if (rec.events.size() == 2)
		CHECK(rec.events[1].kind == Event::kLost);
	CHECK(browser.Peers().empty());
}

int main()
{
	TestAnnounceIsDiscoverable();
	TestRespondsToServiceQuery();
	TestIgnoresNonMatching();
	TestUnconfiguredGuardAndSetters();
	TestGoodbyeDropsPeer();

	std::printf("responder: %d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
