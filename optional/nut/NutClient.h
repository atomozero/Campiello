// NutClient.h
//
// A tiny read-only client for the Network UPS Tools (NUT) server protocol, advertised as _nut._tcp
// on TCP port 3493. NUT is a simple line-oriented text protocol (docs: the NUT "Network protocol
// information"): the client asks for the list of UPS units and their variables, the server answers
// with BEGIN LIST / VAR ... / END LIST blocks. This add-on only ever reads (LIST/GET); it never
// sends an INSTCMD or SET, so it cannot change the UPS state - it is a monitor, matching Campiello's
// "look, do not touch" rule for infrastructure.
//
// Protocol sketch:
//   -> LIST UPS
//   <- BEGIN LIST UPS
//      UPS <name> "<description>"
//      END LIST UPS
//   -> LIST VAR <name>
//   <- BEGIN LIST VAR <name>
//      VAR <name> battery.charge "100"
//      ...
//      END LIST VAR <name>
// Values are double-quoted and may escape " and \ with a backslash.
//
// Portable: standard C++ + POSIX sockets, no BeAPI and no third-party dependency, so the parsers are
// unit-testable off Haiku.

#ifndef CAMPIELLO_NUT_NUTCLIENT_H
#define CAMPIELLO_NUT_NUTCLIENT_H

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace campiello {
namespace nut {

// Unescape a NUT double-quoted value body (the text between the quotes): "\\" -> "\", "\"" -> "\"".
std::string Unescape(const std::string& in);

// Parse a "LIST UPS" reply into (name, description) pairs. Ignores the BEGIN/END framing and any
// unrelated lines.
std::vector<std::pair<std::string, std::string>> ParseListUps(const std::string& reply);

// Parse a "LIST VAR <ups>" reply into a variable map (key -> value). Only VAR lines for `ups` are
// taken; values are unescaped.
std::map<std::string, std::string> ParseListVar(const std::string& reply, const std::string& ups);

// Decode a NUT "ups.status" value (space-separated flags like "OL CHRG") into a readable Italian
// phrase. Unknown flags are passed through verbatim.
std::string StatusText(const std::string& status);

// Format a "battery.runtime" value in seconds as a human duration ("1 h 5 min"). Non-numeric input
// is returned unchanged.
std::string RuntimeText(const std::string& seconds);

class NutClient {
public:
	explicit NutClient(const std::string& host, int port = 3493) : fHost(host), fPort(port) {}

	// The units the server exposes (usually one). Empty on connection failure.
	std::vector<std::pair<std::string, std::string>> ListUps();

	// All variables of one unit. Empty on failure.
	std::map<std::string, std::string> ListVars(const std::string& ups);

private:
	// Open, send `command\n`, read until the connection idles, LOGOUT, close. Returns "" on failure.
	std::string Exchange(const std::string& command);

	std::string fHost;
	int fPort;
};

} // namespace nut
} // namespace campiello

#endif // CAMPIELLO_NUT_NUTCLIENT_H
