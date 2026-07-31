// test_radarlabels.cpp
//
// Unit test for the radar's label/TXT decoding and report writer (RadarLabels). Pure standard
// C++, runs anywhere: it checks the service catalog, the TXT key labels, the HomeKit/Matter
// value decoders, the instance summary, and that a hand-built snapshot renders a report carrying
// the decoded text.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/bricola/mdns/RadarLabels.h"

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

static bool Contains(const std::string& hay, const std::string& needle)
{
	return hay.find(needle) != std::string::npos;
}

int main()
{
	// Service catalog: known and fallback.
	CHECK(LookupService("_hue._tcp.local").label == "Philips Hue");
	CHECK(LookupService("_hap._tcp.local").label == "Apple HomeKit");
	CHECK(LookupService("_matterd._udp.local").category == "Casa");
	CHECK(LookupService("_wat._tcp.local").category == "Altro");
	CHECK(DeriveServiceLabel("_matterd._udp.local") == "matterd");

	// TXT key labels, service-aware.
	CHECK(TxtKeyLabel("_hap._tcp.local", "ci") == "Categoria");
	CHECK(TxtKeyLabel("_hap._tcp.local", "ff") == "Flag funzionalita");
	CHECK(TxtKeyLabel("_hap._tcp.local", "sh") == "Hash configurazione");
	CHECK(TxtKeyLabel("_matterd._udp.local", "DT") == "Tipo dispositivo");
	CHECK(TxtKeyLabel("_hue._tcp.local", "modelid") == "ID modello");
	CHECK(TxtKeyLabel("_hue._tcp.local", "zz") == "zz"); // unknown -> raw

	// Amazon Whisperplay keys.
	CHECK(TxtKeyLabel("_amzn-wplay._tcp.local", "c") == "Indirizzo MAC");
	CHECK(TxtKeyLabel("_amzn-wplay._tcp.local", "ad") == "Seriale dispositivo");
	CHECK(TxtKeyLabel("_amzn-wplay._tcp.local", "sp") == "Porta sicura");
	CHECK(TxtKeyLabel("_amzn-wplay._tcp.local", "tr") == "Trasporto");

	// HomeKit feature-flag decode.
	CHECK(Contains(DecodeTxtValue("_hap._tcp.local", "ff", "2"), "autenticazione software"));
	CHECK(Contains(DecodeTxtValue("_hap._tcp.local", "ff", "0"), "nessuna"));

	// HomeKit category decode (ci=5 -> Lampadina), status flags, protocol version passthrough.
	CHECK(Contains(DecodeTxtValue("_hap._tcp.local", "ci", "5"), "Lampadina"));
	CHECK(Contains(DecodeTxtValue("_hap._tcp.local", "ci", "2"), "Bridge"));
	CHECK(DecodeTxtValue("_hap._tcp.local", "sf", "1") == "non abbinato (visibile)");
	CHECK(DecodeTxtValue("_hap._tcp.local", "sf", "0") == "abbinato");
	CHECK(DecodeTxtValue("_hap._tcp.local", "pv", "1.1") == "1.1"); // untouched

	// Matter device-type decode: decimal 257 and hex 0x0101 both -> dimmable light; 35 -> a
	// casting video player (a Fire TV as a Matter media device).
	CHECK(Contains(DecodeTxtValue("_matterd._udp.local", "DT", "257"), "dimmerabile"));
	CHECK(Contains(DecodeTxtValue("_matterc._udp.local", "DT", "0x0101"), "dimmerabile"));
	CHECK(Contains(DecodeTxtValue("_matterd._udp.local", "DT", "35"), "casting"));
	CHECK(DecodeTxtValue("_matterc._udp.local", "VP", "4361+1") == "Vendor 4361, Prodotto 1");
	// A test/development vendor id (0xFFF1 = 65521) is flagged.
	CHECK(Contains(DecodeTxtValue("_matterd._udp.local", "VP", "65521+32769"), "vendor di test"));
	CHECK(DecodeTxtValue("_matterd._udp.local", "DT", "99999") == "99999"); // unknown -> raw

	// Instance summary.
	CHECK(InstanceSummary("_hap._tcp.local", {{"md", "Hue Bridge"}, {"ci", "2"}})
		== "Hue Bridge - Bridge (2)");
	CHECK(Contains(InstanceSummary("_matterd._udp.local", {{"DT", "257"}}), "dimmerabile"));
	// Matter with a device name plus a decoded type combines them.
	{
		std::string s = InstanceSummary("_matterd._udp.local", {{"DN", "Fire TV"}, {"DT", "35"}});
		CHECK(Contains(s, "Fire TV") && Contains(s, "casting"));
	}
	// Matter with an unrecognized type falls back to the device name.
	CHECK(InstanceSummary("_matterd._udp.local", {{"DN", "Fire TV"}, {"DT", "99999"}})
		== "Fire TV");
	// Amazon Whisperplay exposes its name in "n".
	CHECK(InstanceSummary("_amzn-wplay._tcp.local", {{"n", "FireTVStick di Andrea"}})
		== "FireTVStick di Andrea");
	CHECK(InstanceSummary("_googlecast._tcp.local", {{"fn", "Sala"}, {"md", "Chromecast"}})
		== "Sala (Chromecast)");
	CHECK(InstanceSummary("_unknown._tcp.local", {}).empty());

	// Report writer: a hand-built snapshot renders the decoded HomeKit and Matter text.
	{
		RadarSnapshot snap;
		snap.interfaceIp = "192.168.2.100";
		snap.startMs = 0;
		snap.nowMs = 5000;
		snap.totalPackets = 12;
		snap.totalRecords = 40;

		RadarSource src;
		src.ip = "192.168.2.101";
		src.packets = 12;
		src.records = 40;
		snap.sources.push_back(src);

		RadarService svc;
		svc.type = "_hap._tcp.local";
		svc.instances = 1;
		snap.services.push_back(svc);

		RadarInstance inst;
		inst.name = "Hue Bridge._hap._tcp.local";
		inst.type = "_hap._tcp.local";
		inst.host = "hue.local";
		inst.port = 8080;
		inst.addrs.push_back("192.168.2.101");
		inst.txt = {{"md", "BSB002"}, {"ci", "2"}, {"sf", "0"}};
		snap.instances.push_back(inst);

		std::string report = BuildRadarReport(snap);
		CHECK(Contains(report, "192.168.2.100"));
		CHECK(Contains(report, "Apple HomeKit"));
		CHECK(Contains(report, "Categoria = Bridge (2)"));
		CHECK(Contains(report, "Stato abbinamento = abbinato"));
		CHECK(Contains(report, "192.168.2.101 - Apple HomeKit"));
		CHECK(Contains(report, "cattura di 5s"));
	}

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
