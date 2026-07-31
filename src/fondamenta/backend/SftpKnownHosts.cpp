// SftpKnownHosts.cpp
//
// Implementation of the SFTP host-key trust-on-first-use store. See SftpKnownHosts.h.

#include "SftpKnownHosts.h"

#include <cstdio>
#include <fstream>

#include <sys/stat.h>

namespace campiello {
namespace fondamenta {

namespace {

std::string ToHex(const std::vector<uint8_t>& bytes)
{
	static const char* kDigits = "0123456789abcdef";
	std::string out;
	out.reserve(bytes.size() * 2);
	for (uint8_t b : bytes) {
		out += kDigits[b >> 4];
		out += kDigits[b & 0x0F];
	}
	return out;
}

// Parse an even-length lowercase/uppercase hex string into bytes. False on odd length or a
// non-hex character.
bool FromHex(const std::string& hex, std::vector<uint8_t>& out)
{
	if (hex.empty() || (hex.size() % 2) != 0)
		return false;
	auto nibble = [](char c, int& v) -> bool {
		if (c >= '0' && c <= '9') { v = c - '0'; return true; }
		if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
		if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
		return false;
	};
	std::vector<uint8_t> bytes;
	bytes.reserve(hex.size() / 2);
	for (size_t i = 0; i < hex.size(); i += 2) {
		int hi = 0, lo = 0;
		if (!nibble(hex[i], hi) || !nibble(hex[i + 1], lo))
			return false;
		bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
	}
	out = std::move(bytes);
	return true;
}

// A host token carries no whitespace (it is the first field on a line). True if usable.
bool ValidHost(const std::string& host)
{
	if (host.empty())
		return false;
	for (char c : host) {
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			return false;
	}
	return true;
}

} // namespace

void SftpKnownHosts::Pin(const std::string& host, const std::vector<uint8_t>& key)
{
	for (KnownHost& h : fHosts) {
		if (h.host == host) {
			h.key = key;
			return;
		}
	}
	fHosts.push_back(KnownHost{host, key});
}

bool SftpKnownHosts::Forget(const std::string& host)
{
	for (size_t i = 0; i < fHosts.size(); i++) {
		if (fHosts[i].host == host) {
			fHosts.erase(fHosts.begin() + i);
			return true;
		}
	}
	return false;
}

const KnownHost* SftpKnownHosts::Find(const std::string& host) const
{
	for (const KnownHost& h : fHosts) {
		if (h.host == host)
			return &h;
	}
	return nullptr;
}

bool SftpKnownHosts::IsKnown(const std::string& host) const
{
	return Find(host) != nullptr;
}

HostKeyStatus SftpKnownHosts::Evaluate(const std::string& host,
	const std::vector<uint8_t>& key) const
{
	const KnownHost* known = Find(host);
	if (known == nullptr)
		return HostKeyStatus::kUnknown;
	return (known->key == key) ? HostKeyStatus::kTrusted : HostKeyStatus::kKeyChanged;
}

bool SftpKnownHosts::SaveToFile(const std::string& path) const
{
	std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::out | std::ios::trunc);
		if (!out)
			return false;
		for (const KnownHost& h : fHosts) {
			if (!ValidHost(h.host) || h.key.empty())
				continue; // never write a malformed entry
			out << h.host << ' ' << ToHex(h.key) << '\n';
		}
		out.flush();
		if (!out)
			return false;
	}
	::chmod(tmp.c_str(), 0600);
	return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool SftpKnownHosts::LoadFromFile(const std::string& path)
{
	std::ifstream in(path);
	if (!in)
		return false;

	std::vector<KnownHost> hosts;
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		size_t sp = line.find(' ');
		if (sp == std::string::npos)
			continue;
		std::string host = line.substr(0, sp);
		std::string hex = line.substr(sp + 1);
		std::vector<uint8_t> key;
		if (!ValidHost(host) || !FromHex(hex, key))
			continue;
		hosts.push_back(KnownHost{host, key});
	}
	fHosts = std::move(hosts);
	return true;
}

} // namespace fondamenta
} // namespace campiello
