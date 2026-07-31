// HapInfo.h
//
// Read-only decoder for an Apple HomeKit (HAP) accessory's advertised metadata. A HAP accessory
// announces itself over mDNS as _hap._tcp with a TXT record; this turns those TXT key/values into a
// human-readable summary (name, category, pairing state, config number, protocol version).
//
// This does NOT control the accessory: HomeKit control requires a paired secure session (SRP pairing,
// then per-message ChaCha20-Poly1305 with Ed25519 identities), which is a heavy crypto module and a
// documented follow-up (docs/addons/homekit.md). Decoding the public TXT needs no crypto and no
// third-party dependency (MIT-clean).
//
// References: the HomeKit Accessory Protocol specification (non-commercial), Apple's HomeKitADK, and
// the hap-python / HAP-NodeJS projects for the TXT keys (md/id/ci/sf/c#/s#/pv) and the category ids.

#ifndef CAMPIELLO_HOMEKIT_HAPINFO_H
#define CAMPIELLO_HOMEKIT_HAPINFO_H

#include <string>
#include <utility>
#include <vector>

namespace campiello {
namespace homekit {

struct HapInfo {
	std::string name;            // md (model / accessory name)
	std::string id;              // id (device id)
	int         categoryId = 0;  // ci
	std::string category;        // human name for ci
	bool        pairedKnown = false;
	bool        paired = false;  // from sf: bit 0 clear = paired
	std::string configNumber;    // c#
	std::string stateNumber;     // s#
	std::string protocolVersion; // pv (default "1.0")
};

// The human-readable (Italian) name for a HAP category id (e.g. 5 -> "Lampadina").
std::string CategoryName(int categoryId);

// Decode HAP TXT key/values into a HapInfo.
HapInfo ParseHapTxt(const std::vector<std::pair<std::string, std::string>>& txt);

} // namespace homekit
} // namespace campiello

#endif // CAMPIELLO_HOMEKIT_HAPINFO_H
