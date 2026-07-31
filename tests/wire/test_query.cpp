// test_query.cpp
//
// Round-trip and hostile-input tests for the M4 QUERY messages (QUERY_OPEN, QUERY_RESULT,
// QUERY_UPDATE, QUERY_CLOSE). Pure standard C++, no framework; non-zero exit on failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/Listing.h"
#include "../../src/traghetto/wire/Query.h"

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

static Entry MakeEntry(const std::string& name, uint64_t size)
{
	Entry e;
	e.name = name;
	e.stat.mode = 0100644;
	e.stat.size = size;
	e.stat.mtime = 1700000000;
	e.attrs = { Attr{"BEOS:TYPE", 0x4D494D53, Bytes{'t', 'x', 't'}} };
	return e;
}

static bool SameEntry(const Entry& a, const Entry& b)
{
	return a.name == b.name && a.stat.size == b.stat.size && a.stat.mode == b.stat.mode
		&& a.attrs.size() == b.attrs.size();
}

int main()
{
	// QUERY_OPEN round-trip.
	{
		Bytes p = EncodeQueryOpenRequest(42, "name==*.txt && size>1024");
		uint64_t id = 0; std::string q;
		CHECK(DecodeQueryOpenRequest(p, id, q));
		CHECK(id == 42);
		CHECK(q == "name==*.txt && size>1024");
		// Frame builder carries the type and requestId.
		Frame f = MakeQueryOpenRequest(42, "x", 7);
		CHECK(f.type == MessageType::kQueryOpen);
		CHECK(f.requestId == 7);
	}

	// QUERY_RESULT round-trip with a batch of entries and the done flag.
	{
		std::vector<Entry> entries = {MakeEntry("a.txt", 10), MakeEntry("b.txt", 2048)};
		Bytes p = EncodeQueryResultReply(99, entries, true);
		uint64_t id = 0; std::vector<Entry> out; bool done = false;
		CHECK(DecodeQueryResultReply(p, id, out, done));
		CHECK(id == 99);
		CHECK(done == true);
		CHECK(out.size() == 2);
		CHECK(SameEntry(out[0], entries[0]));
		CHECK(SameEntry(out[1], entries[1]));

		// An empty, not-done batch is valid (a streamed intermediate/keepalive).
		Bytes p2 = EncodeQueryResultReply(99, {}, false);
		std::vector<Entry> out2; bool done2 = true; uint64_t id2 = 0;
		CHECK(DecodeQueryResultReply(p2, id2, out2, done2));
		CHECK(out2.empty() && done2 == false && id2 == 99);
	}

	// QUERY_UPDATE round-trip (added and removed).
	{
		Entry e = MakeEntry("new.txt", 5);
		Bytes p = EncodeQueryUpdate(7, true, e);
		uint64_t id = 0; bool added = false; Entry out;
		CHECK(DecodeQueryUpdate(p, id, added, out));
		CHECK(id == 7 && added == true && SameEntry(out, e));

		Bytes pr = EncodeQueryUpdate(7, false, e);
		bool added2 = true;
		CHECK(DecodeQueryUpdate(pr, id, added2, out));
		CHECK(added2 == false);
	}

	// QUERY_CLOSE round-trip + the Ok reply builder.
	{
		Bytes p = EncodeQueryCloseRequest(123);
		uint64_t id = 0;
		CHECK(DecodeQueryCloseRequest(p, id));
		CHECK(id == 123);
		Frame f = MakeQueryCloseReply(5);
		CHECK(f.type == MessageType::kQueryClose);
		CHECK(f.requestId == 5);
	}

	// Hostile / malformed inputs are rejected, not crashed on.
	{
		uint64_t id; std::string q;
		CHECK(!DecodeQueryOpenRequest(Bytes{}, id, q));                 // empty
		CHECK(!DecodeQueryOpenRequest(Bytes{0xA0}, id, q));             // empty map, missing keys
		std::vector<Entry> es; bool done; uint64_t rid;
		CHECK(!DecodeQueryResultReply(Bytes{0xA0}, rid, es, done));     // missing keys
		// Truncated payload (a valid prefix cut short).
		Bytes good = EncodeQueryOpenRequest(1, "abc");
		Bytes cut(good.begin(), good.begin() + good.size() / 2);
		CHECK(!DecodeQueryOpenRequest(cut, id, q));
		// An over-long query string is refused.
		std::string big(kMaxQueryBytes + 1, 'x');
		Bytes p = EncodeQueryOpenRequest(1, big);
		CHECK(!DecodeQueryOpenRequest(p, id, q));
	}

	std::printf("%s: %d checks, %d failures\n",
		gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
