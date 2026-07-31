// test_browser.cpp
//
// Tests the browse/reconcile path: feed the Browser real mDNS packets built with MdnsWire and
// check the Peer events. Covers a full announce, records split across packets, the sender-IP
// address fallback, TXT parsing, no-op refresh vs real update, goodbye (TTL 0), TTL expiry,
// and ignoring foreign service types. Pure standard C++, no framework; deterministic clock
// passed in as milliseconds. Returns non-zero on any failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "../../src/bricola/mdns/Browser.h"
#include "../../src/bricola/mdns/MdnsWire.h"

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
	void Clear() { events.clear(); }
	std::vector<Event> events;
};

static const char* const kService = "_campiello._tcp.local";

// Build a DNS-SD announce packet for one instance. Any of the record parts can be omitted by
// passing empty/zero, to simulate records split across packets.
static std::string MakeAnnounce(const std::string& instance, const std::string& host,
	uint16_t port, const std::string& ip, const std::string& node, const std::string& fp,
	uint32_t ptrTtl, uint32_t otherTtl)
{
	std::vector<OutRecord> answers;
	std::vector<OutRecord> additionals;

	OutRecord ptr;
	ptr.name = kService;
	ptr.type = kTypePTR;
	ptr.ttl = ptrTtl;
	ptr.rdata = MakePtr(instance);
	answers.push_back(ptr);

	if (port != 0) {
		OutRecord srv;
		srv.name = instance;
		srv.type = kTypeSRV;
		srv.ttl = otherTtl;
		srv.rdata = MakeSrv(0, 0, port, host);
		additionals.push_back(srv);

		OutRecord txt;
		txt.name = instance;
		txt.type = kTypeTXT;
		txt.ttl = otherTtl;
		std::vector<std::pair<std::string, std::string>> kv = {{"v", "1"}, {"bfs", "1"}};
		if (!node.empty())
			kv.push_back({"node", node});
		if (!fp.empty())
			kv.push_back({"fp", fp});
		txt.rdata = MakeTxt(kv);
		additionals.push_back(txt);
	}

	if (!ip.empty()) {
		OutRecord a;
		a.name = host;
		a.type = kTypeA;
		a.ttl = otherTtl;
		a.rdata = MakeA(ip);
		additionals.push_back(a);
	}

	return BuildResponse(answers, additionals);
}

static void Feed(Browser& b, const std::string& packet, const std::string& srcIp, int64_t nowMs)
{
	b.OnPacket(reinterpret_cast<const uint8_t*>(packet.data()), packet.size(), srcIp, nowMs);
}

static void TestFullAnnounce()
{
	Recorder rec;
	Browser browser(&rec);
	std::string pkt = MakeAnnounce("Studio._campiello._tcp.local", "studio.local", 7735,
		"192.168.1.7", "Studio Mac", "deadbeef", 4500, 120);
	Feed(browser, pkt, "192.168.1.7", 1000);

	CHECK(rec.events.size() == 1);
	if (!rec.events.empty()) {
		const Peer& p = rec.events[0].peer;
		CHECK(rec.events[0].kind == Event::kFound);
		CHECK(p.key == "Studio._campiello._tcp.local");
		CHECK(p.instance == "Studio Mac");        // TXT node overrides the label
		CHECK(p.hostname == "studio.local");
		CHECK(p.port == 7735);
		CHECK(p.addresses.size() == 1 && p.addresses[0] == "192.168.1.7");
		CHECK(p.protocolVersion == 1);
		CHECK(p.bfsAttrs);
		CHECK(p.fingerprintHex == "deadbeef");
	}
	CHECK(browser.Peers().size() == 1);
}

static void TestSplitAcrossPackets()
{
	Recorder rec;
	Browser browser(&rec);
	// PTR only: the peer is known but not yet connectable, so nothing is reported.
	std::string ptrOnly = MakeAnnounce("Studio._campiello._tcp.local", "studio.local", 0,
		"", "", "", 4500, 120);
	Feed(browser, ptrOnly, "192.168.1.7", 1000);
	CHECK(rec.events.empty());
	CHECK(browser.Peers().empty());

	// Now the SRV/TXT/A arrive: it becomes connectable and is reported once.
	std::string rest = MakeAnnounce("Studio._campiello._tcp.local", "studio.local", 7735,
		"192.168.1.7", "", "", 4500, 120);
	Feed(browser, rest, "192.168.1.7", 1100);
	CHECK(rec.events.size() == 1);
	CHECK(!rec.events.empty() && rec.events[0].kind == Event::kFound);
}

