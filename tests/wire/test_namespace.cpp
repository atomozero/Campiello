// test_namespace.cpp
//
// Tests for the CNP namespace-mutation messages MKDIR / UNLINK / RENAME / TRUNCATE: a golden
// for the canonical RENAME key order, round-trips, frame builders (right MessageType, Ok
// replies), and hostile inputs. Pure standard C++, no framework; non-zero exit on failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/traghetto/wire/Cbor.h"
#include "../../src/traghetto/wire/FileOps.h"   // DecodeOk
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/Listing.h"    // DecodePathRequest
#include "../../src/traghetto/wire/Namespace.h"

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

// Golden: RENAME { to: "b", from: "a" }, canonical order to (2) before from (4).
static void TestGoldenRename()
{
	Bytes got = EncodeRenameRequest("a", "b");
	Bytes want = {
		0xa2,                              // map(2)
		0x62, 't', 'o', 0x61, 'b',         // "to": "b"
		0x64, 'f', 'r', 'o', 'm', 0x61, 'a' // "from": "a"
	};
	CHECK(Eq(got, want));
}

static void TestRoundTrips()
{
	{ std::string p; uint32_t m = 0;
	  CHECK(DecodeMkdirRequest(EncodeMkdirRequest("Condivisa/dir", 0755), p, m));
	  CHECK(p == "Condivisa/dir" && m == 0755); }
	{ std::string f, t;
	  CHECK(DecodeRenameRequest(EncodeRenameRequest("old.txt", "new.txt"), f, t));
	  CHECK(f == "old.txt" && t == "new.txt"); }
	{ std::string p; uint64_t s = 0;
	  CHECK(DecodeTruncateRequest(EncodeTruncateRequest("f.bin", 4096), p, s));
	  CHECK(p == "f.bin" && s == 4096); }
	// UNLINK reuses the { path } body.
	{ std::string p;
	  CHECK(DecodePathRequest(MakeUnlinkRequest("gone.txt", 1).payload, p));
	  CHECK(p == "gone.txt"); }
}

static void TestFrameBuilders()
{
	CHECK(MakeMkdirRequest("d", 0755, 1).type == MessageType::kMkdir);
	CHECK(MakeUnlinkRequest("f", 2).type == MessageType::kUnlink);
	CHECK(MakeRenameRequest("a", "b", 3).type == MessageType::kRename);
	CHECK(MakeTruncateRequest("f", 0, 4).type == MessageType::kTruncate);
	// Every reply is an Ok ack echoing the request id.
	Frame r = MakeMkdirReply(7);
	CHECK(r.type == MessageType::kMkdir && r.requestId == 7 && DecodeOk(r.payload));
	CHECK(DecodeOk(MakeUnlinkReply(1).payload));
	CHECK(DecodeOk(MakeRenameReply(1).payload));
	CHECK(DecodeOk(MakeTruncateReply(1).payload));
}

static void TestRejects()
{
	// MKDIR empty path.
	{ CborWriter w; w.MapHeader(2); w.Text("mode"); w.UInt(0755); w.Text("path"); w.Text("");
	  std::string p; uint32_t m; CHECK(!DecodeMkdirRequest(w.Take(), p, m)); }
	// MKDIR missing mode.
	{ CborWriter w; w.MapHeader(1); w.Text("path"); w.Text("d");
	  std::string p; uint32_t m; CHECK(!DecodeMkdirRequest(w.Take(), p, m)); }
	// RENAME missing to.
	{ CborWriter w; w.MapHeader(1); w.Text("from"); w.Text("a");
	  std::string f, t; CHECK(!DecodeRenameRequest(w.Take(), f, t)); }
	// RENAME oversized path.
	{ std::string big(kMaxPathBytes + 1, 'x');
	  std::string f, t; CHECK(!DecodeRenameRequest(EncodeRenameRequest(big, "b"), f, t)); }
	// TRUNCATE trailing garbage.
	{ Bytes b = EncodeTruncateRequest("f", 1); b.push_back(0x00);
	  std::string p; uint64_t s; CHECK(!DecodeTruncateRequest(b, p, s)); }
	// TRUNCATE duplicate size.
	{ CborWriter w; w.MapHeader(3); w.Text("path"); w.Text("f");
	  w.Text("size"); w.UInt(1); w.Text("size"); w.UInt(2);
	  std::string p; uint64_t s; CHECK(!DecodeTruncateRequest(w.Take(), p, s)); }
	// Unknown key is skipped.
	{ CborWriter w; w.MapHeader(3); w.Text("mode"); w.UInt(0700); w.Text("path"); w.Text("d");
	  w.Text("x"); w.Bool(true);
	  std::string p; uint32_t m; CHECK(DecodeMkdirRequest(w.Take(), p, m)); CHECK(p == "d"); }
}

int main()
{
	TestGoldenRename();
	TestRoundTrips();
	TestFrameBuilders();
	TestRejects();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
