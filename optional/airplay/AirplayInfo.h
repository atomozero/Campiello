// AirplayInfo.h
//
// Read-only decoder for an AirPlay receiver's advertised metadata. An AirPlay receiver announces
// itself over mDNS as _airplay._tcp (and _raop._tcp for the audio/RAOP part) with a TXT record; this
// turns those TXT key/values into a human-readable summary (model, device id, source version) and
// decodes the "features" capability bitfield into readable labels.
//
// This does NOT stream to or control the receiver: AirPlay streaming/mirroring needs the AirPlay 2
// handshake (pair-setup/pair-verify) and FairPlay, i.e. substantial crypto, a documented follow-up
// (docs/addons/airplay.md). Decoding the public TXT needs no crypto and no third-party dependency
// (MIT-clean).
//
// References: the unofficial AirPlay protocol documentation and the pyatv / OwnTone projects for the
// TXT keys (model/deviceid/features/flags/srcvers/pk) and the features bit meanings.

#ifndef CAMPIELLO_AIRPLAY_AIRPLAYINFO_H
#define CAMPIELLO_AIRPLAY_AIRPLAYINFO_H

#include <string>
#include <utility>
#include <vector>

namespace campiello {
namespace airplay {

struct AirplayInfo {
	std::string model;      // model (e.g. AppleTV3,2 / AudioAccessory1,1)
	std::string deviceId;   // deviceid
	std::string srcVersion; // srcvers
	unsigned long long features = 0;
	std::vector<std::string> capabilities; // decoded feature labels
	bool passwordRequired = false;         // from pw / flags
};

// Parse a features string ("0xLOW,0xHIGH" two 32-bit words, or a single hex/decimal) into a 64-bit
// value.
unsigned long long ParseFeatures(const std::string& s);

// Decode the features bitfield into readable (Italian) capability labels (a curated, well-known set).
std::vector<std::string> DecodeFeatures(unsigned long long features);

// Decode AirPlay TXT key/values into an AirplayInfo.
AirplayInfo ParseAirplayTxt(const std::vector<std::pair<std::string, std::string>>& txt);

} // namespace airplay
} // namespace campiello

#endif // CAMPIELLO_AIRPLAY_AIRPLAYINFO_H
