// HapInfo.cpp
//
// See HapInfo.h. Pure data decode; no network, no crypto.

#include "HapInfo.h"

#include <cctype>
#include <cstdlib>

namespace campiello {
namespace homekit {

std::string CategoryName(int categoryId)
{
	switch (categoryId) {
		case 1:  return "Altro";
		case 2:  return "Bridge";
		case 3:  return "Ventilatore";
		case 4:  return "Apricancello";
		case 5:  return "Lampadina";
		case 6:  return "Serratura";
		case 7:  return "Presa";
		case 8:  return "Interruttore";
		case 9:  return "Termostato";
		case 10: return "Sensore";
		case 11: return "Sistema di sicurezza";
		case 12: return "Porta";
		case 13: return "Finestra";
		case 14: return "Tenda / oscurante";
		case 15: return "Interruttore programmabile";
		case 16: return "Range extender";
		case 17: return "Telecamera IP";
		case 18: return "Videocitofono";
		case 19: return "Purificatore d'aria";
		case 20: return "Riscaldatore";
		case 21: return "Condizionatore";
		case 22: return "Umidificatore";
		case 23: return "Deumidificatore";
		case 26: return "Altoparlante";
		case 28: return "Irrigatore";
		case 29: return "Rubinetto";
		case 30: return "Doccia";
		case 31: return "Televisore";
		case 32: return "Telecomando";
		case 33: return "Router WiFi";
		case 34: return "Sintoamplificatore";
		case 35: return "Set-top box TV";
		case 36: return "Chiavetta TV";
		default: return "Sconosciuta";
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
