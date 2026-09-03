// test_shelly.cpp
//
// Unit test for the dependency-free ShellyClient parsing: the JSON scanners, generation detection,
// and status-body -> channel mapping for both device generations. The wire bodies below are the
// shape returned by real devices (a Shelly Plus Plug S gen2 and a Shelly EM / Shelly 1PM gen1).
//
// Optional live mode: `test_shelly <host> [gen]` runs FetchInfo + FetchChannels against a real
// device (and, with a third arg "toggle", flips channel 0), to confirm the transport end to end.
//
//   g++ -std=c++17 -O2 -o test_shelly test_shelly.cpp ShellyClient.cpp -lnetwork
//   ./test_shelly            # offline parsing checks
//   ./test_shelly 192.168.188.132 2          # live, gen2
//   ./test_shelly 192.168.188.132 2 toggle   # live, flips channel 0

#include <cstdio>
#include <cstring>
#include <string>

#include "ShellyClient.h"

using namespace campiello::shelly;

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

static bool NearEq(double a, double b) { return (a - b) < 0.01 && (b - a) < 0.01; }

// GET /shelly bodies (identity + generation).
static const char* kShellyGen2 =
	"{\"name\":null,\"id\":\"shellyplusplugs-80646fd03dc8\",\"mac\":\"80646FD03DC8\","
	"\"model\":\"SNPL-00112EU\",\"gen\":2,\"fw_id\":\"20260311-095859/1.7.5\",\"ver\":\"1.7.5\","
	"\"app\":\"PlusPlugS\",\"auth_en\":false,\"auth_domain\":null,\"profile\":\"switch\"}";
static const char* kShellyGen1EM =
	"{\"type\":\"SHEM\",\"mac\":\"98CDAC1EEF64\",\"auth\":false,"
	"\"fw\":\"20230913-114150/v1.14.0-gcb84623\",\"discoverable\":false,\"num_emeters\":2}";

// GET /rpc/Shelly.GetStatus (gen2 plug, output on, drawing 12.3 W).
static const char* kStatusGen2 =
	"{\"ble\":{},\"cloud\":{\"connected\":false},\"mqtt\":{\"connected\":false},"
	"\"switch:0\":{\"id\":0,\"source\":\"HTTP\",\"output\":true,\"apower\":12.3,\"voltage\":231.4,"
	"\"current\":0.05,\"aenergy\":{\"total\":1234.5,\"by_minute\":[0,0,0],\"minute_ts\":170},"
	"\"temperature\":{\"tC\":45.2,\"tF\":113.4}},\"sys\":{\"mac\":\"80646FD03DC8\"}}";

// GET /status (gen1 Shelly 1PM: one relay + one meter, total in watt-minutes).
static const char* kStatusGen1PM =
	"{\"wifi_sta\":{\"connected\":true},\"relays\":[{\"ison\":true,\"has_timer\":false,"
	"\"overpower\":false,\"is_valid\":true}],\"meters\":[{\"power\":15.0,\"is_valid\":true,"
	"\"timestamp\":170,\"counters\":[0,0,0],\"total\":60}],\"update\":{}}";

// GET /status (gen1 Shelly EM: one relay + two clamp emeters, energy in Wh).
static const char* kStatusGen1EM =
	"{\"relays\":[{\"ison\":false,\"has_timer\":false,\"is_valid\":true}],"
	"\"emeters\":[{\"power\":123.45,\"reactive\":10.2,\"voltage\":230.5,\"is_valid\":true,"
	"\"total\":56789.0,\"total_returned\":0.0},{\"power\":67.8,\"reactive\":1.0,\"voltage\":230.6,"
	"\"is_valid\":true,\"total\":12345.0,\"total_returned\":0.0}]}";

