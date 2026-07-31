// test_frame_codec.cpp
//
// Golden encode/decode and hostile-input tests for the CNP frame codec. Pure standard
// C++, no test framework, so it builds and runs anywhere (CI without Haiku), per the
// testing strategy in docs/PROPOSAL.md section 15. Returns non-zero on any failure.

#include <cstdint>
#include <cstdio>
#include <vector>

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

static bool BytesEqual(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i] != b[i])
			return false;
	return true;
}

// Golden vector: an empty-payload HELLO with request_id 1 encodes to exactly these bytes.
static void TestGoldenEmpty()
{
	Frame f;
	f.type = MessageType::kHello;
	f.requestId = 1;

	std::vector<uint8_t> got = EncodeFrame(f);
	std::vector<uint8_t> want = {
		0x43, 0x4E,             // magic 'C','N'
		0x01,                   // version
		0x01,                   // type HELLO
		0x00, 0x00, 0x00, 0x01, // request_id = 1
		0x00, 0x00, 0x00, 0x00, // payload_len = 0
	};
	CHECK(BytesEqual(got, want));
}

// Golden vector: a READ with a two-byte payload and a non-trivial request_id.
static void TestGoldenWithPayload()
{
	Frame f;
	f.type = MessageType::kRead;
	f.requestId = 0x01020304;
	f.payload = {0xDE, 0xAD};

	std::vector<uint8_t> got = EncodeFrame(f);
	std::vector<uint8_t> want = {
		0x43, 0x4E,             // magic
		0x01,                   // version
		0x13,                   // type READ
		0x01, 0x02, 0x03, 0x04, // request_id
		0x00, 0x00, 0x00, 0x02, // payload_len = 2
		0xDE, 0xAD,             // payload
	};
	CHECK(BytesEqual(got, want));
}

// Round-trip: encode then parse yields an identical frame.
static void TestRoundTrip()
{
	Frame in;
	in.type = MessageType::kWriteAttrs;
	in.requestId = 0xCAFEBABE;
	in.payload = {1, 2, 3, 4, 5, 6, 7, 8, 9};

	std::vector<uint8_t> bytes = EncodeFrame(in);

	FrameParser parser;
	parser.Feed(bytes.data(), bytes.size());

	Frame out;
	CHECK(parser.Next(out) == ParseResult::kFrame);
	CHECK(out.version == in.version);
	CHECK(out.type == in.type);
	CHECK(out.requestId == in.requestId);
	CHECK(BytesEqual(out.payload, in.payload));
	// Nothing left over.
	CHECK(parser.Next(out) == ParseResult::kNeedMore);
	CHECK(parser.Buffered() == 0);
}

// A frame fed one byte at a time must decode correctly, returning kNeedMore until the
// last byte arrives. Exercises the incremental TCP-stream path.
static void TestByteAtATime()
{
	Frame in;
	in.type = MessageType::kList;
	in.requestId = 42;
	in.payload = {0xAA, 0xBB, 0xCC};
	std::vector<uint8_t> bytes = EncodeFrame(in);

	FrameParser parser;
	Frame out;
	for (size_t i = 0; i + 1 < bytes.size(); ++i) {
		parser.Feed(&bytes[i], 1);
		CHECK(parser.Next(out) == ParseResult::kNeedMore);
	}
	parser.Feed(&bytes[bytes.size() - 1], 1);
	CHECK(parser.Next(out) == ParseResult::kFrame);
	CHECK(out.requestId == 42);
	CHECK(BytesEqual(out.payload, in.payload));
}

// Two frames delivered in a single chunk must both be extracted, then kNeedMore.
static void TestTwoFramesOneChunk()
{
	Frame a;
	a.type = MessageType::kStat;
	a.requestId = 1;
	a.payload = {0x01};
	Frame b;
	b.type = MessageType::kClose;
	b.requestId = 2;
	// b has empty payload.

	std::vector<uint8_t> bytes;
	EncodeFrame(a, bytes);
	EncodeFrame(b, bytes);

	FrameParser parser;
	parser.Feed(bytes.data(), bytes.size());

	Frame out;
	CHECK(parser.Next(out) == ParseResult::kFrame);
	CHECK(out.requestId == 1);
	CHECK(out.type == MessageType::kStat);
	CHECK(parser.Next(out) == ParseResult::kFrame);
	CHECK(out.requestId == 2);
	CHECK(out.type == MessageType::kClose);
	CHECK(out.payload.empty());
	CHECK(parser.Next(out) == ParseResult::kNeedMore);
}

