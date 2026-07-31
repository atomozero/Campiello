// MdnsWire.h
//
// Minimal mDNS / DNS-SD wire codec for Bricola (discovery). Deliberately a subset of
// RFC 1035 / 6762 / 6763: exactly the message shapes Campiello advertises and browses for
// the single service type `_campiello._tcp`, nothing general-purpose.
//
// It does two jobs:
//   - DECODE inbound packets: parse the DNS header, questions, and resource records, then
//     pull typed views out of A / PTR / SRV / TXT records (the record set DNS-SD uses).
//   - ENCODE outbound packets: build a browser PTR query and a responder answer carrying
//     our PTR / SRV / TXT / A records. Names in encoded RDATA are written uncompressed
//     (compression is optional per RFC 1035 4.1.4; skipping it keeps the encoder simple and
//     correct for one small service).
//
// Untrusted-input posture (docs/PROPOSAL.md working agreement rule 7): the reader never
// reads past the buffer; a declared RDATA length beyond the remaining bytes is an error and
// never causes an oversized allocation; name decoding caps compression-pointer depth so a
// hostile packet cannot loop or overflow the stack.
//
// Pure standard C++ (no Haiku, no BeAPI), unit-testable off Haiku per PROPOSAL.md section
// 15. The name/record decode is lifted from the proven querier in
// LANterna/src/enrich/MdnsEnricher.cpp (see docs/REUSE.md); TXT parsing and the entire
// encode/responder side are new here.

#ifndef CAMPIELLO_BRICOLA_MDNS_MDNSWIRE_H
#define CAMPIELLO_BRICOLA_MDNS_MDNSWIRE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace campiello {
namespace bricola {
namespace mdns {

// Resource-record types we handle. Others parse into a raw Record and are ignored.
enum RrType : uint16_t {
	kTypeA   = 1,
	kTypePTR = 12,
	kTypeTXT = 16,
	kTypeSRV = 33,
};

static const uint16_t kClassIN = 1;
// Top bit of the record CLASS field: "cache-flush" on responses, "unicast-response
// requested" on questions (RFC 6762 sections 10.2, 5.4). Mask it off to read the real class.
static const uint16_t kClassFlushBit = 0x8000;

// Header flags for the packets we emit.
static const uint16_t kFlagsQuery    = 0x0000;         // QR=0
static const uint16_t kFlagsResponse = 0x8400;         // QR=1, AA=1 (authoritative answer)

// One question in the query section.
struct Question {
	std::string name;
	uint16_t    qtype  = 0;
	uint16_t    qclass = 0;   // includes the unicast-response top bit as received
};

// One resource record, as parsed. `rdata` is the raw RDATA bytes; `rdataOffset` is where
// those bytes begin within the whole message, needed to follow compression pointers that
// PTR / SRV targets may use back into the packet.
struct Record {
	std::string name;
	uint16_t    type        = 0;
	uint16_t    rrclass     = 0;   // includes the cache-flush top bit as received
	uint32_t    ttl         = 0;
	size_t      rdataOffset = 0;
	std::string rdata;
};

// A whole decoded message. Authority records are parsed but rarely used by Bricola.
struct Message {
	uint16_t              id    = 0;
	uint16_t              flags = 0;
	std::vector<Question> questions;
	std::vector<Record>   answers;
	std::vector<Record>   authorities;
	std::vector<Record>   additionals;
};

// True if the flags mark this packet a response (QR bit set).
inline bool IsResponse(uint16_t flags) { return (flags & 0x8000) != 0; }

// ── Name codec ─────────────────────────────────────────────────────────────
// Encode "Studio._campiello._tcp.local" into length-prefixed labels + a zero root, appended
// to `out`. Labels longer than 63 bytes are skipped (malformed input, never emitted by us).
void EncodeName(const std::string& name, std::string& out);

// Decode a DNS name starting at `offset` in `buf`. `out` is cleared, then filled with the
// dotted name. Returns the number of bytes consumed AT `offset` (a compression pointer is 2
// bytes even though the name continues elsewhere), or 0 on any malformation. Follows
// compression pointers with a depth cap.
size_t DecodeName(const uint8_t* buf, size_t len, size_t offset, std::string& out);

// ── Decode ─────────────────────────────────────────────────────────────────
// Parse a full message. Returns false if the header or any name/record runs past the buffer.
bool Parse(const uint8_t* buf, size_t len, Message& out);

// Typed views over a parsed record. Each returns false if the record is the wrong type or
// its RDATA is malformed. A / TXT are self-contained; PTR / SRV need the whole message to
// resolve compression pointers, so they take (buf, len).
bool DecodeA(const Record& rec, std::string& ipv4Out);   // dotted quad, e.g. "192.168.1.7"
bool DecodePtr(const uint8_t* buf, size_t len, const Record& rec, std::string& targetOut);
bool DecodeSrv(const uint8_t* buf, size_t len, const Record& rec,
	uint16_t& priority, uint16_t& weight, uint16_t& port, std::string& targetOut);
// TXT decodes to key/value pairs. A character-string "key=value" splits at the first '=';
// one with no '=' yields {key, ""} (a valueless/boolean attribute per RFC 6763 6.4).
bool DecodeTxt(const Record& rec, std::vector<std::pair<std::string, std::string>>& out);

// ── Encode ─────────────────────────────────────────────────────────────────
// A record to serialize into an outbound packet. `rdata` is the raw RDATA; the Make* helpers
// below build it for each type. Set `cacheFlush` on responder records so peers replace any
// stale copy (RFC 6762 10.2). TTL 0 makes it a goodbye (announce departure).
struct OutRecord {
	std::string name;
	uint16_t    type       = 0;
	uint32_t    ttl        = 0;
	bool        cacheFlush = false;
	std::string rdata;
};

// Build the RDATA bytes for each record type.
std::string MakeA(const std::string& ipv4);                                  // "" on bad input
std::string MakeSrv(uint16_t priority, uint16_t weight, uint16_t port,
	const std::string& target);
std::string MakePtr(const std::string& target);
std::string MakeTxt(const std::vector<std::pair<std::string, std::string>>& kv);

// Build a browser PTR query for `serviceName` (e.g. "_campiello._tcp.local").
std::string BuildQuery(const std::string& serviceName);

// Build a responder answer packet carrying `answers` (plus optional `additionals`, the usual
// place for the SRV/TXT/A that accompany a PTR answer).
std::string BuildResponse(const std::vector<OutRecord>& answers,
	const std::vector<OutRecord>& additionals);

} // namespace mdns
} // namespace bricola
} // namespace campiello

#endif // CAMPIELLO_BRICOLA_MDNS_MDNSWIRE_H
