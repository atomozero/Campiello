// NetworkDirectory.h
//
// The unified neighborhood model behind the Vicinato surface (docs/NEIGHBORHOOD.md): it turns the
// raw services the radar discovers into a classified list of NetworkService entries, each tagged
// with what it is, whether you can browse it as files, how it authenticates, and which backend
// serves it. This is the seam both the Vicinato companion app and (later) a neighborhood FUSE
// volume consume.
//
// Portable: pure standard C++, built on MdnsRadar's snapshot and RadarLabels' naming, no BeAPI,
// so the classification is unit-tested off Haiku.

#ifndef CAMPIELLO_VICINATO_NETWORKDIRECTORY_H
#define CAMPIELLO_VICINATO_NETWORKDIRECTORY_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../bricola/mdns/MdnsRadar.h"

namespace campiello {
namespace vicinato {

// What kind of thing a service is, for the icon and the double-click action.
enum class ServiceKind {
	Campiello, // a native Campiello peer
	Smb,       // a Windows/Samba file share
	Sftp,      // an SSH/SFTP host
	Computer,  // a general computer on the LAN (_workstation, _device-info, NetBIOS host)
	Home,      // a home/IoT device (Hue, HomeKit, Matter, Continuity...)
	Web,       // a web service
	Printer,
	Media,     // cast/airplay/dlna...
	Other,
};

// How entering a service authenticates.
enum class AuthKind {
	None,     // open, just enter
	Password, // username/password (SMB, SFTP)
	Pairing,  // one-tap Campiello pairing
};

// Which PeerBackend serves the files, if any.
enum class BackendKind {
	None, // not a file service (info card only)
	Cnp,
	Smb,
	Sftp,
};

// One discovered service, ready to present and act on.
struct NetworkService {
	std::string id;          // stable key (service type + instance/host)
	std::string label;       // friendly display name
	std::string category;    // from RadarLabels (Casa, File, Media...)
	std::string host;        // resolved address (or SRV target)
	uint16_t    port = 0;
	std::string serviceType; // raw DNS-SD type
	std::vector<std::pair<std::string, std::string>> txt;
	ServiceKind kind      = ServiceKind::Other;
	AuthKind    auth      = AuthKind::None;
	BackendKind backend   = BackendKind::None;
	bool        browsable = false; // can we enter it as a folder of files?

	// Passive LAN-intel enrichment (NetIntel), filled in asynchronously when the host is a dotted
	// IP present in the ARP table. Both empty until then; never required to present the service.
	std::string mac;    // "aa:bb:cc:dd:ee:ff", from the kernel ARP cache
	std::string vendor; // manufacturer, from the IEEE OUI database (if oui.txt is installed)
};

// Classify a DNS-SD service type into kind/auth/backend/browsable (the fields that do not depend
// on a specific instance). label/category come from RadarLabels.
NetworkService ClassifyServiceType(const std::string& serviceType);

// Build the neighborhood from a radar snapshot: one NetworkService per discovered instance, named
// and classified, sorted by kind then label.
std::vector<NetworkService> BuildNeighborhood(const bricola::mdns::RadarSnapshot& snap);

} // namespace vicinato
} // namespace campiello

#endif // CAMPIELLO_VICINATO_NETWORKDIRECTORY_H
