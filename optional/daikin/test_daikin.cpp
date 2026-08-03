// test_daikin.cpp
//
// Unit tests for the Daikin response parser and UI code mappings (no network). Build:
//   g++ -std=c++17 test_daikin.cpp DaikinClient.cpp -o test_daikin && ./test_daikin

#include <cstdio>
#include <string>

#include "DaikinClient.h"

using namespace campiello::daikin;

static int gFail = 0;
#define CHECK(cond) do { if (!(cond)) { \
	std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++gFail; } } while (0)

int main()
{
	// A realistic get_control_info reply.
	{
		Fields f = ParseResponse(
			"ret=OK,pow=1,mode=3,adv=,stemp=25.0,shum=0,dt1=25.0,f_rate=A,f_dir=0");
		CHECK(f.ok);
		CHECK(f.Get("pow") == "1");
		CHECK(f.Get("mode") == "3");
		CHECK(f.Get("stemp") == "25.0");
		CHECK(f.Get("f_rate") == "A");
		CHECK(f.Get("f_dir") == "0");
		CHECK(f.Get("adv") == "");   // empty value preserved
		CHECK(f.Has("adv"));
	}

	// basic_info with a percent-encoded name and a trailing CRLF.
	{
		Fields f = ParseResponse("ret=OK,type=aircon,reg=eu,name=%53%6f%67%67%69%6f,ver=4_2_303\r\n");
		CHECK(f.ok);
		CHECK(f.Get("type") == "aircon");
		CHECK(f.Get("name") == "Soggio");   // %53%6f%67%67%69%6f
		CHECK(f.Get("ver") == "4_2_303");
	}

	// A non-OK reply (e.g. PARAM NG).
	{
		Fields f = ParseResponse("ret=PARAM NG,msg=xxx");
		CHECK(!f.ok);
	}

	// sensor_info with unavailable indoor humidity.
	{
		Fields f = ParseResponse("ret=OK,htemp=24.0,hhum=-,otemp=30.0,err=0,cmpfreq=45");
		CHECK(f.ok);
		CHECK(f.Get("htemp") == "24.0");
		CHECK(f.Get("hhum") == "-");
		CHECK(f.Get("cmpfreq") == "45");
	}

	// UrlDecode leaves malformed escapes verbatim.
	CHECK(UrlDecode("a%2") == "a%2");
	CHECK(UrlDecode("100%") == "100%");
	CHECK(UrlDecode("%41%42C") == "ABC");

	// Mode/fan label mappings.
	CHECK(ModeName(3) == "Raffrescamento");
	CHECK(ModeName(4) == "Riscaldamento");
	CHECK(ModeName(0) == "Automatico");
	CHECK(ModeName(2) == "Deumidificazione");
	CHECK(ModeName(6) == "Ventilazione");
	CHECK(FanRateName("A") == "Automatica");
	CHECK(FanRateName("B") == "Silenziosa");
	CHECK(FanRateName("3") == "Livello 1");
	CHECK(FanRateName("7") == "Livello 5");
	CHECK(FanDirName(3) == "Verticale e orizzontale");

	if (gFail == 0)
		std::printf("all Daikin tests passed\n");
	return gFail == 0 ? 0 : 1;
}
