// Fingerprint.cpp
//
// Hex encode/decode for a Fingerprint. See Fingerprint.h. No OpenSSL dependency.

#include "Fingerprint.h"

namespace campiello {
namespace net {

std::string ToHex(const Fingerprint& fp)
{
	static const char kHex[] = "0123456789abcdef";
	std::string s;
	s.reserve(fp.size() * 2);
	for (uint8_t b : fp) {
		s.push_back(kHex[b >> 4]);
		s.push_back(kHex[b & 0x0F]);
	}
	return s;
}

bool FromHex(const std::string& hex, Fingerprint& out)
{
	if (hex.size() != out.size() * 2)
		return false;
	auto nibble = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	for (size_t i = 0; i < out.size(); i++) {
		int hi = nibble(hex[2 * i]);
		int lo = nibble(hex[2 * i + 1]);
		if (hi < 0 || lo < 0)
			return false;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

bool Equals(const std::vector<uint8_t>& bytes, const Fingerprint& fp)
{
	if (bytes.size() != fp.size())
		return false;
	for (size_t i = 0; i < fp.size(); i++) {
		if (bytes[i] != fp[i])
			return false;
	}
	return true;
}

} // namespace net
} // namespace campiello
