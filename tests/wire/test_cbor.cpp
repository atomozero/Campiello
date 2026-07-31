// test_cbor.cpp
//
// Tests for the minimal CBOR codec. Encoding is checked against the known-answer vectors
// in RFC 8949 Appendix A. Decoding is checked by round-trip and by hostile inputs. Pure
// standard C++, no framework; returns non-zero on any failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/traghetto/wire/Cbor.h"

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

// Encode a single uint and compare to an expected byte vector.
static void CheckUInt(uint64_t v, const Bytes& want)
{
	CborWriter w;
	w.UInt(v);
	CHECK(Eq(w.Buffer(), want));
}

static void CheckInt(int64_t v, const Bytes& want)
{
	CborWriter w;
	w.Int(v);
	CHECK(Eq(w.Buffer(), want));
}

// RFC 8949 Appendix A known-answer vectors for integers.
static void TestGoldenIntegers()
{
	CheckUInt(0, {0x00});
	CheckUInt(1, {0x01});
	CheckUInt(10, {0x0a});
	CheckUInt(23, {0x17});
	CheckUInt(24, {0x18, 0x18});
	CheckUInt(25, {0x18, 0x19});
	CheckUInt(100, {0x18, 0x64});
	CheckUInt(1000, {0x19, 0x03, 0xe8});
	CheckUInt(1000000, {0x1a, 0x00, 0x0f, 0x42, 0x40});
	CheckUInt(1000000000000ull, {0x1b, 0x00, 0x00, 0x00, 0xe8, 0xd4, 0xa5, 0x10, 0x00});
	CheckUInt(18446744073709551615ull,
		{0x1b, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});

	CheckInt(-1, {0x20});
	CheckInt(-10, {0x29});
	CheckInt(-100, {0x38, 0x63});
	CheckInt(-1000, {0x39, 0x03, 0xe7});
	// INT64_MIN: argument is 0x7fffffffffffffff.
	CheckInt((int64_t)(-9223372036854775807LL - 1),
		{0x3b, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
	// A non-negative value through Int() encodes as major type 0.
	CheckInt(10, {0x0a});
}

// RFC 8949 Appendix A vectors for strings, booleans, null.
static void TestGoldenStringsAndSimple()
{
	{ CborWriter w; w.Text(""); CHECK(Eq(w.Buffer(), {0x60})); }
	{ CborWriter w; w.Text("a"); CHECK(Eq(w.Buffer(), {0x61, 0x61})); }
	{ CborWriter w; w.Text("IETF"); CHECK(Eq(w.Buffer(), {0x64, 0x49, 0x45, 0x54, 0x46})); }
	{ CborWriter w; Bytes b; w.Bytes(b); CHECK(Eq(w.Buffer(), {0x40})); }
	{ CborWriter w; Bytes b = {0x01, 0x02, 0x03, 0x04}; w.Bytes(b);
	  CHECK(Eq(w.Buffer(), {0x44, 0x01, 0x02, 0x03, 0x04})); }
	{ CborWriter w; w.Bool(false); CHECK(Eq(w.Buffer(), {0xf4})); }
	{ CborWriter w; w.Bool(true); CHECK(Eq(w.Buffer(), {0xf5})); }
	{ CborWriter w; w.Null(); CHECK(Eq(w.Buffer(), {0xf6})); }
}

// RFC 8949 Appendix A vectors for containers.
static void TestGoldenContainers()
{
	{ CborWriter w; w.ArrayHeader(0); CHECK(Eq(w.Buffer(), {0x80})); }
	{ CborWriter w; w.ArrayHeader(3); w.UInt(1); w.UInt(2); w.UInt(3);
	  CHECK(Eq(w.Buffer(), {0x83, 0x01, 0x02, 0x03})); }
	{ CborWriter w; w.MapHeader(0); CHECK(Eq(w.Buffer(), {0xa0})); }
	{ CborWriter w; w.MapHeader(2); w.UInt(1); w.UInt(2); w.UInt(3); w.UInt(4);
	  CHECK(Eq(w.Buffer(), {0xa2, 0x01, 0x02, 0x03, 0x04})); }
}

// Encode a small map with mixed field types, then decode it back field by field.
static void TestRoundTripMap()
{
	CborWriter w;
	w.MapHeader(4);
	w.Text("node");    w.Text("Berto");
	w.Text("port");    w.UInt(7841);
	w.Text("bfs");     w.Bool(true);
	w.Text("blob");    w.Bytes(Bytes{0xDE, 0xAD, 0xBE, 0xEF});
	Bytes enc = w.Take();

	CborReader r(enc);
	uint64_t count = 0;
	CHECK(r.ReadMapHeader(count));
	CHECK(count == 4);

	std::string k, node;
	uint64_t port = 0;
	bool bfs = false;
	Bytes blob;

	CHECK(r.ReadText(k) && k == "node"); CHECK(r.ReadText(node) && node == "Berto");
	CHECK(r.ReadText(k) && k == "port"); CHECK(r.ReadUInt(port) && port == 7841);
	CHECK(r.ReadText(k) && k == "bfs");  CHECK(r.ReadBool(bfs) && bfs == true);
	CHECK(r.ReadText(k) && k == "blob"); CHECK(r.ReadBytes(blob) && Eq(blob, {0xDE,0xAD,0xBE,0xEF}));
	CHECK(r.AtEnd());
	CHECK(!r.HasError());
}

// Signed round-trip across the interesting boundaries.
static void TestRoundTripInts()
{
	const int64_t values[] = {0, 1, -1, 23, 24, -24, 255, -256, 65535, -65536,
		1000000, -1000000, INT64_MAX, INT64_MIN};
	for (int64_t v : values) {
		CborWriter w;
		w.Int(v);
		Bytes enc = w.Take();
		CborReader r(enc);
		int64_t got = 0;
		CHECK(r.ReadInt(got));
		CHECK(got == v);
		CHECK(r.AtEnd());
	}
	// A uint above INT64_MAX round-trips through ReadUInt but not ReadInt.
	CborWriter w; w.UInt(18446744073709551615ull); Bytes enc = w.Take();
	{ CborReader r(enc); uint64_t u = 0; CHECK(r.ReadUInt(u)); CHECK(u == 18446744073709551615ull); }
	{ CborReader r(enc); int64_t s = 0; CHECK(!r.ReadInt(s)); CHECK(r.HasError()); }
}

// Peek classifies without consuming.
static void TestPeek()
{
	CborWriter w; w.UInt(5); w.Text("hi"); w.Bool(false); w.Null(); w.ArrayHeader(0);
	Bytes enc = w.Take();
	CborReader r(enc);
	CHECK(r.Peek() == CborReader::Type::kUInt);   uint64_t u; r.ReadUInt(u);
	CHECK(r.Peek() == CborReader::Type::kText);   std::string s; r.ReadText(s);
	CHECK(r.Peek() == CborReader::Type::kBool);   bool b; r.ReadBool(b);
	CHECK(r.Peek() == CborReader::Type::kNull);   r.ReadNull();
	CHECK(r.Peek() == CborReader::Type::kArray);
}

// Skip ignores unknown items, including nested containers, so unknown map keys are safe.
static void TestSkip()
{
	CborWriter w;
	w.MapHeader(2);
	w.Text("keep");    w.UInt(1);
	w.Text("unknown"); w.ArrayHeader(2); w.MapHeader(1); w.Text("x"); w.UInt(9); w.Text("deep");
	Bytes enc = w.Take();

	CborReader r(enc);
	uint64_t count = 0;
	CHECK(r.ReadMapHeader(count));
	CHECK(count == 2);
	std::string k;
	uint64_t v = 0;
	CHECK(r.ReadText(k) && k == "keep"); CHECK(r.ReadUInt(v) && v == 1);
	CHECK(r.ReadText(k) && k == "unknown");
	CHECK(r.Skip());   // skips the whole [ {x:9}, "deep" ] value
	CHECK(r.AtEnd());
	CHECK(!r.HasError());
}

// Hostile inputs must fail cleanly, never over-read or over-allocate.
static void TestHostileInputs()
{
	// Text claiming 10 bytes but only 1 present.
	{ Bytes b = {0x6a, 0x41}; CborReader r(b); std::string s; CHECK(!r.ReadText(s)); CHECK(r.HasError()); }
	// Byte string claiming 8-byte huge length (0x5b + 0xffffffffffffffff).
	{ Bytes b = {0x5b, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}; CborReader r(b);
	  Bytes out; CHECK(!r.ReadBytes(out)); CHECK(r.HasError()); }
	// Reserved additional info 28.
	{ Bytes b = {0x1c}; CborReader r(b); uint64_t u; CHECK(!r.ReadUInt(u)); CHECK(r.HasError()); }
	// Indefinite-length array (0x9f) is unsupported.
	{ Bytes b = {0x9f, 0x01, 0xff}; CborReader r(b); uint64_t c; CHECK(!r.ReadArrayHeader(c)); }
	// Float (single, 0xfa) is unsupported: Peek is invalid and Skip fails.
	{ Bytes b = {0xfa, 0x00, 0x00, 0x00, 0x00}; CborReader r(b);
	  CHECK(r.Peek() == CborReader::Type::kInvalid); CHECK(!r.Skip()); }
	// Tag (major 6, 0xc0) is unsupported.
	{ Bytes b = {0xc0, 0x00}; CborReader r(b); CHECK(r.Peek() == CborReader::Type::kInvalid); CHECK(!r.Skip()); }
	// Array header with an absurd count (more than remaining bytes) is rejected.
	{ Bytes b = {0x9b, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}; CborReader r(b);
	  uint64_t c; CHECK(!r.ReadArrayHeader(c)); CHECK(r.HasError()); }
	// Type mismatch: reading text where a uint sits latches error.
	{ Bytes b = {0x01}; CborReader r(b); std::string s; CHECK(!r.ReadText(s)); CHECK(r.HasError()); }
	// Truncated multi-byte argument (0x19 needs two more bytes, only one present).
	{ Bytes b = {0x19, 0x03}; CborReader r(b); uint64_t u; CHECK(!r.ReadUInt(u)); CHECK(r.HasError()); }
	// Empty buffer.
	{ Bytes b; CborReader r(b); CHECK(r.Peek() == CborReader::Type::kInvalid);
	  uint64_t u; CHECK(!r.ReadUInt(u)); }
}

int main()
{
	TestGoldenIntegers();
	TestGoldenStringsAndSimple();
	TestGoldenContainers();
	TestRoundTripMap();
	TestRoundTripInts();
	TestPeek();
	TestSkip();
	TestHostileInputs();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
