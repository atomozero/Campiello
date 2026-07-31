// test_listing.cpp
//
// Tests for the STAT / LIST schema and the AttrSet codec: byte-exact goldens that lock
// the canonical field order, round-trips through full frames, forward-compatible unknown
// keys, and hostile inputs. Pure standard C++, no framework; non-zero exit on failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/traghetto/wire/Attributes.h"
#include "../../src/traghetto/wire/Cbor.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/FrameCodec.h"
#include "../../src/traghetto/wire/Listing.h"

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

static bool ReadWholeAttrSet(const Bytes& b, AttrSet& out)
{
	CborReader r(b);
	return ReadAttrSet(r, out) && r.AtEnd() && !r.HasError();
}

// Golden: a one-attribute set locks the array/map layout and the n,t,v key order.
static void TestGoldenAttrSet()
{
	AttrSet set = { Attr{ "a", 1, Bytes{0x01} } };
	CborWriter w;
	WriteAttrSet(w, set);
	Bytes want = {
		0x81,             // array(1)
		0xa3,             // map(3)
		0x61, 'n', 0x61, 'a', // "n": "a"
		0x61, 't', 0x01,      // "t": 1
		0x61, 'v', 0x41, 0x01 // "v": h'01'
	};
	CHECK(Eq(w.Buffer(), want));
}

// Golden: a Stat for a regular file 0644 (st_mode 0x81A4) locks m,ct,mt,sz,ino order.
static void TestGoldenStat()
{
	Stat s;
	s.mode = 0x81A4;
	CborWriter w;
	WriteStat(w, s);
	Bytes want = {
		0xa5,                   // map(5)
		0x61, 'm', 0x19, 0x81, 0xa4, // "m": 33188
		0x62, 'c', 't', 0x00,   // "ct": 0
		0x62, 'm', 't', 0x00,   // "mt": 0
		0x62, 's', 'z', 0x00,   // "sz": 0
		0x63, 'i', 'n', 'o', 0x00 // "ino": 0
	};
	CHECK(Eq(w.Buffer(), want));
}

static void TestAttrSetRoundTrip()
{
	AttrSet set = {
		Attr{ "BEOS:TYPE", 0x4D494D53 /* 'MIMS' */, {} },
		Attr{ "Audio:Rating", 0x4C4F4E47 /* 'LONG' */, Bytes{0x05, 0x00, 0x00, 0x00} },
		Attr{ "empty", 0x52415754 /* 'RAWT' */, {} },
	};
	CborWriter w;
	WriteAttrSet(w, set);
	AttrSet out;
	CHECK(ReadWholeAttrSet(w.Take(), out));
	CHECK(out.size() == 3);
	CHECK(out[0].name == "BEOS:TYPE" && out[0].type == 0x4D494D53 && out[0].value.empty());
	CHECK(out[1].name == "Audio:Rating" && out[1].type == 0x4C4F4E47);
	CHECK(Eq(out[1].value, {0x05, 0x00, 0x00, 0x00}));
	CHECK(out[2].value.empty());
	// Empty set round-trips.
	CborWriter e; WriteAttrSet(e, {}); AttrSet empty;
	CHECK(ReadWholeAttrSet(e.Take(), empty)); CHECK(empty.empty());
}

static void TestStatRoundTrip()
{
	Stat s;
	s.mode = 0x41ED;         // directory 0755
	s.size = 4096;
	s.mtime = 1751500000;
	s.crtime = 1751400000;
	s.inode = 0x1122334455667788ull;

	CborWriter w;
	WriteStat(w, s);
	CborReader r(w.Take());
	Stat out;
	CHECK(ReadStat(r, out) && r.AtEnd());
	CHECK(out.mode == s.mode);
	CHECK(out.size == s.size);
	CHECK(out.mtime == s.mtime);
	CHECK(out.crtime == s.crtime);
	CHECK(out.inode == s.inode);
}

static Entry SampleEntry(const std::string& name)
{
	Entry e;
	e.name = name;
	e.stat.mode = 0x81A4;
	e.stat.size = 12;
	e.stat.mtime = 1000;
	e.stat.crtime = 900;
	e.stat.inode = 7;
	e.attrs = { Attr{ "BEOS:TYPE", 0x4D494D53, {'t','x','t'} } };
	return e;
}

// STAT reply as a full frame: encode -> parse -> decode.
static void TestStatFrameRoundTrip()
{
	Entry e = SampleEntry("nota.txt");
	Frame f = MakeStatReply(e, 99);
	CHECK(f.type == MessageType::kStat);
	Bytes onWire = EncodeFrame(f);

	FrameParser parser;
	parser.Feed(onWire.data(), onWire.size());
	Frame parsed;
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	CHECK(parsed.requestId == 99);

	Entry out;
	CHECK(DecodeStatReply(parsed.payload, out));
	CHECK(out.name == "nota.txt");
	CHECK(out.stat.size == 12);
	CHECK(out.attrs.size() == 1 && out.attrs[0].name == "BEOS:TYPE");
}

