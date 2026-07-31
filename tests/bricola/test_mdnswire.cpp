// test_mdnswire.cpp
//
// Tests for the Bricola mDNS wire codec. Names and TXT are checked against hand-computed
// golden byte vectors; parsing is checked by round-trip (build a response, parse it back)
// and by hostile inputs (truncation, compression-pointer loops, oversized lengths). Pure
// standard C++, no framework; returns non-zero on any failure.

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "../../src/bricola/mdns/MdnsWire.h"

using namespace campiello::bricola::mdns;

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

static Bytes AsBytes(const std::string& s)
{
	return Bytes(s.begin(), s.end());
}

static bool Eq(const Bytes& a, const Bytes& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i] != b[i])
			return false;
	return true;
}

// ── Name encoding golden vectors ────────────────────────────────────────────

static void TestEncodeNameGolden()
{
	// "local" -> 0x05 'l' 'o' 'c' 'a' 'l' 0x00
	std::string out;
	EncodeName("local", out);
	CHECK(Eq(AsBytes(out), {0x05, 'l', 'o', 'c', 'a', 'l', 0x00}));

	// "_campiello._tcp.local"
	out.clear();
	EncodeName("_campiello._tcp.local", out);
	CHECK(Eq(AsBytes(out),
		{0x0a, '_', 'c', 'a', 'm', 'p', 'i', 'e', 'l', 'l', 'o',
		 0x04, '_', 't', 'c', 'p',
		 0x05, 'l', 'o', 'c', 'a', 'l', 0x00}));

	// Empty name is just the root label.
	out.clear();
	EncodeName("", out);
	CHECK(Eq(AsBytes(out), {0x00}));
}

// ── Name decode + round-trip ────────────────────────────────────────────────

static void TestNameRoundTrip()
{
	const char* names[] = {
		"local", "_campiello._tcp.local", "Studio._campiello._tcp.local", "host-1.local"};
	for (const char* n : names) {
		std::string enc;
		EncodeName(n, enc);
		std::string dec;
		size_t consumed = DecodeName(
			reinterpret_cast<const uint8_t*>(enc.data()), enc.size(), 0, dec);
		CHECK(consumed == enc.size());
		CHECK(dec == n);
	}
}

static void TestDecodeCompressionPointer()
{
	// Message layout: at offset 12, "_tcp.local" ; at offset 24, "Studio" + pointer->12.
	std::string msg(12, '\0');   // dummy header
	CHECK(msg.size() == 12);
	// offset 12: "_tcp" "local" root
	std::string tail;
	EncodeName("_tcp.local", tail);
	msg += tail;                 // occupies offsets 12..(12+tail.size()-1)
	size_t studioOff = msg.size();
	// "Studio" label then a pointer to offset 12.
	msg += static_cast<char>(6);
	msg += "Studio";
	msg += static_cast<char>(0xC0);
	msg += static_cast<char>(12);

	std::string dec;
	size_t consumed = DecodeName(
		reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), studioOff, dec);
	CHECK(dec == "Studio._tcp.local");
	// Consumed at studioOff: 1+6 (label) + 2 (pointer) = 9, NOT the pointed-to bytes.
	CHECK(consumed == 9);
}

static void TestDecodeHostileNames()
{
	// Pointer loop: offset 0 points to itself.
	{
		uint8_t buf[] = {0xC0, 0x00};
		std::string out;
		size_t c = DecodeName(buf, sizeof(buf), 0, out);
		CHECK(c == 0 || out.empty());   // must not hang; error or empty is acceptable
	}
	// Truncated label: length byte says 5 but only 2 bytes remain.
	{
		uint8_t buf[] = {0x05, 'a', 'b'};
		std::string out;
		CHECK(DecodeName(buf, sizeof(buf), 0, out) == 0);
	}
	// Offset past the end.
	{
		uint8_t buf[] = {0x00};
		std::string out;
		CHECK(DecodeName(buf, sizeof(buf), 5, out) == 0);
	}
}

// ── TXT golden + round-trip ─────────────────────────────────────────────────

