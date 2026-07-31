// test_error.cpp
//
// Tests for the ERROR reply: a byte-exact golden, round-trips with and without a message,
// tolerance of unknown codes, a full frame round-trip, and hostile inputs. Pure standard
// C++, no framework; non-zero exit on failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/traghetto/wire/Cbor.h"
#include "../../src/traghetto/wire/Error.h"
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

// Golden: a code-only ERROR is a single-entry map { "code": 2 }.
static void TestGoldenCodeOnly()
{
	Bytes got = EncodeError(ErrorCode::kNotFound);
	Bytes want = {
		0xa1,                        // map(1)
		0x64, 'c', 'o', 'd', 'e',    // "code"
		0x02                         // 2
	};
	CHECK(Eq(got, want));
}

// Golden: with a message, canonical order puts msg before code.
static void TestGoldenWithMessage()
{
	Bytes got = EncodeError(ErrorCode::kAccessDenied, "no");
	Bytes want = {
		0xa2,                        // map(2)
		0x63, 'm', 's', 'g',         // "msg"
		0x62, 'n', 'o',              // "no"
		0x64, 'c', 'o', 'd', 'e',    // "code"
		0x03                         // 3
	};
	CHECK(Eq(got, want));
}

static void TestRoundTrip()
{
	{ ErrorReply e; CHECK(DecodeError(EncodeError(ErrorCode::kInternal), e));
	  CHECK(e.code == 1); CHECK(e.message.empty()); }
	{ ErrorReply e; CHECK(DecodeError(EncodeError(ErrorCode::kBadHandle, "closed"), e));
	  CHECK(e.code == 10); CHECK(e.message == "closed"); }
}

// An unknown code is tolerated (forward compatibility).
static void TestUnknownCodeTolerated()
{
	CborWriter w;
	w.MapHeader(1);
	w.Text("code"); w.UInt(9999);
	ErrorReply e;
	CHECK(DecodeError(w.Take(), e));
	CHECK(e.code == 9999);
}

// Unknown key is skipped.
static void TestUnknownKeyIgnored()
{
	CborWriter w;
	w.MapHeader(2);
	w.Text("code"); w.UInt(2);
	w.Text("detail"); w.ArrayHeader(0);
	ErrorReply e;
	CHECK(DecodeError(w.Take(), e));
	CHECK(e.code == 2);
}

static void TestFrameRoundTrip()
{
	Frame f = MakeError(ErrorCode::kNotFound, "nota.txt", 0x0BADF00D);
	CHECK(f.type == MessageType::kError);
	CHECK(f.requestId == 0x0BADF00D);

	Bytes onWire = EncodeFrame(f);
	FrameParser parser;
	parser.Feed(onWire.data(), onWire.size());
	Frame parsed;
	CHECK(parser.Next(parsed) == ParseResult::kFrame);
	CHECK(parsed.type == MessageType::kError);
	CHECK(parsed.requestId == 0x0BADF00D);

	ErrorReply e;
	CHECK(DecodeError(parsed.payload, e));
	CHECK(e.code == static_cast<uint32_t>(ErrorCode::kNotFound));
	CHECK(e.message == "nota.txt");
}

static void TestRejects()
{
	// Missing mandatory code.
	{ CborWriter w; w.MapHeader(1); w.Text("msg"); w.Text("x");
	  ErrorReply e; CHECK(!DecodeError(w.Take(), e)); }
	// Duplicate code.
	{ CborWriter w; w.MapHeader(2); w.Text("code"); w.UInt(1); w.Text("code"); w.UInt(2);
	  ErrorReply e; CHECK(!DecodeError(w.Take(), e)); }
	// Message too long.
	{ CborWriter w; w.MapHeader(2); w.Text("msg");
	  w.Text(std::string(kMaxErrorMessageBytes + 1, 'x')); w.Text("code"); w.UInt(1);
	  ErrorReply e; CHECK(!DecodeError(w.Take(), e)); }
	// Trailing garbage.
	{ Bytes b = EncodeError(ErrorCode::kInternal); b.push_back(0x00);
	  ErrorReply e; CHECK(!DecodeError(b, e)); }
	// Not a map.
	{ CborWriter w; w.UInt(2); ErrorReply e; CHECK(!DecodeError(w.Take(), e)); }
	// Empty payload.
	{ Bytes b; ErrorReply e; CHECK(!DecodeError(b, e)); }
}

int main()
{
	TestGoldenCodeOnly();
	TestGoldenWithMessage();
	TestRoundTrip();
	TestUnknownCodeTolerated();
	TestUnknownKeyIgnored();
	TestFrameRoundTrip();
	TestRejects();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
