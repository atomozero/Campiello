// test_radar.cpp
//
// Unit test for the MdnsRadar aggregator. It feeds hand-built mDNS packets (via MdnsWire) into
// Ingest and checks the snapshot accumulates sources, service types, and instance details
// correctly, with addresses resolved from A records. No socket, no network, so it runs anywhere.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/bricola/mdns/MdnsWire.h"
#include "../../src/bricola/mdns/MdnsRadar.h"

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

// Find an instance by name in a snapshot.
static const RadarInstance* FindInstance(const RadarSnapshot& snap, const std::string& name)
{
	for (const RadarInstance& i : snap.instances)
		if (i.name == name)
			return &i;
	return nullptr;
}

static bool HasService(const RadarSnapshot& snap, const std::string& type)
{
	for (const RadarService& s : snap.services)
		if (s.type == type)
			return true;
	return false;
}

int main()
{
	MdnsRadar radar; // not started: we drive Ingest directly

	// Packet 1: a DNS-SD service-type enumeration answer (the meta-query response) listing two
	// service types on the host.
	{
		std::vector<OutRecord> answers;
		OutRecord a1{"_services._dns-sd._udp.local", kTypePTR, 4500, false,
			MakePtr("_campiello._tcp.local")};
		OutRecord a2{"_services._dns-sd._udp.local", kTypePTR, 4500, false,
			MakePtr("_smb._tcp.local")};
		answers.push_back(a1);
		answers.push_back(a2);
		std::string pkt = BuildResponse(answers, {});
		radar.Ingest(reinterpret_cast<const uint8_t*>(pkt.data()), pkt.size(), "192.168.1.5", 1000);
	}

	// Packet 2: a full announce for one _campiello instance (PTR + SRV + TXT + A).
	{
		std::vector<OutRecord> answers;
		answers.push_back(OutRecord{"_campiello._tcp.local", kTypePTR, 4500, false,
			MakePtr("Studio._campiello._tcp.local")});
		std::vector<OutRecord> extra;
		extra.push_back(OutRecord{"Studio._campiello._tcp.local", kTypeSRV, 120, true,
			MakeSrv(0, 0, 7442, "studio.local")});
		extra.push_back(OutRecord{"Studio._campiello._tcp.local", kTypeTXT, 4500, true,
			MakeTxt({{"fp", "abc123"}, {"name", "Studio"}})});
		extra.push_back(OutRecord{"studio.local", kTypeA, 120, true, MakeA("192.168.1.5")});
		std::string pkt = BuildResponse(answers, extra);
		radar.Ingest(reinterpret_cast<const uint8_t*>(pkt.data()), pkt.size(), "192.168.1.5", 2000);
	}

	// Packet 3: a service-type enumeration that includes a DNS-SD subtype
	// (_printer._sub._http._tcp.local). The subtype must fold into its parent _http._tcp.local,
	// not appear as its own service.
	{
		std::vector<OutRecord> answers;
		answers.push_back(OutRecord{"_services._dns-sd._udp.local", kTypePTR, 4500, false,
			MakePtr("_printer._sub._http._tcp.local")});
		std::string pkt = BuildResponse(answers, {});
		radar.Ingest(reinterpret_cast<const uint8_t*>(pkt.data()), pkt.size(), "192.168.1.7", 2500);
	}

	RadarSnapshot snap = radar.Snapshot();

	// Three datagrams from two sources (192.168.1.5 twice, 192.168.1.7 once).
	CHECK(snap.totalPackets == 3);
	CHECK(snap.droppedPackets == 0);
	CHECK(snap.sources.size() == 2);
	CHECK(snap.sources[0].ip == "192.168.1.5");
	CHECK(snap.sources[0].packets == 2);

	// Both service types were seen.
	CHECK(HasService(snap, "_campiello._tcp.local"));
	CHECK(HasService(snap, "_smb._tcp.local"));

	// The _campiello instance is fully resolved.
	const RadarInstance* inst = FindInstance(snap, "Studio._campiello._tcp.local");
	CHECK(inst != nullptr);
	if (inst != nullptr) {
		CHECK(inst->type == "_campiello._tcp.local");
		CHECK(inst->host == "studio.local");
		CHECK(inst->port == 7442);
		CHECK(inst->addrs.size() == 1);
		CHECK(!inst->addrs.empty() && inst->addrs[0] == "192.168.1.5");
		bool sawFp = false;
		for (const auto& kv : inst->txt)
			if (kv.first == "fp" && kv.second == "abc123")
				sawFp = true;
		CHECK(sawFp);
	}

	// The _campiello service reports exactly one instance; _smb has none (only enumerated).
	for (const RadarService& s : snap.services) {
		if (s.type == "_campiello._tcp.local")
			CHECK(s.instances == 1);
		if (s.type == "_smb._tcp.local")
			CHECK(s.instances == 0);
	}

	// The _http service exists with the _printer subtype folded in; the raw subtype string is
	// never a standalone service.
	CHECK(HasService(snap, "_http._tcp.local"));
	CHECK(!HasService(snap, "_printer._sub._http._tcp.local"));
	for (const RadarService& s : snap.services) {
		if (s.type == "_http._tcp.local") {
			bool sawPrinter = false;
			for (const std::string& st : s.subtypes)
				if (st == "_printer")
					sawPrinter = true;
			CHECK(sawPrinter);
		}
	}

	// A garbage datagram is counted but dropped, not fatal.
	{
		const uint8_t junk[] = {0x00, 0x01, 0x02};
		radar.Ingest(junk, sizeof(junk), "10.0.0.9", 3000);
		RadarSnapshot s2 = radar.Snapshot();
		CHECK(s2.totalPackets == 4);
		CHECK(s2.droppedPackets == 1);
		CHECK(s2.sources.size() == 3); // 192.168.1.5, 192.168.1.7, and the junk sender 10.0.0.9
	}

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
