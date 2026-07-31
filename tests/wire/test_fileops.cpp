// test_fileops.cpp
//
// Tests for the OPEN / READ / CLOSE file read path: byte-exact goldens, a full
// OPEN -> READ -> CLOSE frame exchange, and hostile inputs. Pure standard C++, no
// framework; non-zero exit on failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/traghetto/wire/Cbor.h"
#include "../../src/traghetto/wire/FileOps.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/FrameCodec.h"

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

// Golden: OPEN request { mode: 1, path: "a" } in canonical order mode, path.
static void TestGoldenOpenRequest()
{
	Bytes got = EncodeOpenRequest("a", kOpenRead);
	Bytes want = {
		0xa2,                             // map(2)
		0x64, 'm', 'o', 'd', 'e', 0x01,   // "mode": 1
		0x64, 'p', 'a', 't', 'h', 0x61, 'a' // "path": "a"
	};
	CHECK(Eq(got, want));
}

// Golden: READ reply { data: h'DEAD' }.
static void TestGoldenReadReply()
{
	Bytes got = EncodeReadReply(Bytes{0xDE, 0xAD});
	Bytes want = {
		0xa1,                             // map(1)
		0x64, 'd', 'a', 't', 'a',         // "data"
		0x42, 0xDE, 0xAD                  // h'DEAD'
	};
	CHECK(Eq(got, want));
}

// Golden: WRITE request { data: h'AA', handle: 1, offset: 0 } in canonical order.
static void TestGoldenWriteRequest()
{
	Bytes got = EncodeWriteRequest(1, 0, Bytes{0xAA});
	Bytes want = {
		0xa3,                                       // map(3)
		0x64, 'd', 'a', 't', 'a', 0x41, 0xAA,       // "data": h'AA'
		0x66, 'h', 'a', 'n', 'd', 'l', 'e', 0x01,   // "handle": 1
		0x66, 'o', 'f', 'f', 's', 'e', 't', 0x00    // "offset": 0
	};
	CHECK(Eq(got, want));
}

// Golden: Ok ack is an empty map.
static void TestGoldenOk()
{
	CHECK(Eq(EncodeOk(), Bytes{0xa0}));
}

static void TestRoundTrips()
{
	{ std::string p; uint32_t m = 0;
	  CHECK(DecodeOpenRequest(EncodeOpenRequest("Condivisa/x.txt", kOpenRead), p, m));
	  CHECK(p == "Condivisa/x.txt" && m == kOpenRead); }
	{ uint64_t h = 0, s = 0;
	  CHECK(DecodeOpenReply(EncodeOpenReply(0xABCD1234ull, 4096), h, s));
	  CHECK(h == 0xABCD1234ull && s == 4096); }
	{ uint64_t h = 0, off = 0; uint32_t len = 0;
	  CHECK(DecodeReadRequest(EncodeReadRequest(7, 65536, 4096), h, off, len));
	  CHECK(h == 7 && off == 65536 && len == 4096); }
	{ Bytes d; Bytes payload = {1, 2, 3, 4, 5};
	  CHECK(DecodeReadReply(EncodeReadReply(payload), d)); CHECK(Eq(d, payload)); }
	{ Bytes d; CHECK(DecodeReadReply(EncodeReadReply(Bytes{}), d)); CHECK(d.empty()); } // EOF read
	{ uint64_t h = 0; CHECK(DecodeCloseRequest(EncodeCloseRequest(9), h)); CHECK(h == 9); }
	{ uint64_t h = 0, off = 0; Bytes d; Bytes payload = {9, 8, 7};
	  CHECK(DecodeWriteRequest(EncodeWriteRequest(3, 128, payload), h, off, d));
	  CHECK(h == 3 && off == 128 && Eq(d, payload)); }
	{ uint64_t h = 0, off = 0; Bytes d;   // empty write is valid
	  CHECK(DecodeWriteRequest(EncodeWriteRequest(3, 0, Bytes{}), h, off, d)); CHECK(d.empty()); }
	{ uint64_t n = 0; CHECK(DecodeWriteReply(EncodeWriteReply(4096), n)); CHECK(n == 4096); }
	{ CHECK(DecodeOk(EncodeOk())); }
}

// A WRITE request/reply exchange over frames.
static void TestWriteExchange()
{
	FrameParser parser;
	Frame parsed;

	Frame write = MakeWriteRequest(0x2000, 512, Bytes{'d', 'a', 't'}, 5);
	CHECK(write.type == MessageType::kWrite);
	Bytes wire = EncodeFrame(write);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	uint64_t h = 0, off = 0; Bytes d;
	CHECK(DecodeWriteRequest(parsed.payload, h, off, d));
	CHECK(h == 0x2000 && off == 512 && d.size() == 3);

	Frame reply = MakeWriteReply(3, 5);
	wire = EncodeFrame(reply);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	CHECK(parsed.requestId == 5);
	uint64_t written = 0;
	CHECK(DecodeWriteReply(parsed.payload, written));
	CHECK(written == 3);
}

