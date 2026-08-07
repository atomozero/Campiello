// HapInfo.cpp
//
// See HapInfo.h. Pure data decode; no network, no crypto.

#include "HapInfo.h"

#include <Catalog.h>

#include <cctype>
#include <cstdlib>

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "HomeKit"

namespace campiello {
namespace homekit {

std::string CategoryName(int categoryId)
{
	switch (categoryId) {
		case 1:  return B_TRANSLATE("Altro");
		case 2:  return B_TRANSLATE("Bridge");
		case 3:  return B_TRANSLATE("Ventilatore");
		case 4:  return B_TRANSLATE("Apricancello");
		case 5:  return B_TRANSLATE("Lampadina");
		case 6:  return B_TRANSLATE("Serratura");
		case 7:  return B_TRANSLATE("Presa");
		case 8:  return B_TRANSLATE("Interruttore");
		case 9:  return B_TRANSLATE("Termostato");
		case 10: return B_TRANSLATE("Sensore");
		case 11: return B_TRANSLATE("Sistema di sicurezza");
		case 12: return B_TRANSLATE("Porta");
		case 13: return B_TRANSLATE("Finestra");
		case 14: return B_TRANSLATE("Tenda / oscurante");
		case 15: return B_TRANSLATE("Interruttore programmabile");
		case 16: return B_TRANSLATE("Range extender");
		case 17: return B_TRANSLATE("Telecamera IP");
		case 18: return B_TRANSLATE("Videocitofono");
		case 19: return B_TRANSLATE("Purificatore d'aria");
		case 20: return B_TRANSLATE("Riscaldatore");
		case 21: return B_TRANSLATE("Condizionatore");
		case 22: return B_TRANSLATE("Umidificatore");
		case 23: return B_TRANSLATE("Deumidificatore");
		case 26: return B_TRANSLATE("Altoparlante");
		case 28: return B_TRANSLATE("Irrigatore");
		case 29: return B_TRANSLATE("Rubinetto");
		case 30: return B_TRANSLATE("Doccia");
		case 31: return B_TRANSLATE("Televisore");
		case 32: return B_TRANSLATE("Telecomando");
		case 33: return B_TRANSLATE("Router WiFi");
		case 34: return B_TRANSLATE("Sintoamplificatore");
		case 35: return B_TRANSLATE("Set-top box TV");
		case 36: return B_TRANSLATE("Chiavetta TV");
		default: return B_TRANSLATE("Sconosciuta");
	}
}

HapInfo ParseHapTxt(const std::vector<std::pair<std::string, std::string>>& txt)
{
	HapInfo h;
	h.protocolVersion = "1.0"; // HAP default when pv is absent
	for (const std::pair<std::string, std::string>& kv : txt) {
		std::string key;
		for (char c : kv.first)
			key += (char)tolower((unsigned char)c);
		const std::string& v = kv.second;
		if (key == "md")       h.name = v;
		else if (key == "id")  h.id = v;
		else if (key == "ci")  { h.categoryId = std::atoi(v.c_str()); h.category = CategoryName(h.categoryId); }
		else if (key == "sf")  { h.pairedKnown = true; h.paired = (std::atoi(v.c_str()) & 1) == 0; }
		else if (key == "c#")  h.configNumber = v;
		else if (key == "s#")  h.stateNumber = v;
		else if (key == "pv")  h.protocolVersion = v;
	}
	return h;
}

} // namespace homekit
} // namespace campiello