static void TestTxtGolden()
{
	std::vector<std::pair<std::string, std::string>> kv = {{"v", "1"}, {"bfs", "1"}};
	std::string rdata = MakeTxt(kv);
	// "v=1" -> 0x03 'v' '=' '1' ; "bfs=1" -> 0x05 'b' 'f' 's' '=' '1'
	CHECK(Eq(AsBytes(rdata),
		{0x03, 'v', '=', '1', 0x05, 'b', 'f', 's', '=', '1'}));

	// Empty attribute set is a single zero-length string.
	CHECK(Eq(AsBytes(MakeTxt({})), {0x00}));

	// Valueless key stays bare (no '=').
	CHECK(Eq(AsBytes(MakeTxt({{"flag", ""}})), {0x04, 'f', 'l', 'a', 'g'}));
}

static void TestTxtRoundTrip()
{
	std::vector<std::pair<std::string, std::string>> kv = {
		{"v", "1"}, {"node", "Studio"}, {"port", "7735"}, {"bfs", "1"}, {"flag", ""}};
	Record rec;
	rec.type = kTypeTXT;
	rec.rdata = MakeTxt(kv);
	std::vector<std::pair<std::string, std::string>> got;
	CHECK(DecodeTxt(rec, got));
	CHECK(got.size() == kv.size());
	for (size_t i = 0; i < kv.size() && i < got.size(); ++i) {
		CHECK(got[i].first == kv[i].first);
		CHECK(got[i].second == kv[i].second);
	}

	// A value containing '=' must split only at the first one.
	Record eqrec;
	eqrec.type = kTypeTXT;
	eqrec.rdata = MakeTxt({{"k", "a=b"}});
	std::vector<std::pair<std::string, std::string>> eqgot;
	CHECK(DecodeTxt(eqrec, eqgot));
	CHECK(eqgot.size() == 1);
	CHECK(eqgot[0].first == "k" && eqgot[0].second == "a=b");
}

static void TestTxtHostile()
{
	// Character-string length runs past the RDATA end.
	Record rec;
	rec.type = kTypeTXT;
	rec.rdata = std::string("\x05""ab", 3);   // says 5, only 2 follow
	std::vector<std::pair<std::string, std::string>> got;
	CHECK(!DecodeTxt(rec, got));
}

// ── A / SRV RDATA ───────────────────────────────────────────────────────────

static void TestMakeA()
{
	CHECK(Eq(AsBytes(MakeA("192.168.1.7")), {192, 168, 1, 7}));
	CHECK(Eq(AsBytes(MakeA("0.0.0.0")), {0, 0, 0, 0}));
	CHECK(MakeA("256.0.0.1").empty());       // octet out of range
	CHECK(MakeA("1.2.3").empty());           // too few octets
	CHECK(MakeA("1.2.3.4.5").empty());       // too many
	CHECK(MakeA("1.2.3.4x").empty());        // trailing garbage
	CHECK(MakeA("").empty());
}

// ── Full response build + parse round-trip ──────────────────────────────────