// LIST request and reply through frames.
static void TestListFrameRoundTrip()
{
	Frame req = MakeListRequest("Condivisa/musica", 7);
	CHECK(req.type == MessageType::kList);
	std::string path;
	CHECK(DecodePathRequest(req.payload, path));
	CHECK(path == "Condivisa/musica");

	std::vector<Entry> entries = { SampleEntry("a.mp3"), SampleEntry("b.mp3"), SampleEntry("sub") };
	Frame reply = MakeListReply(entries, 7);
	Bytes onWire = EncodeFrame(reply);

	FrameParser parser;
	parser.Feed(onWire.data(), onWire.size());
	Frame parsed;
	CHECK(parser.Next(parsed) == ParseResult::kFrame);

	std::vector<Entry> out;
	CHECK(DecodeListing(parsed.payload, out));
	CHECK(out.size() == 3);
	CHECK(out[0].name == "a.mp3" && out[2].name == "sub");
	CHECK(out[1].attrs.size() == 1);
	// Empty listing round-trips.
	std::vector<Entry> none;
	CHECK(DecodeListing(EncodeListing(none), out));
	CHECK(out.empty());
}

// Unknown keys in stat / entry / path request are skipped (forward compatibility).
static void TestUnknownKeysIgnored()
{
	// Stat with an extra key.
	CborWriter w;
	w.MapHeader(6);
	w.Text("m");   w.UInt(0x81A4);
	w.Text("ct");  w.Int(0);
	w.Text("mt");  w.Int(0);
	w.Text("sz");  w.UInt(0);
	w.Text("ino"); w.UInt(0);
	w.Text("atime"); w.Int(12345); // unknown, deferred field
	CborReader r(w.Take());
	Stat s;
	CHECK(ReadStat(r, s) && r.AtEnd());
	CHECK(s.mode == 0x81A4);
}

static void TestRejects()
{
	// PathRequest: empty path.
	{ CborWriter w; w.MapHeader(1); w.Text("path"); w.Text(""); std::string p;
	  CHECK(!DecodePathRequest(w.Take(), p)); }
	// PathRequest: path too long.
	{ CborWriter w; w.MapHeader(1); w.Text("path"); w.Text(std::string(kMaxPathBytes + 1, 'x'));
	  std::string p; CHECK(!DecodePathRequest(w.Take(), p)); }
	// PathRequest: missing path.
	{ CborWriter w; w.MapHeader(0); std::string p; CHECK(!DecodePathRequest(w.Take(), p)); }
	// PathRequest: trailing garbage.
	{ CborWriter w; w.MapHeader(1); w.Text("path"); w.Text("x"); Bytes b = w.Take();
	  b.push_back(0x00); std::string p; CHECK(!DecodePathRequest(b, p)); }

	// Attr: empty name.
	{ CborWriter w; w.ArrayHeader(1); w.MapHeader(3);
	  w.Text("n"); w.Text(""); w.Text("t"); w.UInt(1); w.Text("v"); w.Bytes(Bytes{});
	  AttrSet o; CHECK(!ReadWholeAttrSet(w.Take(), o)); }
	// Attr: name too long.
	{ CborWriter w; w.ArrayHeader(1); w.MapHeader(3);
	  w.Text("n"); w.Text(std::string(kMaxAttrNameBytes + 1, 'a'));
	  w.Text("t"); w.UInt(1); w.Text("v"); w.Bytes(Bytes{});
	  AttrSet o; CHECK(!ReadWholeAttrSet(w.Take(), o)); }
	// Attr: missing value.
	{ CborWriter w; w.ArrayHeader(1); w.MapHeader(2);
	  w.Text("n"); w.Text("a"); w.Text("t"); w.UInt(1);
	  AttrSet o; CHECK(!ReadWholeAttrSet(w.Take(), o)); }
	// AttrSet: count over kMaxAttrs.
	{ AttrSet big; big.resize(kMaxAttrs + 1, Attr{ "a", 0, {} });
	  CborWriter w; WriteAttrSet(w, big);
	  AttrSet o; CHECK(!ReadWholeAttrSet(w.Take(), o)); }

	// Stat: missing field (no ino).
	{ CborWriter w; w.MapHeader(4);
	  w.Text("m"); w.UInt(0); w.Text("ct"); w.Int(0); w.Text("mt"); w.Int(0); w.Text("sz"); w.UInt(0);
	  CborReader r(w.Take()); Stat s; CHECK(!ReadStat(r, s)); }
	// Stat: duplicate key.
	{ CborWriter w; w.MapHeader(6);
	  w.Text("m"); w.UInt(0); w.Text("m"); w.UInt(1);
	  w.Text("ct"); w.Int(0); w.Text("mt"); w.Int(0); w.Text("sz"); w.UInt(0); w.Text("ino"); w.UInt(0);
	  CborReader r(w.Take()); Stat s; CHECK(!ReadStat(r, s)); }

	// Entry: empty name.
	{ Entry e = SampleEntry(""); Bytes b = EncodeStatReply(e); Entry o;
	  CHECK(!DecodeStatReply(b, o)); }
	// STAT reply: trailing garbage.
	{ Entry e = SampleEntry("x"); Bytes b = EncodeStatReply(e); b.push_back(0x00);
	  Entry o; CHECK(!DecodeStatReply(b, o)); }
	// Listing: missing entries key.
	{ CborWriter w; w.MapHeader(0); std::vector<Entry> o; CHECK(!DecodeListing(w.Take(), o)); }
}

int main()
{
	TestGoldenAttrSet();
	TestGoldenStat();
	TestAttrSetRoundTrip();
	TestStatRoundTrip();
	TestStatFrameRoundTrip();
	TestListFrameRoundTrip();
	TestUnknownKeysIgnored();
	TestRejects();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