// Bad magic latches kError and stays there.
static void TestBadMagic()
{
	std::vector<uint8_t> bytes = {
		0x58, 0x58,             // wrong magic
		0x01, 0x01,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	FrameParser parser;
	parser.Feed(bytes.data(), bytes.size());
	Frame out;
	CHECK(parser.Next(out) == ParseResult::kError);
	CHECK(parser.HasError());
	// Latched: still error on subsequent pulls.
	CHECK(parser.Next(out) == ParseResult::kError);
}

// Unsupported version latches kError.
static void TestBadVersion()
{
	std::vector<uint8_t> bytes = {
		0x43, 0x4E,
		0x02,                   // version 2, unsupported
		0x01,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	FrameParser parser;
	parser.Feed(bytes.data(), bytes.size());
	Frame out;
	CHECK(parser.Next(out) == ParseResult::kError);
}

// A declared payload_len over the cap is rejected as an error, without buffering that
// many bytes. Guards against a hostile peer trying to force a huge allocation.
static void TestOversizedPayloadRejected()
{
	std::vector<uint8_t> bytes = {
		0x43, 0x4E,
		0x01,
		0x14,                   // type WRITE
		0x00, 0x00, 0x00, 0x00,
		0xFF, 0xFF, 0xFF, 0xFF, // payload_len ~4 GiB, far over the 16 MiB cap
	};
	FrameParser parser;
	parser.Feed(bytes.data(), bytes.size());
	Frame out;
	CHECK(parser.Next(out) == ParseResult::kError);
	CHECK(parser.HasError());
}

// A payload_len exactly at the cap is accepted by the header check (returns kNeedMore
// while the body is still absent, not kError). Boundary check on the guard.
static void TestPayloadAtCapIsNotError()
{
	std::vector<uint8_t> bytes = {
		0x43, 0x4E,
		0x01,
		0x14,
		0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, // payload_len = 0x01000000 = 16 MiB = kMaxPayloadLength
	};
	FrameParser parser;
	parser.Feed(bytes.data(), bytes.size());
	Frame out;
	CHECK(parser.Next(out) == ParseResult::kNeedMore);
	CHECK(!parser.HasError());
}

// A truncated frame (header says more payload than delivered) is kNeedMore, never error.
static void TestTruncatedIsNeedMore()
{
	Frame in;
	in.type = MessageType::kRead;
	in.requestId = 7;
	in.payload = {1, 2, 3, 4, 5, 6, 7, 8};
	std::vector<uint8_t> bytes = EncodeFrame(in);
	bytes.pop_back(); // drop the last payload byte

	FrameParser parser;
	parser.Feed(bytes.data(), bytes.size());
	Frame out;
	CHECK(parser.Next(out) == ParseResult::kNeedMore);
	CHECK(!parser.HasError());
}

// Encoding a payload over the cap fails cleanly and leaves the buffer untouched.
static void TestEncodeOversizeFails()
{
	Frame f;
	f.type = MessageType::kWrite;
	// Construct a payload one byte over the cap without actually filling 16 MiB of data
	// content meaningfully; resize is enough to trigger the guard.
	f.payload.resize(static_cast<size_t>(kMaxPayloadLength) + 1, 0);

	std::vector<uint8_t> out = {0xEE}; // sentinel
	CHECK(!EncodeFrame(f, out));
	// out untouched on failure.
	CHECK(out.size() == 1 && out[0] == 0xEE);
}

int main()
{
	TestGoldenEmpty();
	TestGoldenWithPayload();
	TestRoundTrip();
	TestByteAtATime();
	TestTwoFramesOneChunk();
	TestBadMagic();
	TestBadVersion();
	TestOversizedPayloadRejected();
	TestPayloadAtCapIsNotError();
	TestTruncatedIsNeedMore();
	TestEncodeOversizeFails();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
