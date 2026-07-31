// MatterInfo.h
//
// Read-only decoder for a Matter device's advertised metadata. A Matter device announces itself over
// mDNS as _matterc._udp (commissionable) or _matter._tcp (operational) with a TXT record; this turns
// those TXT key/values into a human-readable summary (vendor/product, device type, commissioning
// state, discriminator, session timing).
//
// This does NOT commission or control the device: Matter commissioning/control needs PASE/CASE
// sessions, device attestation certificates, and Thread/Wi-Fi onboarding (the Matter SDK / heavy
// crypto), a documented follow-up (docs/addons/matter.md). Decoding the public TXT needs no crypto
// and no third-party dependency (MIT-clean).
//
// References: the Matter Core Specification (CSA), the connectedhomeip SDK, and python-matter-server
// for the TXT keys (D/VP/CM/DT/DN/SII/SAI/SAT/T) and the device type ids.

#ifndef CAMPIELLO_MATTER_MATTERINFO_H
#define CAMPIELLO_MATTER_MATTERINFO_H

#include <string>
#include <utility>
#include <vector>

namespace campiello {
namespace matter {

struct MatterInfo {
	int         vendorId = 0;        // from VP
	int         productId = 0;       // from VP
	int         deviceTypeId = 0;    // DT
	std::string deviceType;          // human name for DT
	std::string deviceName;          // DN
	int         commissioningMode = -1; // CM (-1 = unknown)
	std::string commissioningText;   // human text for CM
	std::string discriminator;       // D
	std::string sessionIdle;         // SII (ms)
	std::string sessionActive;       // SAI (ms)
	std::string sessionThreshold;    // SAT (ms)
	bool        tcpSupported = false; // T
};

// The human-readable (Italian) name for a Matter device type id (e.g. 257 -> "Luce dimmerabile").
std::string DeviceTypeName(int deviceTypeId);

// Decode Matter TXT key/values into a MatterInfo.
MatterInfo ParseMatterTxt(const std::vector<std::pair<std::string, std::string>>& txt);

} // namespace matter
} // namespace campiello

#endif // CAMPIELLO_MATTER_MATTERINFO_H
