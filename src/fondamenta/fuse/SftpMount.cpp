// SftpMount.cpp
//
// See SftpMount.h.

#include "SftpMount.h"

#include <cstdlib>

namespace campiello {
namespace fondamenta {

namespace {

// If `token` is "key=value" with `key` matching `name`, return true and set `value`.
bool MatchOption(const std::string& token, const char* name, std::string& value)
{
	size_t eq = token.find('=');
	if (eq == std::string::npos)
		return false;
	if (token.compare(0, eq, name) != 0)
		return false;
	value = token.substr(eq + 1);
	return true;
}

std::string Env(const char* name)
{
	const char* v = std::getenv(name);
	return (v != nullptr) ? std::string(v) : std::string();
}

} // namespace

SftpMount ParseSftpMount(int argc, char** argv)
{
	SftpMount out;
	if (argc > 0)
		out.fuseArgs.push_back(argv[0]); // program name

	std::string port;
	for (int i = 1; i < argc; ++i) {
		std::string token = argv[i];
		std::string value;
		if (MatchOption(token, "host", value)) {
			out.config.host = value;
		} else if (MatchOption(token, "port", value)) {
			port = value;
		} else if (MatchOption(token, "user", value)) {
			out.config.user = value;
		} else if (MatchOption(token, "password", value)) {
			out.config.password = value;
		} else if (MatchOption(token, "key", value)) {
			out.config.privateKeyPath = value;
		} else if (MatchOption(token, "path", value)) {
			out.config.basePath = value;
		} else {
			out.fuseArgs.push_back(token); // mount point, FUSE -o options, etc.
		}
	}

	// Environment fallbacks for anything not given on the command line.
	if (out.config.host.empty())          out.config.host = Env("CAMPIELLO_SFTP_HOST");
	if (out.config.user.empty())          out.config.user = Env("CAMPIELLO_SFTP_USER");
	if (out.config.password.empty())      out.config.password = Env("CAMPIELLO_SFTP_PASS");
	if (out.config.privateKeyPath.empty()) out.config.privateKeyPath = Env("CAMPIELLO_SFTP_KEY");
	if (out.config.basePath.empty())      out.config.basePath = Env("CAMPIELLO_SFTP_PATH");
	if (port.empty())                     port = Env("CAMPIELLO_SFTP_PORT");

	if (!port.empty()) {
		int p = std::atoi(port.c_str());
		if (p > 0 && p <= 0xFFFF)
			out.config.port = static_cast<uint16_t>(p);
	}

	if (out.config.host.empty()) {
		out.error = "no SFTP host (set host=... or CAMPIELLO_SFTP_HOST)";
		return out;
	}
	if (out.config.user.empty()) {
		out.error = "no SFTP user (set user=... or CAMPIELLO_SFTP_USER)";
		return out;
	}
	out.ok = true;
	return out;
}

std::string BuildMountParameters(const SftpConfig& config)
{
	std::string s = "campiello_sftp";
	s += " host=" + config.host;
	s += " port=" + std::to_string(config.port);
	s += " user=" + config.user;
	if (!config.password.empty())
		s += " password=" + config.password;
	if (!config.privateKeyPath.empty())
		s += " key=" + config.privateKeyPath;
	if (!config.basePath.empty())
		s += " path=" + config.basePath;
	return s;
}

} // namespace fondamenta
} // namespace campiello
