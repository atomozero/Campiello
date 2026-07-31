// test_handshake.cpp
//
// Tests for the HELLO / WELCOME handshake schema: a byte-exact golden that locks the
// canonical field order, round-trips through a full frame, order-independent decode, and
// hostile inputs. Pure standard C++, no framework; non-zero exit on failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/traghetto/wire/Cbor.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/FrameCodec.h"
#include "../../src/traghetto/wire/Handshake.h"

using namespace campiello::wire;

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

using Bytes = std::vector<uint8_t>;

static bool Eq(const Bytes& a, const Bytes& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i] != b[i])
			return false;
	return true;
}

static Bytes Fp(uint8_t fill)
{
	return Bytes(kFingerprintBytes, fill);
}

// Byte-exact golden: locks the canonical map order (v, fp, caps, node) and the field
// encodings, so an accidental reorder or schema drift fails loudly.
static void TestGoldenBytes()
{
	NodeIdentity id;
	id.version = 1;
	id.fingerprint = Fp(0x00);
	id.caps = {"bfs"};
	id.node = "Ada";

	Bytes got = EncodeNodeIdentity(id);

	Bytes want;
	want.push_back(0xa4);                               // map(4)
	want.push_back(0x61); want.push_back('v');          // "v"
	want.push_back(0x01);                               //   1
	want.push_back(0x62); want.push_back('f'); want.push_back('p'); // "fp"
	want.push_back(0x58); want.push_back(0x20);         //   bytes(32)
	for (int i = 0; i < 32; ++i) want.push_back(0x00);  //   32 zero bytes
	want.push_back(0x64); want.push_back('c'); want.push_back('a'); want.push_back('p'); want.push_back('s'); // "caps"
	want.push_back(0x81);                               //   array(1)
	want.push_back(0x63); want.push_back('b'); want.push_back('f'); want.push_back('s'); //   "bfs"
	want.push_back(0x64); want.push_back('n'); want.push_back('o'); want.push_back('d'); want.push_back('e'); // "node"
	want.push_back(0x63); want.push_back('A'); want.push_back('d'); want.push_back('a'); //   "Ada"

	CHECK(Eq(got, want));
}

static void TestRoundTrip()
{
	NodeIdentity id;
	id.version = 1;
	id.fingerprint = Fp(0xAB);
	id.caps = {"bfs", "write"};
	id.node = "Berto's Laptop";

	NodeIdentity out;
	CHECK(DecodeNodeIdentity(EncodeNodeIdentity(id), out));
	CHECK(out.version == 1);
	CHECK(Eq(out.fingerprint, id.fingerprint));
	CHECK(out.caps.size() == 2 && out.caps[0] == "bfs" && out.caps[1] == "write");
	CHECK(out.node == "Berto's Laptop");
	CHECK(out.HasCap("bfs"));
	CHECK(!out.HasCap("query"));
}

// Empty caps round-trips as an empty list.
static void TestEmptyCaps()
{
	NodeIdentity id;
	id.version = 1;
	id.fingerprint = Fp(0x11);
	id.node = "Nano";
	NodeIdentity out;
	CHECK(DecodeNodeIdentity(EncodeNodeIdentity(id), out));
	CHECK(out.caps.empty());
	CHECK(out.node == "Nano");
}

// A HELLO built as a full frame, serialized, parsed, and decoded end to end.
static void TestFrameRoundTrip()
{
	NodeIdentity id;
	id.version = 1;
	id.fingerprint = Fp(0x7F);
	id.caps = {"bfs"};
	id.node = "Ada";

	Frame hello = MakeHello(id, 0x01020304);
	CHECK(hello.type == MessageType::kHello);
	CHECK(hello.requestId == 0x01020304);

	Bytes onWire = EncodeFrame(hello);

	FrameParser parser;
	parser.Feed(onWire.data(), onWire.size());
	Frame parsed;
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	CHECK(parsed.type == MessageType::kHello);
	CHECK(parsed.requestId == 0x01020304);

	NodeIdentity out;
	CHECK(DecodeNodeIdentity(parsed.payload, out));
	CHECK(out.node == "Ada");
	CHECK(out.HasCap("bfs"));

	// WELCOME echoes the request id.
	Frame welcome = MakeWelcome(id, parsed.requestId);
	CHECK(welcome.type == MessageType::kWelcome);
	CHECK(welcome.requestId == 0x01020304);
}

