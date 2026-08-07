// AirplayInfo.cpp
//
// See AirplayInfo.h. Pure data decode; no network, no crypto.

#include "AirplayInfo.h"

#include <Catalog.h>

#include <cctype>
#include <cstdlib>

// Haiku Locale Kit: the feature labels below are shown to the user (in the AirPlay window's
// "Funzioni" line), so they are marked here for catkeys collection and translated at the point of
// display in campiello_airplay.cpp (B_TRANSLATE there; B_TRANSLATE_MARK only marks the source here).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AirPlay"

namespace campiello {
namespace airplay {

unsigned long long ParseFeatures(const std::string& s)
{
	// "0xLOW,0xHIGH" -> low | (high << 32); or a single value.
	size_t comma = s.find(',');
	if (comma != std::string::npos) {
		unsigned long long low = std::strtoull(s.substr(0, comma).c_str(), nullptr, 0);
		unsigned long long high = std::strtoull(s.substr(comma + 1).c_str(), nullptr, 0);
		return (high << 32) | (low & 0xffffffffULL);
	}
	return std::strtoull(s.c_str(), nullptr, 0);
}

// A curated set of well-known AirPlay feature bits (per pyatv's AirPlayFlags). Not exhaustive.
std::vector<std::string> DecodeFeatures(unsigned long long f)
{
	struct Bit { int bit; const char* label; };
	static const Bit kBits[] = {
		{0,  B_TRANSLATE_MARK("Video")},
		{1,  B_TRANSLATE_MARK("Foto")},
		{7,  B_TRANSLATE_MARK("Screen mirroring")},
		{9,  B_TRANSLATE_MARK("Audio")},
		{11, B_TRANSLATE_MARK("Audio ridondante")},
		{14, B_TRANSLATE_MARK("MFi (soft AirPlay)")},
		{18, B_TRANSLATE_MARK("Controllo multimediale unificato")},
		{30, B_TRANSLATE_MARK("Pair-Setup unificato (MFi)")},
		{38, B_TRANSLATE_MARK("Audio bufferizzato")},
		{40, B_TRANSLATE_MARK("Sincronizzazione PTP")},
		{46, B_TRANSLATE_MARK("Pairing HomeKit")},
		{48, B_TRANSLATE_MARK("AirPlay 2 (CoreUtils)")},
	};
	std::vector<std::string> out;
	for (const Bit& b : kBits)
		if (f & (1ULL << b.bit))
			out.push_back(b.label);
	return out;
}

AirplayInfo ParseAirplayTxt(const std::vector<std::pair<std::string, std::string>>& txt)
{
	AirplayInfo a;
	for (const std::pair<std::string, std::string>& kv : txt) {
		std::string key;
		for (char c : kv.first)
			key += (char)tolower((unsigned char)c);
		const std::string& v = kv.second;
		if (key == "model")        a.model = v;
		else if (key == "deviceid") a.deviceId = v;
		else if (key == "srcvers")  a.srcVersion = v;
		else if (key == "features") a.features = ParseFeatures(v);
		else if (key == "pw")       a.passwordRequired = (v == "true" || v == "1");
		else if (key == "flags")    { if (ParseFeatures(v) & 0x80) a.passwordRequired = true; }
	}
	a.capabilities = DecodeFeatures(a.features);
	return a;
}

} // namespace airplay
} // namespace campiello
