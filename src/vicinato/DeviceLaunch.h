// DeviceLaunch.h
//
// The launch protocol for the device add-on framework (docs/DEVICE_ADDONS.md): how a discovered
// device is handed to a handler app. The WON app builds the argument list with BuildLaunchArgs and
// launches the handler by signature; the handler parses it back with ParseDevice. Values are
// percent-encoded so a name or TXT value with spaces survives argv tokenization - the same trick as
// the SMB mount parameters. Pure std (no Haiku), so both sides and the test share one codec.

#ifndef CAMPIELLO_VICINATO_DEVICELAUNCH_H
#define CAMPIELLO_VICINATO_DEVICELAUNCH_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "NetworkDirectory.h"

namespace campiello {
namespace vicinato {

// A device as a handler receives it.
struct DeviceInfo {
	std::string host;
	uint16_t    port = 0;
	std::string type;    // mDNS service type, e.g. "_hue._tcp"
	std::string name;    // friendly label
	std::string action;  // "" or "open" = the default action; else the chosen action id
	std::vector<std::pair<std::string, std::string>> txt; // TXT records
};

// Percent-encode / decode a single value (space -> %20, '%' -> %25, control bytes -> %XX).
std::string EncodeArg(const std::string& value);
std::string DecodeArg(const std::string& value);

// Build the argument list (the tokens after argv[0]) for launching a handler on `service` with
// `action` ("" for the default): host=.. port=.. type=.. name=.. [action=..] txt.<k>=<v>...
std::vector<std::string> BuildLaunchArgs(const NetworkService& service, const std::string& action);

// Parse a handler's argv back into a DeviceInfo (skips argv[0] and unrecognized tokens).
DeviceInfo ParseDevice(int argc, const char* const* argv);

} // namespace vicinato
} // namespace campiello

#endif // CAMPIELLO_VICINATO_DEVICELAUNCH_H
