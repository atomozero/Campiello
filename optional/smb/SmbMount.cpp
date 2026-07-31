// SmbMount.cpp
//
// See SmbMount.h.

#include "SmbMount.h"

#include <cstdlib>

namespace campiello {
namespace fondamenta {

namespace {

// Percent-decode a value. The mount parameter string is whitespace-tokenized by userlandfs before
// the add-on sees it, so a value that can contain spaces (notably a share name like
// "din esp8266 mini") must be percent-encoded by the producer (%20 for space, %25 for '%'). We
// decode it back here. Unknown/short escapes are left as-is. See EncodeMountValue in SmbMount.h.
std::string PercentDecode(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); ++i) {
		if (in[i] == '%' && i + 2 < in.size()) {
			auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			int hi = hex(in[i + 1]);
			int lo = hex(in[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out.push_back(static_cast<char>((hi << 4) | lo));
				i += 2;
				continue;
			}
		}
		out.push_back(in[i]);
	}
	return out;
}

// If `token` is "key=value" with `key` matching `name`, return true and set `value` (decoded).
bool MatchOption(const std::string& token, const char* name, std::string& value)
{
	size_t eq = token.find('=');
	if (eq == std::string::npos)
		return false;
	if (token.compare(0, eq, name) != 0)
		return false;
	value = PercentDecode(token.substr(eq + 1));
	return true;
}

std::string Env(const char* name)
{
	const char* v = std::getenv(name);
	return (v != nullptr) ? std::string(v) : std::string();
}

} // namespace

SmbMount ParseSmbMount(int argc, char** argv)
{
	SmbMount out;
	if (argc > 0)
		out.fuseArgs.push_back(argv[0]); // program name

	for (int i = 1; i < argc; ++i) {
		std::string token = argv[i];
		std::string value;
		if (MatchOption(token, "server", value)) {
			out.config.server = value;
		} else if (MatchOption(token, "share", value)) {
			out.config.share = value;
		} else if (MatchOption(token, "user", value)) {
			out.config.user = value;
		} else if (MatchOption(token, "password", value)) {
			out.config.password = value;
		} else if (MatchOption(token, "domain", value)) {
			out.config.domain = value;
		} else if (MatchOption(token, "path", value)) {
			out.config.basePath = value;
		} else {
			out.fuseArgs.push_back(token); // mount point, FUSE -o options, etc.
		}
	}

	// Environment fallbacks for anything not given on the command line.
	if (out.config.server.empty())   out.config.server = Env("CAMPIELLO_SMB_SERVER");
	if (out.config.share.empty())    out.config.share = Env("CAMPIELLO_SMB_SHARE");
	if (out.config.user.empty())     out.config.user = Env("CAMPIELLO_SMB_USER");
	if (out.config.password.empty()) out.config.password = Env("CAMPIELLO_SMB_PASS");
	if (out.config.domain.empty())   out.config.domain = Env("CAMPIELLO_SMB_DOMAIN");
	if (out.config.basePath.empty()) out.config.basePath = Env("CAMPIELLO_SMB_PATH");

	if (out.config.server.empty()) {
		out.error = "no SMB server (set server=... or CAMPIELLO_SMB_SERVER)";
		return out;
	}
	// An empty share is valid: it selects the Windows "\\server" style server-level mount, whose
	// root lists the shares as folders (SmbServerBackend). A named share mounts that one share
	// (SmbBackend). Either way we have enough to proceed.
	out.ok = true;
	return out;
}

std::string EncodeMountValue(const std::string& value)
{
	static const char* kHex = "0123456789ABCDEF";
	std::string out;
	out.reserve(value.size());
	for (unsigned char c : value) {
		if (c <= 0x20 || c == '%' || c == 0x7f) {
			out.push_back('%');
			out.push_back(kHex[c >> 4]);
			out.push_back(kHex[c & 0x0f]);
		} else {
			out.push_back(static_cast<char>(c));
		}
	}
	return out;
}

std::string BuildSmbMountParameters(const SmbConfig& config)
{
	std::string s = "campiello_smb";
	s += " server=" + EncodeMountValue(config.server);
	if (!config.share.empty()) // omitted -> server-level mount (root lists the shares)
		s += " share=" + EncodeMountValue(config.share);
	if (!config.user.empty())
		s += " user=" + EncodeMountValue(config.user);
	if (!config.password.empty())
		s += " password=" + EncodeMountValue(config.password);
	if (!config.domain.empty())
		s += " domain=" + EncodeMountValue(config.domain);
	if (!config.basePath.empty())
		s += " path=" + EncodeMountValue(config.basePath);
	return s;
}

} // namespace fondamenta
} // namespace campiello