static void OfflineChecks()
{
	// --- JSON scanners
	CHECK(JsonString(kShellyGen2, "id") == "shellyplusplugs-80646fd03dc8");
	CHECK(JsonString(kShellyGen2, "app") == "PlusPlugS");
	bool b = true;
	CHECK(JsonBool(kShellyGen2, "auth_en", b) && b == false);
	double d = 0;
	CHECK(JsonNumber(kStatusGen2, "apower", d) && NearEq(d, 12.3));
	// "name":null must not parse as a number/string value.
	CHECK(JsonString(kShellyGen2, "name").empty());
	CHECK(!JsonNumber(kShellyGen2, "name", d));
	// Balanced block extraction, including a key that itself contains a colon.
	std::string sw = JsonValueBlock(kStatusGen2, "switch:0");
	CHECK(!sw.empty() && sw.front() == '{' && sw.back() == '}');
	CHECK(JsonValueBlock(sw, "aenergy").find("1234.5") != std::string::npos);

	// --- generation detection
	CHECK(GenerationFromShelly(kShellyGen2) == 2);
	CHECK(GenerationFromShelly(kShellyGen1EM) == 1); // no "gen" field -> gen1

	// --- gen2 channel mapping
	auto g2 = ParseChannelsGen2(kStatusGen2);
	CHECK(g2.size() == 1);
	CHECK(g2[0].id == 0 && g2[0].controllable && g2[0].on);
	CHECK(g2[0].hasPower && NearEq(g2[0].power, 12.3));
	CHECK(NearEq(g2[0].voltage, 231.4));
	CHECK(NearEq(g2[0].total, 1234.5)); // aenergy.total, Wh

	// --- gen1 1PM: relay + meter; Wmin total (60) becomes 1.0 Wh
	auto g1 = ParseChannelsGen1(kStatusGen1PM);
	CHECK(g1.size() == 1);
	CHECK(g1[0].controllable && g1[0].on);
	CHECK(g1[0].hasPower && NearEq(g1[0].power, 15.0));
	CHECK(NearEq(g1[0].total, 1.0)); // 60 Wmin / 60 = 1 Wh

	// --- gen1 EM: one controllable relay + two read-only clamp meters (Wh totals)
	auto em = ParseChannelsGen1(kStatusGen1EM);
	CHECK(em.size() == 3);
	CHECK(em[0].controllable && !em[0].on);          // the relay
	CHECK(!em[1].controllable && em[1].hasPower);    // clamp 1
	CHECK(NearEq(em[1].power, 123.45) && NearEq(em[1].voltage, 230.5));
	CHECK(NearEq(em[1].total, 56789.0));             // Wh, as reported
	CHECK(!em[2].controllable && NearEq(em[2].power, 67.8));
}

static int LiveRun(int argc, char** argv)
{
	std::string host = argv[1];
	int gen = (argc > 2) ? std::atoi(argv[2]) : 0;
	bool doToggle = (argc > 3) && std::strcmp(argv[3], "toggle") == 0;

	ShellyClient c(host, 80, gen);
	ShellyInfo info;
	if (!c.FetchInfo(info)) {
		std::printf("FetchInfo failed (unreachable?)\n");
		return 1;
	}
	std::printf("gen%d  model=%s app=%s fw=%s mac=%s auth=%s\n", info.gen, info.model.c_str(),
		info.app.c_str(), info.fw.c_str(), info.mac.c_str(), info.authEnabled ? "on" : "off");
	std::vector<ShellyChannel> chans;
	if (!c.FetchChannels(chans)) {
		std::printf("FetchChannels failed\n");
		return 1;
	}
	for (const auto& ch : chans) {
		std::printf("  [%d] %s %s", ch.id, ch.name.c_str(),
			ch.controllable ? (ch.on ? "ON " : "OFF") : "---");
		if (ch.hasPower) std::printf("  %.1f W", ch.power);
		if (ch.voltage > 0) std::printf("  %.0f V", ch.voltage);
		if (ch.total > 0) std::printf("  %.1f Wh", ch.total);
		std::printf("\n");
	}
	if (doToggle) {
		std::printf("toggle channel 0 -> %s\n", c.Toggle(0) ? "OK" : "FAIL");
	}
	return 0;
}

int main(int argc, char** argv)
{
	if (argc > 1)
		return LiveRun(argc, argv);
	OfflineChecks();
	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
