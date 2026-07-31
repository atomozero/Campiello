// NetIntel.h
//
// Passive LAN intelligence for the WON neighborhood: four small, dependency-free capabilities that
// enrich or extend what mDNS + the SMB sweep already find. All four are ports of the corresponding
// pieces of the sibling LANterna project (MIT, atomozero) - see docs/NETINTEL.md - reworked into a
// single Campiello module under the campiello::vicinato namespace:
//
//   1. ReadArpCache()      - IP -> MAC from the kernel ARP table (Haiku: `arp -a`).
//   2. OuiDatabase         - MAC prefix -> manufacturer, from the public IEEE oui.txt (the DATA is
//                            reused, never Wireshark's GPL `manuf`). The file is not bundled; it is
//                            looked up in the Campiello data dirs and the feature degrades silently
//                            when absent (zero-config: absence just hides the vendor line).
//   3. SendWakeOnLan()     - a Wake-on-LAN magic packet to a MAC (UDP broadcast).
//   4. QueryNetBiosName()  - the Windows/NetBIOS computer name of an SMB host (UDP 137 NBSTAT).
//   5. DiscoverSsdp()      - SSDP/UPnP M-SEARCH: finds TVs, Sonos, NAS and routers that mDNS misses.
//
// Portable POSIX sockets only (the same surface SmbHostFinder / TcpPing already use on Haiku). The
// pure parsing/encoding helpers are exposed so they can be unit-tested off Haiku with no sockets.

#ifndef CAMPIELLO_VICINATO_NETINTEL_H
#define CAMPIELLO_VICINATO_NETINTEL_H

#include <cstdint>
#include <map>
#include <string>

namespace campiello {
namespace vicinato {

// ---------------------------------------------------------------------------------------------
// 1. ARP cache: IP (dotted quad) -> MAC ("aa:bb:cc:dd:ee:ff"). Empty map if unavailable.
// ---------------------------------------------------------------------------------------------
std::map<std::string, std::string> ReadArpCache();

// ---------------------------------------------------------------------------------------------
// 2. OUI database: MAC prefix -> manufacturer, loaded from an IEEE-format oui.txt.
// ---------------------------------------------------------------------------------------------
class OuiDatabase {
public:
	// Load from an IEEE oui.txt ("00-50-C2   (hex)   Vendor Name"). Returns entries read
	// (0 if the file is absent or has no valid entries). Replaces any previously loaded data.
	size_t LoadFromFile(const std::string& path);

	// Manufacturer for a MAC ("aa:bb:cc:..." or "aabbcc..."), or an empty string if unknown.
	std::string Lookup(const std::string& mac) const;

	size_t Size() const { return fByPrefix.size(); }
	bool Empty() const { return fByPrefix.empty(); }

	// Normalise a MAC to its first 6 uppercase hex digits (the OUI). Empty if fewer than 6.
	static std::string OuiKey(const std::string& mac);

private:
	std::map<std::string, std::string> fByPrefix; // "AABBCC" -> vendor
};

// Resolve the first existing oui.txt across the Campiello data dirs (user settings, then user data,
// then system data). Empty string if none is present. Purely a path lookup; does not open the file.
std::string FindOuiFile();

// ---------------------------------------------------------------------------------------------
// 3. Wake-on-LAN.
// ---------------------------------------------------------------------------------------------
// Build a 102-byte magic packet (6x 0xFF + 16x the MAC) into out. Returns false on a malformed MAC.
bool BuildWolPacket(const std::string& mac, uint8_t out[102]);

// Send a magic packet for `mac`. `broadcastIp` defaults to the limited broadcast; `port` to 9
// (discard). Returns true if the datagram was sent (not that the target woke).
bool SendWakeOnLan(const std::string& mac, const std::string& broadcastIp = "255.255.255.255",
	int port = 9);

// The /24 directed broadcast for a dotted-quad IP ("192.168.2.50" -> "192.168.2.255"), or empty.
std::string DirectedBroadcast(const std::string& ip);

// ---------------------------------------------------------------------------------------------
// 4. NetBIOS node status (UDP 137): the computer name of a Windows/SMB host.
// ---------------------------------------------------------------------------------------------
// Query `ip` for its NetBIOS names; on success fills `name` (workstation/server) and, when present,
// `workgroup`. Returns false on timeout or no usable name. Blocks up to timeoutMs.
bool QueryNetBiosName(const std::string& ip, int timeoutMs, std::string& name,
	std::string& workgroup);

// Pure helpers (testable without a socket):
std::string BuildNbstatQuery();
bool ParseNbstatResponse(const uint8_t* buf, size_t len, std::string& name, std::string& workgroup);

// ---------------------------------------------------------------------------------------------
// 5. SSDP / UPnP discovery.
// ---------------------------------------------------------------------------------------------
struct SsdpDevice {
	std::string server;     // SERVER header (e.g. "Linux/3.14 UPnP/1.0 Sonos/...")
	std::string deviceType; // ST / NT header
	std::string location;   // LOCATION header (device description URL)
};

// One M-SEARCH burst; collect responders for up to timeoutMs. Keyed by responder IP.
std::map<std::string, SsdpDevice> DiscoverSsdp(int timeoutMs);

// Pure helpers (testable):
std::string SsdpHeaderValue(const std::string& msg, const char* name);
std::string InferSsdpType(const SsdpDevice& d); // human label from SERVER/ST (Italian)

} // namespace vicinato
} // namespace campiello

#endif // CAMPIELLO_VICINATO_NETINTEL_H
