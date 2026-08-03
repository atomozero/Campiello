// test_nut.cpp
//
// Unit tests for the NUT reply parsers and decoders (no network). Build:
//   g++ -std=c++17 test_nut.cpp NutClient.cpp -lnetwork -o test_nut && ./test_nut

#include <cstdio>
#include <map>
#include <string>

#include "NutClient.h"

using namespace campiello::nut;

static int gFail = 0;
#define CHECK(cond) do { if (!(cond)) { \
	std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++gFail; } } while (0)

int main()
{
	// LIST UPS with a quoted description.
	{
		auto ups = ParseListUps(
			"BEGIN LIST UPS\r\n"
			"UPS eaton \"Eaton 5E in the rack\"\r\n"
			"UPS apc \"APC Back-UPS\"\r\n"
			"END LIST UPS\r\n");
		CHECK(ups.size() == 2);
		CHECK(ups[0].first == "eaton");
		CHECK(ups[0].second == "Eaton 5E in the rack");
		CHECK(ups[1].first == "apc");
	}

	// LIST VAR: only the requested UPS' VAR lines, values unescaped.
	{
		std::string reply =
			"BEGIN LIST VAR eaton\r\n"
			"VAR eaton battery.charge \"100\"\r\n"
			"VAR eaton ups.status \"OL CHRG\"\r\n"
			"VAR eaton ups.model \"5E 850i\"\r\n"
			"VAR eaton device.mfr \"Eaton \\\"Powerware\\\"\"\r\n"
			"VAR other battery.charge \"5\"\r\n"
			"END LIST VAR eaton\r\n";
		auto v = ParseListVar(reply, "eaton");
		CHECK(v["battery.charge"] == "100");
		CHECK(v["ups.status"] == "OL CHRG");
		CHECK(v["ups.model"] == "5E 850i");
		CHECK(v["device.mfr"] == "Eaton \"Powerware\"");   // escaped quotes unescaped
		CHECK(v.find("other") == v.end());                  // other UPS excluded
		CHECK(v.count("battery.charge") == 1);
	}

	// Status decoding.
	CHECK(StatusText("OL") == "in linea");
	CHECK(StatusText("OB LB") == "a batteria, batteria scarica");
	CHECK(StatusText("OL CHRG") == "in linea, in carica");
	CHECK(StatusText("ZZ") == "ZZ"); // unknown flag passthrough

	// Runtime formatting.
	CHECK(RuntimeText("3900") == "1 h 5 min");
	CHECK(RuntimeText("300") == "5 min");
	CHECK(RuntimeText("nan") == "nan"); // non-numeric passthrough

	// Unescape helper.
	CHECK(Unescape("a\\\"b") == "a\"b");
	CHECK(Unescape("c\\\\d") == "c\\d");

	if (gFail == 0)
		std::printf("all NUT tests passed\n");
	return gFail == 0 ? 0 : 1;
}
