// test_netintel.cpp
//
// Unit test for the pure encode/parse steps of NetIntel (the LANterna-derived LAN-intel module):
// OUI parsing/lookup, the Wake-on-LAN magic packet, /24 directed broadcast, the NBSTAT query and
// response parse, and SSDP header extraction / type inference. No sockets, so it runs offline.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../../src/vicinato/NetIntel.h"

using namespace campiello::vicinato;

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

// Append a 15-byte space-padded NetBIOS name, its type byte, and 2 flag bytes.
static void AppendName(std::vector<uint8_t>& v, const char* name, uint8_t type, uint16_t flags)
{
	char field[15];
	std::memset(field, ' ', sizeof(field));
	size_t n = std::strlen(name);
	if (n > 15) n = 15;
	std::memcpy(field, name, n);
	for (int i = 0; i < 15; ++i) v.push_back(static_cast<uint8_t>(field[i]));
	v.push_back(type);
	v.push_back(static_cast<uint8_t>(flags >> 8));
	v.push_back(static_cast<uint8_t>(flags & 0xFF));
}

int main()
{
	// --- OUI database: parse an IEEE-format snippet, then look vendors up ---
	{
		const char* path = "./netintel_oui_test.tmp";
		std::FILE* f = std::fopen(path, "w");
		CHECK(f != nullptr);
		std::fputs(
			"OUI/MAC hardware address blocks\n"
			"\n"
			"00-50-C2   (hex)\t\tIEEE Registration Authority\n"
			"AC-DE-48   (hex)\t\tPrivate\n"
			"garbage line without hex marker\n",
			f);
		std::fclose(f);

		OuiDatabase db;
		size_t loaded = db.LoadFromFile(path);
		CHECK(loaded == 2);
		CHECK(db.Lookup("00:50:c2:11:22:33") == "IEEE Registration Authority");
		CHECK(db.Lookup("00-50-C2-AA-BB-CC") == "IEEE Registration Authority");
		CHECK(db.Lookup("acde4800aabb") == "Private");
		CHECK(db.Lookup("ff:ff:ff:00:00:00").empty()); // unknown prefix
		CHECK(db.Lookup("zz").empty());                // malformed MAC
		std::remove(path);

		OuiDatabase missing;
		CHECK(missing.LoadFromFile("/no/such/oui.txt") == 0);
		CHECK(missing.Empty());
	}

	// --- OuiKey normalisation ---
	{
		CHECK(OuiDatabase::OuiKey("00:50:c2:aa:bb:cc") == "0050C2");
		CHECK(OuiDatabase::OuiKey("0050c2aabbcc") == "0050C2");
		CHECK(OuiDatabase::OuiKey("00:50").empty()); // too short
	}

	// --- Wake-on-LAN magic packet: 6x 0xFF then 16 repeats of the MAC ---
	{
		uint8_t pkt[102];
		CHECK(BuildWolPacket("01:23:45:67:89:ab", pkt));
		for (int i = 0; i < 6; ++i) CHECK(pkt[i] == 0xFF);
		const uint8_t mac[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab};
		for (int rep = 0; rep < 16; ++rep)
			for (int i = 0; i < 6; ++i)
				CHECK(pkt[6 + rep * 6 + i] == mac[i]);
		// Dash-separated and lowercase both parse; short/garbage MACs are rejected.
		CHECK(BuildWolPacket("01-23-45-67-89-AB", pkt));
		CHECK(!BuildWolPacket("01:23:45:67:89", pkt));
		CHECK(!BuildWolPacket("nonsense", pkt));
	}

	// --- /24 directed broadcast ---
	{
		CHECK(DirectedBroadcast("192.168.2.50") == "192.168.2.255");
		CHECK(DirectedBroadcast("10.0.0.1") == "10.0.0.255");
		CHECK(DirectedBroadcast("not-an-ip").empty());
	}

	// --- NBSTAT query shape ---
	{
		std::string q = BuildNbstatQuery();
		CHECK(q.size() == 50);           // 12 header + 34 name (len+32+nul) + 4 (qtype+qclass)
		CHECK((uint8_t)q[5] == 0x01);    // QDCOUNT = 1
		CHECK((uint8_t)q[q.size() - 3] == 0x21); // QTYPE = NBSTAT (33)
		CHECK((uint8_t)q[q.size() - 1] == 0x01); // QCLASS = IN
	}

	// --- NBSTAT response parse: pull the workstation name and the workgroup ---
	{
		std::vector<uint8_t> r;
		// Header: TRN_ID, flags, QDCOUNT, ANCOUNT=1, NSCOUNT, ARCOUNT.
		const uint8_t header[12] = {0, 0, 0x84, 0x00, 0, 0, 0, 1, 0, 0, 0, 0};
		for (uint8_t b : header) r.push_back(b);
		r.push_back(0x00);                        // answer name = root label
		const uint8_t fixed[10] = {0, 0x21, 0, 1, 0, 0, 0, 0, 0, 40}; // type,class,TTL,RDLENGTH
		for (uint8_t b : fixed) r.push_back(b);
		r.push_back(2);                           // NUM_NAMES
		AppendName(r, "DESKTOP-ANNA", 0x00, 0x0400); // unique workstation
		AppendName(r, "WORKGROUP", 0x00, 0x8000);    // group -> workgroup

		std::string name, workgroup;
		CHECK(ParseNbstatResponse(r.data(), r.size(), name, workgroup));
		CHECK(name == "DESKTOP-ANNA");
		CHECK(workgroup == "WORKGROUP");

		// Truncated / empty buffers fail cleanly.
		std::string n2, w2;
		CHECK(!ParseNbstatResponse(r.data(), 8, n2, w2));
		CHECK(!ParseNbstatResponse(nullptr, 0, n2, w2));
	}

	// --- SSDP header extraction (case-insensitive) and type inference ---
	{
		std::string msg =
			"HTTP/1.1 200 OK\r\n"
			"CACHE-CONTROL: max-age=1800\r\n"
			"SERVER: Linux UPnP/1.0 Sonos/57.9\r\n"
			"ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
			"LOCATION: http://192.168.2.40:1400/xml/device_description.xml\r\n"
			"\r\n";
		CHECK(SsdpHeaderValue(msg, "SERVER") == "Linux UPnP/1.0 Sonos/57.9");
		CHECK(SsdpHeaderValue(msg, "server") == "Linux UPnP/1.0 Sonos/57.9"); // case-insensitive
		CHECK(SsdpHeaderValue(msg, "LOCATION").rfind("http://192.168.2.40", 0) == 0);
		CHECK(SsdpHeaderValue(msg, "NOPE").empty());

		SsdpDevice sonos{"Linux UPnP/1.0 Sonos/57.9", "urn:...:ZonePlayer:1", ""};
		CHECK(InferSsdpType(sonos) == "Sonos");
		SsdpDevice igd{"Router/1.0", "urn:schemas-upnp-org:device:InternetGatewayDevice:1", ""};
		CHECK(InferSsdpType(igd) == "Router (UPnP IGD)");
		SsdpDevice dlna{"", "urn:schemas-upnp-org:device:MediaServer:1", ""};
		CHECK(InferSsdpType(dlna) == "Server multimediale DLNA");
		SsdpDevice unknown{"", "", ""};
		CHECK(InferSsdpType(unknown) == "Dispositivo UPnP");
	}

	std::printf("%s: %d checks, %d failures\n",
		gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
