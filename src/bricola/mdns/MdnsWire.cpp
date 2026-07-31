// MdnsWire.cpp
//
// See MdnsWire.h. The name/record decode is lifted, with edits, from the querier in
// LANterna/src/enrich/MdnsEnricher.cpp; TXT parsing and the whole encode side are new.

#include "MdnsWire.h"

#include <cstdio>
#include <cstring>

namespace campiello {
namespace bricola {
namespace mdns {

namespace {

// Cap on compression-pointer indirection while decoding a name (RFC 1035 4.1.4). A hostile
// packet can chain pointers; this bounds recursion so it cannot loop or blow the stack.
const int kMaxNameDepth = 20;

uint16_t ReadBE16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

uint32_t ReadBE32(const uint8_t* p)
{
	return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
		| (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

void AppendBE16(std::string& out, uint16_t v)
{
	out += static_cast<char>((v >> 8) & 0xFF);
	out += static_cast<char>(v & 0xFF);
}

void AppendBE32(std::string& out, uint32_t v)
{
	out += static_cast<char>((v >> 24) & 0xFF);
	out += static_cast<char>((v >> 16) & 0xFF);
	out += static_cast<char>((v >> 8) & 0xFF);
	out += static_cast<char>(v & 0xFF);
}

// Recursive worker: appends labels to `out` (does not clear it), so a pointer can continue a
// name already partly read. Returns bytes consumed at `offset` (0 on error).
size_t DecodeNameInto(const uint8_t* buf, size_t len, size_t offset, std::string& out,
	int depth)
{
	if (depth > kMaxNameDepth || offset >= len)
		return 0;

	size_t start = offset;
	bool jumped = false;
	size_t consumed = 0;

	while (offset < len) {
		uint8_t labelLen = buf[offset];
		if (labelLen == 0) {
			++offset;
			if (!jumped)
				consumed = offset - start;
			break;
		}
		if ((labelLen & 0xC0) == 0xC0) {
			// Compression pointer: 14-bit offset into the message.
			if (offset + 1 >= len)
				return 0;
			size_t ptr = ((labelLen & 0x3F) << 8) | buf[offset + 1];
			if (!jumped)
				consumed = offset + 2 - start;
			if (DecodeNameInto(buf, len, ptr, out, depth + 1) == 0)
				return 0;
			jumped = true;
			break;
		}
		if ((labelLen & 0xC0) != 0)
			return 0;   // reserved label-length bits, malformed
		++offset;
		if (offset + labelLen > len)
			return 0;
		if (!out.empty())
			out += '.';
		out.append(reinterpret_cast<const char*>(buf + offset), labelLen);
		offset += labelLen;
	}
	return consumed;
}

// Parse the qd/an/ns/ar record vectors. Advances `offset`; false on any overrun.
bool ParseRecords(const uint8_t* buf, size_t len, size_t& offset, int count,
	std::vector<Record>& out)
{
	for (int i = 0; i < count; ++i) {
		Record rec;
		size_t c = DecodeName(buf, len, offset, rec.name);
		if (c == 0)
			return false;
		offset += c;
		if (offset + 10 > len)
			return false;
		rec.type = ReadBE16(buf + offset);
		rec.rrclass = ReadBE16(buf + offset + 2);
		rec.ttl = ReadBE32(buf + offset + 4);
		uint16_t rdlen = ReadBE16(buf + offset + 8);
		offset += 10;
		if (offset + rdlen > len)
			return false;
		rec.rdataOffset = offset;
		rec.rdata.assign(reinterpret_cast<const char*>(buf + offset), rdlen);
		offset += rdlen;
		out.push_back(std::move(rec));
	}
	return true;
}

} // namespace

// ── Name codec ─────────────────────────────────────────────────────────────

void EncodeName(const std::string& name, std::string& out)
{
	size_t start = 0;
	while (start < name.size()) {
		size_t dot = name.find('.', start);
		if (dot == std::string::npos)
			dot = name.size();
		size_t labelLen = dot - start;
		if (labelLen > 0 && labelLen < 64) {
			out += static_cast<char>(labelLen);
			out.append(name, start, labelLen);
		}
		start = dot + 1;
	}
	out += '\0';
}

size_t DecodeName(const uint8_t* buf, size_t len, size_t offset, std::string& out)
{
	out.clear();
	return DecodeNameInto(buf, len, offset, out, 0);
}

// ── Decode ─────────────────────────────────────────────────────────────────

bool Parse(const uint8_t* buf, size_t len, Message& out)
{
	if (buf == nullptr || len < 12)
		return false;

	out = Message{};
	out.id = ReadBE16(buf);
	out.flags = ReadBE16(buf + 2);
	uint16_t qd = ReadBE16(buf + 4);
	uint16_t an = ReadBE16(buf + 6);
	uint16_t ns = ReadBE16(buf + 8);
	uint16_t ar = ReadBE16(buf + 10);

	size_t offset = 12;

	for (int i = 0; i < qd; ++i) {
		Question q;
		size_t c = DecodeName(buf, len, offset, q.name);
		if (c == 0)
			return false;
		offset += c;
		if (offset + 4 > len)
			return false;
		q.qtype = ReadBE16(buf + offset);
		q.qclass = ReadBE16(buf + offset + 2);
		offset += 4;
		out.questions.push_back(std::move(q));
	}

	if (!ParseRecords(buf, len, offset, an, out.answers))
		return false;
	if (!ParseRecords(buf, len, offset, ns, out.authorities))
		return false;
	if (!ParseRecords(buf, len, offset, ar, out.additionals))
		return false;
	return true;
}

bool DecodeA(const Record& rec, std::string& ipv4Out)
{
	if (rec.type != kTypeA || rec.rdata.size() != 4)
		return false;
	const uint8_t* p = reinterpret_cast<const uint8_t*>(rec.rdata.data());
	char tmp[16];
	std::snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
	ipv4Out.assign(tmp);
	return true;
}

bool DecodePtr(const uint8_t* buf, size_t len, const Record& rec, std::string& targetOut)
{
	if (rec.type != kTypePTR)
		return false;
	return DecodeName(buf, len, rec.rdataOffset, targetOut) != 0;
}

bool DecodeSrv(const uint8_t* buf, size_t len, const Record& rec,
	uint16_t& priority, uint16_t& weight, uint16_t& port, std::string& targetOut)
{
	if (rec.type != kTypeSRV || rec.rdata.size() < 7)
		return false;
	const uint8_t* p = reinterpret_cast<const uint8_t*>(rec.rdata.data());
	priority = ReadBE16(p);
	weight = ReadBE16(p + 2);
	port = ReadBE16(p + 4);
	// The target name lives 6 bytes into RDATA and may use compression back into the packet.
	return DecodeName(buf, len, rec.rdataOffset + 6, targetOut) != 0;
}

bool DecodeTxt(const Record& rec, std::vector<std::pair<std::string, std::string>>& out)
{
	if (rec.type != kTypeTXT)
		return false;
	out.clear();
	const std::string& r = rec.rdata;
	// A single empty character-string (one 0x00 byte) is a valid, attribute-less TXT.
	size_t i = 0;
	while (i < r.size()) {
		uint8_t slen = static_cast<uint8_t>(r[i]);
		++i;
		if (i + slen > r.size())
			return false;
		std::string entry = r.substr(i, slen);
		i += slen;
		if (entry.empty())
			continue;   // padding character-string, skip
		size_t eq = entry.find('=');
		if (eq == std::string::npos)
			out.emplace_back(entry, std::string());
		else
			out.emplace_back(entry.substr(0, eq), entry.substr(eq + 1));
	}
	return true;
}

// ── Encode ─────────────────────────────────────────────────────────────────

std::string MakeA(const std::string& ipv4)
{
	unsigned a = 0, b = 0, c = 0, d = 0;
	char extra = 0;
	// %c catches a trailing character (e.g. "1.2.3.4.5" or "1.2.3.4x"), rejecting garbage.
	if (std::sscanf(ipv4.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4)
		return std::string();
	if (a > 255 || b > 255 || c > 255 || d > 255)
		return std::string();
	std::string out;
	out += static_cast<char>(a);
	out += static_cast<char>(b);
	out += static_cast<char>(c);
	out += static_cast<char>(d);
	return out;
}

std::string MakeSrv(uint16_t priority, uint16_t weight, uint16_t port,
	const std::string& target)
{
	std::string out;
	AppendBE16(out, priority);
	AppendBE16(out, weight);
	AppendBE16(out, port);
	EncodeName(target, out);
	return out;
}

std::string MakePtr(const std::string& target)
{
	std::string out;
	EncodeName(target, out);
	return out;
}

std::string MakeTxt(const std::vector<std::pair<std::string, std::string>>& kv)
{
	std::string out;
	for (const auto& pair : kv) {
		std::string entry = pair.first;
		if (!pair.second.empty())
			entry += "=" + pair.second;
		// A character-string is length-prefixed by a single byte, so max 255 bytes.
		if (entry.size() > 255)
			entry.resize(255);
		out += static_cast<char>(entry.size());
		out += entry;
	}
	// RFC 6763 6.1: an empty TXT is represented by a single zero-length character-string.
	if (out.empty())
		out += '\0';
	return out;
}

std::string BuildQuery(const std::string& serviceName)
{
	std::string out;
	AppendBE16(out, 0);              // ID 0 (mDNS ignores it)
	AppendBE16(out, kFlagsQuery);
	AppendBE16(out, 1);              // qdcount
	AppendBE16(out, 0);              // ancount
	AppendBE16(out, 0);              // nscount
	AppendBE16(out, 0);              // arcount
	EncodeName(serviceName, out);
	AppendBE16(out, kTypePTR);
	AppendBE16(out, kClassIN);
	return out;
}

namespace {

void AppendRecord(std::string& out, const OutRecord& rec)
{
	EncodeName(rec.name, out);
	AppendBE16(out, rec.type);
	uint16_t klass = kClassIN;
	if (rec.cacheFlush)
		klass |= kClassFlushBit;
	AppendBE16(out, klass);
	AppendBE32(out, rec.ttl);
	AppendBE16(out, static_cast<uint16_t>(rec.rdata.size()));
	out += rec.rdata;
}

} // namespace

std::string BuildResponse(const std::vector<OutRecord>& answers,
	const std::vector<OutRecord>& additionals)
{
	std::string out;
	AppendBE16(out, 0);
	AppendBE16(out, kFlagsResponse);
	AppendBE16(out, 0);   // qdcount
	AppendBE16(out, static_cast<uint16_t>(answers.size()));
	AppendBE16(out, 0);   // nscount
	AppendBE16(out, static_cast<uint16_t>(additionals.size()));
	for (const auto& rec : answers)
		AppendRecord(out, rec);
	for (const auto& rec : additionals)
		AppendRecord(out, rec);
	return out;
}

} // namespace mdns
} // namespace bricola
} // namespace campiello
