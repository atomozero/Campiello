// ShellyClient.h
//
// Native C++ client for a Shelly smart relay/plug over its LOCAL HTTP API (no cloud). Handles both
// device generations, auto-detected from GET /shelly:
//   gen1 (CoIoT/REST): GET /status (relays[].ison, meters[].power/total, emeters[] for the EM),
//                      control via GET /relay/<id>?turn=on|off|toggle.
//   gen2+ (RPC/HTTP):  GET /rpc/Shelly.GetStatus ("switch:N" -> output/apower/voltage/aenergy),
//                      control via GET /rpc/Switch.Set?id=<id>&on=true|false and Switch.Toggle.
// Verified against real devices on the LAN: a Shelly Plus Plug S (gen2, model SNPL-00112EU,
// /rpc/Shelly.GetDeviceInfo -> gen:2, auth_en:false) and a Shelly EM (gen1).
//
// Protocol references (not code): the Shelly gen1 REST API and the gen2 RPC-over-HTTP API, both
// documented publicly. Auth is off by default on a freshly paired LAN device; when it is enabled the
// calls return HTTP 401 and the app reports it (digest auth is a follow-up).
//
// Dependency-free: plain POSIX sockets + a tiny JSON scanner, no libbe and no third-party library,
// so it is unit-testable off Haiku and the campiello_shelly package stays MIT-clean.

#ifndef CAMPIELLO_SHELLY_SHELLYCLIENT_H
#define CAMPIELLO_SHELLY_SHELLYCLIENT_H

#include <string>
#include <vector>

namespace campiello {
namespace shelly {

// One relay/meter channel of a device. `controllable` relays carry on/off; metering-only channels
// (a Shelly EM's clamps) are read-only power readouts.
struct ShellyChannel {
	int         id = 0;
	std::string name;               // "Canale 1" or the device-set name
	bool        controllable = false;
	bool        on = false;
	bool        hasPower = false;
	double      power = 0.0;         // active power, W
	double      voltage = 0.0;       // V (0 if the channel does not report it)
	double      total = 0.0;         // accumulated energy, Wh
};

// Static identity of the device, from GET /shelly.
struct ShellyInfo {
	std::string id;      // "shellyplusplugs-...", "shellyem-..." (gen2 reports it; synthesized on gen1)
	std::string model;   // model code ("SNPL-00112EU", "SHEM", ...)
	std::string app;     // app/profile ("PlusPlugS", "shellyem", ...)
	std::string mac;
	std::string fw;      // firmware version string
	int         gen = 1; // 1 or 2+ (3/4 speak the gen2 RPC dialect)
	bool        authEnabled = false;
};

class ShellyClient {
public:
	// `gen` may be 0 (unknown): the client probes GET /shelly the first time it needs it.
	explicit ShellyClient(const std::string& host, int port = 80, int gen = 0)
		: fHost(host), fPort(port), fGen(gen) {}

	// GET /shelly and fill `out` (also caches the generation). False on transport failure.
	bool FetchInfo(ShellyInfo& out);

	// Read every relay/meter channel with its current state. False on transport failure.
	bool FetchChannels(std::vector<ShellyChannel>& out);

	// Set / toggle a controllable channel. True on HTTP 200 (false on transport error or 401).
	bool SetOn(int id, bool on);
	bool Toggle(int id);

	int Gen() const { return fGen; }

private:
	// Make fGen known (probe /shelly if it is still 0). Returns the generation (>=1).
	int EnsureGen();
	// HTTP/1.0 GET; returns the response body, *statusOut gets the HTTP status (0 on transport fail).
	std::string HttpGet(const std::string& path, int* statusOut);

	std::string fHost;
	int         fPort;
	int         fGen;
};

// --------------------------------------------------------------------------- JSON scan helpers
// Minimal, dependency-free scanners over a flat-ish JSON body (shared with the app and the tests).

// The string value of the first "key":"value" (empty if absent). Unescapes \" and \\.
std::string JsonString(const std::string& json, const std::string& key);
// The boolean value of the first "key":true/false (out untouched, returns false, if absent).
bool JsonBool(const std::string& json, const std::string& key, bool& out);
// The numeric value of the first "key":<number> (out untouched, returns false, if absent/null).
bool JsonNumber(const std::string& json, const std::string& key, double& out);
// The balanced {...} or [...] substring that is the value of "key": (empty if absent).
std::string JsonValueBlock(const std::string& json, const std::string& key);
// Split a [ {..}, {..} ] array body into its top-level object substrings.
std::vector<std::string> JsonArrayObjects(const std::string& arrayBody);
// The generation implied by a GET /shelly body: the "gen" number if present, else 1.
int GenerationFromShelly(const std::string& shellyJson);

// Turn a raw status body into channels (pure, testable without a socket).
//   gen2: the body of GET /rpc/Shelly.GetStatus ("switch:N" blocks).
//   gen1: the body of GET /status (relays[]/meters[] and, for the EM, emeters[]).
std::vector<ShellyChannel> ParseChannelsGen2(const std::string& statusJson);
std::vector<ShellyChannel> ParseChannelsGen1(const std::string& statusJson);

} // namespace shelly
} // namespace campiello

#endif // CAMPIELLO_SHELLY_SHELLYCLIENT_H