// A full OPEN -> READ -> CLOSE exchange over frames.
static void TestFullExchange()
{
	FrameParser parser;
	Frame parsed;

	// Client OPENs.
	Frame open = MakeOpenRequest("nota.txt", kOpenRead, 1);
	CHECK(open.type == MessageType::kOpen);
	Bytes wire = EncodeFrame(open);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	std::string p; uint32_t m = 0;
	CHECK(DecodeOpenRequest(parsed.payload, p, m));
	CHECK(p == "nota.txt");

	// Server replies with a handle and size.
	Frame openReply = MakeOpenReply(0x1000, 2, 1);
	wire = EncodeFrame(openReply);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	CHECK(parsed.requestId == 1);
	uint64_t handle = 0, size = 0;
	CHECK(DecodeOpenReply(parsed.payload, handle, size));
	CHECK(handle == 0x1000 && size == 2);

	// Client READs.
	Frame read = MakeReadRequest(handle, 0, 4096, 2);
	wire = EncodeFrame(read);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	uint64_t rh = 0, roff = 0; uint32_t rlen = 0;
	CHECK(DecodeReadRequest(parsed.payload, rh, roff, rlen));
	CHECK(rh == handle && roff == 0 && rlen == 4096);

	// Server returns two bytes (a short read: EOF).
	Frame readReply = MakeReadReply(Bytes{'h', 'i'}, 2);
	wire = EncodeFrame(readReply);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	Bytes data;
	CHECK(DecodeReadReply(parsed.payload, data));
	CHECK(data.size() == 2 && data[0] == 'h' && data[1] == 'i');
	CHECK(data.size() < rlen); // short read means EOF

	// Client CLOSEs, server acks.
	Frame close = MakeCloseRequest(handle, 3);
	wire = EncodeFrame(close);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	uint64_t ch = 0;
	CHECK(DecodeCloseRequest(parsed.payload, ch));
	CHECK(ch == handle);

	Frame closeReply = MakeCloseReply(3);
	wire = EncodeFrame(closeReply);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	CHECK(DecodeOk(parsed.payload));
}

static void TestUnknownKeyIgnored()
{
	CborWriter w;
	w.MapHeader(3);
	w.Text("mode"); w.UInt(kOpenRead);
	w.Text("path"); w.Text("x");
	w.Text("hint"); w.Bool(true); // unknown
	std::string p; uint32_t m = 0;
	CHECK(DecodeOpenRequest(w.Take(), p, m));
	CHECK(p == "x" && m == kOpenRead);
}

static void TestRejects()
{
	// READ request length over the cap.
	{ CborWriter w; w.MapHeader(3);
	  w.Text("handle"); w.UInt(1); w.Text("length"); w.UInt(kMaxReadLength + 1);
	  w.Text("offset"); w.UInt(0);
	  uint64_t h, off; uint32_t len; CHECK(!DecodeReadRequest(w.Take(), h, off, len)); }
	// READ reply data over the cap.
	{ CborWriter w; w.MapHeader(1); w.Text("data"); w.Bytes(Bytes(kMaxReadLength + 1, 0));
	  Bytes d; CHECK(!DecodeReadReply(w.Take(), d)); }
	// WRITE request data over the cap.
	{ CborWriter w; w.MapHeader(3);
	  w.Text("data"); w.Bytes(Bytes(kMaxWriteLength + 1, 0));
	  w.Text("handle"); w.UInt(1); w.Text("offset"); w.UInt(0);
	  uint64_t h, off; Bytes d; CHECK(!DecodeWriteRequest(w.Take(), h, off, d)); }
	// WRITE request missing data.
	{ CborWriter w; w.MapHeader(2); w.Text("handle"); w.UInt(1); w.Text("offset"); w.UInt(0);
	  uint64_t h, off; Bytes d; CHECK(!DecodeWriteRequest(w.Take(), h, off, d)); }
	// OPEN request missing path.
	{ CborWriter w; w.MapHeader(1); w.Text("mode"); w.UInt(kOpenRead);
	  std::string p; uint32_t m; CHECK(!DecodeOpenRequest(w.Take(), p, m)); }
	// OPEN request empty path.
	{ CborWriter w; w.MapHeader(2); w.Text("mode"); w.UInt(kOpenRead); w.Text("path"); w.Text("");
	  std::string p; uint32_t m; CHECK(!DecodeOpenRequest(w.Take(), p, m)); }
	// OPEN reply missing size.
	{ CborWriter w; w.MapHeader(1); w.Text("handle"); w.UInt(1);
	  uint64_t h, s; CHECK(!DecodeOpenReply(w.Take(), h, s)); }
	// Duplicate handle in READ.
	{ CborWriter w; w.MapHeader(4);
	  w.Text("handle"); w.UInt(1); w.Text("handle"); w.UInt(2);
	  w.Text("length"); w.UInt(1); w.Text("offset"); w.UInt(0);
	  uint64_t h, off; uint32_t len; CHECK(!DecodeReadRequest(w.Take(), h, off, len)); }
	// Trailing garbage after CLOSE.
	{ Bytes b = EncodeCloseRequest(5); b.push_back(0x00);
	  uint64_t h; CHECK(!DecodeCloseRequest(b, h)); }
	// Ok that is not a map.
	{ CborWriter w; w.UInt(0); CHECK(!DecodeOk(w.Take())); }
}

int main()
{
	TestGoldenOpenRequest();
	TestGoldenReadReply();
	TestGoldenWriteRequest();
	TestGoldenOk();
	TestRoundTrips();
	TestFullExchange();
	TestWriteExchange();
	TestUnknownKeyIgnored();
	TestRejects();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
