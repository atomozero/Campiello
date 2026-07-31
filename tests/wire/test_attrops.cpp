// test_attrops.cpp
//
// Tests for the CNP READ_ATTRS / WRITE_ATTRS messages: round-trips that preserve attribute
// names, type codes, and values (type fidelity is the point), a full WRITE_ATTRS frame
// exchange, and hostile inputs. Pure standard C++, no framework; non-zero exit on failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/traghetto/wire/AttrOps.h"
#include "../../src/traghetto/wire/Attributes.h"
#include "../../src/traghetto/wire/Cbor.h"
#include "../../src/traghetto/wire/FileOps.h"   // DecodeOk
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/FrameCodec.h"
#include "../../src/traghetto/wire/Listing.h"    // DecodePathRequest

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

// A representative attribute set: a MIME string and a typed int32, exercising type fidelity.
static AttrSet SampleAttrs()
{
	AttrSet a;
	Attr mime;
	mime.name = "BEOS:TYPE";
	mime.type = 0x4D494D53; // B_MIME_STRING_TYPE 'MIMS'
	mime.value = Bytes{'t', 'e', 'x', 't', '/', 'p', 'l', 'a', 'i', 'n', 0};
	a.push_back(mime);
	Attr rating;
	rating.name = "Media:Rating";
	rating.type = 0x4C4F4E47; // B_INT32_TYPE 'LONG'
	rating.value = Bytes{5, 0, 0, 0};
	a.push_back(rating);
	return a;
}

static bool AttrsEqual(const AttrSet& a, const AttrSet& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].name != b[i].name || a[i].type != b[i].type || a[i].value != b[i].value)
			return false;
	}
	return true;
}

static void TestReadAttrsRoundTrip()
{
	AttrSet in = SampleAttrs();
	AttrSet out;
	CHECK(DecodeReadAttrsReply(EncodeReadAttrsReply(in), out));
	CHECK(AttrsEqual(in, out));

	// An empty attribute set is valid (a node with no attributes).
	AttrSet empty, emptyOut;
	CHECK(DecodeReadAttrsReply(EncodeReadAttrsReply(empty), emptyOut));
	CHECK(emptyOut.empty());
}

static void TestWriteAttrsRoundTrip()
{
	AttrSet in = SampleAttrs();
	std::string path;
	AttrSet out;
	CHECK(DecodeWriteAttrsRequest(EncodeWriteAttrsRequest("Condivisa/nota.txt", in), path, out));
	CHECK(path == "Condivisa/nota.txt");
	CHECK(AttrsEqual(in, out));
	// Type codes survive exactly: this is the M3 fidelity guarantee.
	CHECK(out.size() == 2 && out[0].type == 0x4D494D53 && out[1].type == 0x4C4F4E47);
}

// READ_ATTRS request is a bare { path }.
static void TestReadAttrsRequest()
{
	std::string p;
	CHECK(DecodePathRequest(MakeReadAttrsRequest("Condivisa/x", 1).payload, p));
	CHECK(p == "Condivisa/x");
	CHECK(MakeReadAttrsRequest("x", 1).type == MessageType::kReadAttrs);
}

// A full WRITE_ATTRS request -> Ok reply exchange over frames.
static void TestWriteAttrsExchange()
{
	FrameParser parser;
	Frame parsed;

	Frame req = MakeWriteAttrsRequest("f.txt", SampleAttrs(), 9);
	CHECK(req.type == MessageType::kWriteAttrs);
	Bytes wire = EncodeFrame(req);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	std::string p; AttrSet a;
	CHECK(DecodeWriteAttrsRequest(parsed.payload, p, a));
	CHECK(p == "f.txt" && a.size() == 2);

	Frame reply = MakeWriteAttrsReply(9);
	wire = EncodeFrame(reply);
	parser.Feed(wire.data(), wire.size());
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	CHECK(parsed.requestId == 9);
	CHECK(DecodeOk(parsed.payload));
}

static void TestRejects()
{
	// WRITE_ATTRS missing attrs.
	{ CborWriter w; w.MapHeader(1); w.Text("path"); w.Text("f");
	  std::string p; AttrSet a; CHECK(!DecodeWriteAttrsRequest(w.Take(), p, a)); }
	// WRITE_ATTRS empty path.
	{ std::string p; AttrSet a;
	  CHECK(!DecodeWriteAttrsRequest(EncodeWriteAttrsRequest("", SampleAttrs()), p, a)); }
	// READ_ATTRS reply missing attrs.
	{ CborWriter w; w.MapHeader(0);
	  AttrSet a; CHECK(!DecodeReadAttrsReply(w.Take(), a)); }
	// Trailing garbage after a READ_ATTRS reply.
	{ Bytes b = EncodeReadAttrsReply(SampleAttrs()); b.push_back(0x00);
	  AttrSet a; CHECK(!DecodeReadAttrsReply(b, a)); }
	// An attribute with an empty name is rejected by ReadAttrSet.
	{ CborWriter w; w.MapHeader(1); w.Text("attrs");
	  w.ArrayHeader(1); w.MapHeader(3);
	  w.Text("n"); w.Text(""); w.Text("t"); w.UInt(0x4C4F4E47); w.Text("v"); w.Bytes(Bytes{1});
	  AttrSet a; CHECK(!DecodeReadAttrsReply(w.Take(), a)); }
}

int main()
{
	TestReadAttrsRoundTrip();
	TestWriteAttrsRoundTrip();
	TestReadAttrsRequest();
	TestWriteAttrsExchange();
	TestRejects();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