static void TestSenderIpFallback()
{
	Recorder rec;
	Browser browser(&rec);
	// SRV/TXT but no A record: the peer's address falls back to the datagram sender.
	std::string pkt = MakeAnnounce("Box._campiello._tcp.local", "box.local", 7735,
		"", "", "", 4500, 120);
	Feed(browser, pkt, "10.0.0.5", 2000);
	CHECK(rec.events.size() == 1);
	if (!rec.events.empty()) {
		const Peer& p = rec.events[0].peer;
		CHECK(p.addresses.size() == 1 && p.addresses[0] == "10.0.0.5");
	}
}

static void TestNoopRefreshVsUpdate()
{
	Recorder rec;
	Browser browser(&rec);
	std::string pkt = MakeAnnounce("Studio._campiello._tcp.local", "studio.local", 7735,
		"192.168.1.7", "", "", 4500, 120);
	Feed(browser, pkt, "192.168.1.7", 1000);
	CHECK(rec.events.size() == 1);   // Found

	// Identical re-announce: no content change, so no event.
	rec.Clear();
	Feed(browser, pkt, "192.168.1.7", 2000);
	CHECK(rec.events.empty());

	// A second address for the same host is a real change: one Updated.
	rec.Clear();
	std::vector<OutRecord> answers, additionals;
	OutRecord a;
	a.name = "studio.local";
	a.type = kTypeA;
	a.ttl = 120;
	a.rdata = MakeA("192.168.1.8");
	additionals.push_back(a);
	std::string extraA = BuildResponse(answers, additionals);
	// A-only packet does not touch the peer entry directly; re-send the PTR so the peer is
	// republished with the freshly-learned address.
	Feed(browser, extraA, "192.168.1.8", 3000);
	Feed(browser, pkt, "192.168.1.7", 3001);
	bool sawUpdate = false;
	for (const Event& e : rec.events) {
		if (e.kind == Event::kUpdated && e.peer.addresses.size() == 2)
			sawUpdate = true;
	}
	CHECK(sawUpdate);
}

static void TestGoodbye()
{
	Recorder rec;
	Browser browser(&rec);
	std::string pkt = MakeAnnounce("Studio._campiello._tcp.local", "studio.local", 7735,
		"192.168.1.7", "", "", 4500, 120);
	Feed(browser, pkt, "192.168.1.7", 1000);
	CHECK(rec.events.size() == 1);

	// A PTR with TTL 0 is a goodbye: the peer is dropped immediately.
	rec.Clear();
	std::string bye = MakeAnnounce("Studio._campiello._tcp.local", "studio.local", 0,
		"", "", "", 0, 0);
	Feed(browser, bye, "192.168.1.7", 1500);
	CHECK(rec.events.size() == 1);
	CHECK(!rec.events.empty() && rec.events[0].kind == Event::kLost);
	CHECK(browser.Peers().empty());
}

static void TestExpiry()
{
	Recorder rec;
	Browser browser(&rec);
	std::string pkt = MakeAnnounce("Studio._campiello._tcp.local", "studio.local", 7735,
		"192.168.1.7", "", "", 4500, 120);
	Feed(browser, pkt, "192.168.1.7", 1000);
	CHECK(rec.events.size() == 1);

	// Before the PTR TTL (4500 s) elapses, the peer stays.
	rec.Clear();
	browser.Tick(1000 + 4000 * 1000);
	CHECK(rec.events.empty());
	CHECK(browser.Peers().size() == 1);

	// After it elapses, the peer expires with a Lost event.
	browser.Tick(1000 + 4600 * 1000);
	CHECK(rec.events.size() == 1);
	CHECK(!rec.events.empty() && rec.events[0].kind == Event::kLost);
	CHECK(browser.Peers().empty());
}

static void TestIgnoreForeignService()
{
	Recorder rec;
	Browser browser(&rec);
	// A PTR answer for a different service type must be ignored.
	std::vector<OutRecord> answers, additionals;
	OutRecord ptr;
	ptr.name = "_http._tcp.local";
	ptr.type = kTypePTR;
	ptr.ttl = 4500;
	ptr.rdata = MakePtr("Web._http._tcp.local");
	answers.push_back(ptr);
	std::string pkt = BuildResponse(answers, additionals);
	Feed(browser, pkt, "192.168.1.9", 1000);
	CHECK(rec.events.empty());
	CHECK(browser.Peers().empty());
}

int main()
{
	TestFullAnnounce();
	TestSplitAcrossPackets();
	TestSenderIpFallback();
	TestNoopRefreshVsUpdate();
	TestGoodbye();
	TestExpiry();
	TestIgnoreForeignService();

	std::printf("browser: %d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
