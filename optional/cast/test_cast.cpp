// test_cast.cpp
//
// Unit tests for the CASTv2 CastMessage protobuf codec, the JSON readers, and the DIAL XmlTag helper
// (no network). Build:
//   g++ -std=c++17 test_cast.cpp CastChannel.cpp DialClient.cpp -lssl -lcrypto -lnetwork -o test_cast
//   ./test_cast

#include <cstdio>
#include <string>

#include "CastChannel.h"
#include "DialClient.h"

using namespace campiello::cast;

static int gFail = 0;
#define CHECK(cond) do { if (!(cond)) { \
	std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++gFail; } } while (0)

int main()
{
	// CastMessage round-trip.
	{
		std::string wire = EncodeCastMessage(
			"urn:x-cast:com.google.cast.receiver", "sender-0", "receiver-0",
			"{\"type\":\"GET_STATUS\",\"requestId\":1}");
		CastMessage m;
		CHECK(DecodeCastMessage(wire, m));
		CHECK(m.source == "sender-0");
		CHECK(m.destination == "receiver-0");
		CHECK(m.nameSpace == "urn:x-cast:com.google.cast.receiver");
		CHECK(m.payload == "{\"type\":\"GET_STATUS\",\"requestId\":1}");
	}

	// Empty payload and a namespace with a colon still round-trips.
	{
		std::string wire = EncodeCastMessage("urn:x-cast:com.google.cast.tp.connection",
			"sender-0", "transport-123", "{\"type\":\"CONNECT\"}");
		CastMessage m;
		CHECK(DecodeCastMessage(wire, m));
		CHECK(m.destination == "transport-123");
		CHECK(m.payload == "{\"type\":\"CONNECT\"}");
	}

	// A truncated buffer fails cleanly, not crashes.
	{
		std::string wire = EncodeCastMessage("ns", "s", "d", "payload");
		CastMessage m;
		CHECK(!DecodeCastMessage(wire.substr(0, wire.size() - 3), m));
	}

	// JSON string extraction from a realistic RECEIVER_STATUS.
	{
		std::string json =
			"{\"requestId\":1,\"status\":{\"applications\":[{\"appId\":\"CC1AD845\","
			"\"displayName\":\"Default Media Receiver\",\"sessionId\":\"abc-123\","
			"\"statusText\":\"Ready To Cast\",\"transportId\":\"web-5\"}],"
			"\"volume\":{\"controlType\":\"attenuation\",\"level\":0.35,\"muted\":false}},"
			"\"type\":\"RECEIVER_STATUS\"}";
		CHECK(CastJsonString(json, "appId") == "CC1AD845");
		CHECK(CastJsonString(json, "displayName") == "Default Media Receiver");
		CHECK(CastJsonString(json, "sessionId") == "abc-123");
		CHECK(CastJsonString(json, "transportId") == "web-5");
		double level = 0.0;
		CHECK(CastJsonNumber(json, "level", level));
		CHECK(level > 0.34 && level < 0.36);
		CHECK(json.find("\"muted\":true") == std::string::npos);
	}

	// Missing key returns empty / false.
	{
		CHECK(CastJsonString("{\"a\":\"b\"}", "c") == "");
		double d = 0.0;
		CHECK(!CastJsonNumber("{\"a\":\"b\"}", "level", d));
	}

	// DIAL XmlTag still works (kept for launch/stop of named apps).
	CHECK(XmlTag("<x><state>running</state></x>", "state") == "running");

	if (gFail == 0)
		std::printf("all Cast tests passed\n");
	return gFail == 0 ? 0 : 1;
}