// Decode must not depend on key order: build the map with keys reversed.
static void TestOrderIndependentDecode()
{
	CborWriter w;
	w.MapHeader(4);
	w.Text("node"); w.Text("Zoe");
	w.Text("caps"); w.ArrayHeader(1); w.Text("bfs");
	w.Text("fp");   w.Bytes(Fp(0x22));
	w.Text("v");    w.UInt(1);

	NodeIdentity out;
	CHECK(DecodeNodeIdentity(w.Take(), out));
	CHECK(out.node == "Zoe");
	CHECK(out.version == 1);
	CHECK(out.HasCap("bfs"));
}

// An unknown key is ignored (forward compatibility).
static void TestUnknownKeyIgnored()
{
	CborWriter w;
	w.MapHeader(5);
	w.Text("v");     w.UInt(1);
	w.Text("fp");    w.Bytes(Fp(0x33));
	w.Text("node");  w.Text("Ida");
	w.Text("caps");  w.ArrayHeader(0);
	w.Text("future");w.ArrayHeader(2); w.UInt(7); w.Text("ignored"); // unknown, nested

	NodeIdentity out;
	CHECK(DecodeNodeIdentity(w.Take(), out));
	CHECK(out.node == "Ida");
	CHECK(out.version == 1);
}

static void TestRejects()
{
	// Missing mandatory "fp".
	{ CborWriter w; w.MapHeader(2); w.Text("v"); w.UInt(1); w.Text("node"); w.Text("X");
	  NodeIdentity o; CHECK(!DecodeNodeIdentity(w.Take(), o)); }
	// Fingerprint of the wrong length (31 bytes).
	{ CborWriter w; w.MapHeader(3); w.Text("v"); w.UInt(1);
	  w.Text("fp"); Bytes short31(31, 0); w.Bytes(short31); w.Text("node"); w.Text("X");
	  NodeIdentity o; CHECK(!DecodeNodeIdentity(w.Take(), o)); }
	// Node name too long.
	{ CborWriter w; w.MapHeader(3); w.Text("v"); w.UInt(1); w.Text("fp"); w.Bytes(Fp(0));
	  std::string big(kMaxNodeNameBytes + 1, 'x'); w.Text("node"); w.Text(big);
	  NodeIdentity o; CHECK(!DecodeNodeIdentity(w.Take(), o)); }
	// Too many caps.
	{ CborWriter w; w.MapHeader(4); w.Text("v"); w.UInt(1); w.Text("fp"); w.Bytes(Fp(0));
	  w.Text("caps"); w.ArrayHeader(kMaxCaps + 1);
	  for (size_t i = 0; i < kMaxCaps + 1; ++i) w.Text("c");
	  w.Text("node"); w.Text("X");
	  NodeIdentity o; CHECK(!DecodeNodeIdentity(w.Take(), o)); }
	// Duplicate "v" key.
	{ CborWriter w; w.MapHeader(4); w.Text("v"); w.UInt(1); w.Text("v"); w.UInt(2);
	  w.Text("fp"); w.Bytes(Fp(0)); w.Text("node"); w.Text("X");
	  NodeIdentity o; CHECK(!DecodeNodeIdentity(w.Take(), o)); }
	// Version zero is invalid.
	{ CborWriter w; w.MapHeader(3); w.Text("v"); w.UInt(0); w.Text("fp"); w.Bytes(Fp(0));
	  w.Text("node"); w.Text("X");
	  NodeIdentity o; CHECK(!DecodeNodeIdentity(w.Take(), o)); }
	// Trailing garbage after the map.
	{ CborWriter w; w.MapHeader(3); w.Text("v"); w.UInt(1); w.Text("fp"); w.Bytes(Fp(0));
	  w.Text("node"); w.Text("X"); Bytes b = w.Take(); b.push_back(0x00);
	  NodeIdentity o; CHECK(!DecodeNodeIdentity(b, o)); }
	// Not a map at all.
	{ CborWriter w; w.UInt(5); NodeIdentity o; CHECK(!DecodeNodeIdentity(w.Take(), o)); }
	// Empty payload.
	{ Bytes b; NodeIdentity o; CHECK(!DecodeNodeIdentity(b, o)); }
}

int main()
{
	TestGoldenBytes();
	TestRoundTrip();
	TestEmptyCaps();
	TestFrameRoundTrip();
	TestOrderIndependentDecode();
	TestUnknownKeyIgnored();
	TestRejects();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