static void TestResponseRoundTrip()
{
	const std::string service = "_campiello._tcp.local";
	const std::string instance = "Studio._campiello._tcp.local";
	const std::string host = "studio.local";

	std::vector<OutRecord> answers;
	OutRecord ptr;
	ptr.name = service;
	ptr.type = kTypePTR;
	ptr.ttl = 4500;
	ptr.rdata = MakePtr(instance);
	answers.push_back(ptr);

	std::vector<OutRecord> additionals;
	OutRecord srv;
	srv.name = instance;
	srv.type = kTypeSRV;
	srv.ttl = 120;
	srv.cacheFlush = true;
	srv.rdata = MakeSrv(0, 0, 7735, host);
	additionals.push_back(srv);

	OutRecord txt;
	txt.name = instance;
	txt.type = kTypeTXT;
	txt.ttl = 4500;
	txt.rdata = MakeTxt({{"v", "1"}, {"node", "Studio"}, {"port", "7735"}, {"bfs", "1"}});
	additionals.push_back(txt);

	OutRecord a;
	a.name = host;
	a.type = kTypeA;
	a.ttl = 120;
	a.cacheFlush = true;
	a.rdata = MakeA("192.168.1.7");
	additionals.push_back(a);

	std::string packet = BuildResponse(answers, additionals);

	Message msg;
	CHECK(Parse(reinterpret_cast<const uint8_t*>(packet.data()), packet.size(), msg));
	CHECK(IsResponse(msg.flags));
	CHECK(msg.answers.size() == 1);
	CHECK(msg.additionals.size() == 3);

	// PTR answer resolves back to the instance.
	std::string target;
	CHECK(DecodePtr(reinterpret_cast<const uint8_t*>(packet.data()), packet.size(),
		msg.answers[0], target));
	CHECK(target == instance);

	// SRV -> port + host.
	uint16_t prio = 0, weight = 0, port = 0;
	std::string srvHost;
	CHECK(DecodeSrv(reinterpret_cast<const uint8_t*>(packet.data()), packet.size(),
		msg.additionals[0], prio, weight, port, srvHost));
	CHECK(port == 7735);
	CHECK(srvHost == host);

	// TXT -> keys.
	std::vector<std::pair<std::string, std::string>> kv;
	CHECK(DecodeTxt(msg.additionals[1], kv));
	CHECK(kv.size() == 4);
	CHECK(kv[0].first == "v" && kv[0].second == "1");
	CHECK(kv[2].first == "port" && kv[2].second == "7735");

	// A -> dotted quad, and cache-flush bit is set on the wire but masked out of the class.
	std::string ip;
	CHECK(DecodeA(msg.additionals[2], ip));
	CHECK(ip == "192.168.1.7");
	CHECK((msg.additionals[2].rrclass & kClassFlushBit) != 0);
	CHECK((msg.additionals[2].rrclass & ~kClassFlushBit) == kClassIN);
}

static void TestQueryRoundTrip()
{
	std::string q = BuildQuery("_campiello._tcp.local");
	Message msg;
	CHECK(Parse(reinterpret_cast<const uint8_t*>(q.data()), q.size(), msg));
	CHECK(!IsResponse(msg.flags));
	CHECK(msg.questions.size() == 1);
	CHECK(msg.questions[0].name == "_campiello._tcp.local");
	CHECK(msg.questions[0].qtype == kTypePTR);
	CHECK((msg.questions[0].qclass & 0x7FFF) == kClassIN);
}

// ── Parse-level hostile inputs ──────────────────────────────────────────────

static void TestParseHostile()
{
	Message msg;
	// Too short for a header.
	{
		uint8_t buf[] = {0, 0, 0};
		CHECK(!Parse(buf, sizeof(buf), msg));
	}
	// Header claims 1 answer but the body is empty.
	{
		uint8_t buf[12] = {0};
		buf[7] = 1;   // ancount = 1
		CHECK(!Parse(buf, sizeof(buf), msg));
	}
	// RDATA length overruns the buffer.
	{
		// header (ancount=1) + name "a.local" + type A + class IN + ttl + rdlen=255
		std::string p(12, '\0');
		p[7] = 1;
		std::string name;
		EncodeName("a.local", name);
		p += name;
		p += std::string("\x00\x01", 2);   // type A
		p += std::string("\x00\x01", 2);   // class IN
		p += std::string(4, '\0');          // ttl
		p += std::string("\x00\xFF", 2);   // rdlen 255, but nothing follows
		CHECK(!Parse(reinterpret_cast<const uint8_t*>(p.data()), p.size(), msg));
	}
	// Null buffer.
	CHECK(!Parse(nullptr, 0, msg));
}

int main()
{
	TestEncodeNameGolden();
	TestNameRoundTrip();
	TestDecodeCompressionPointer();
	TestDecodeHostileNames();
	TestTxtGolden();
	TestTxtRoundTrip();
	TestTxtHostile();
	TestMakeA();
	TestResponseRoundTrip();
	TestQueryRoundTrip();
	TestParseHostile();

	std::printf("mdnswire: %d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
