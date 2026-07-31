// MatterInfo.cpp
//
// See MatterInfo.h. Pure data decode; no network, no crypto.

#include "MatterInfo.h"

#include <cctype>
#include <cstdlib>

namespace campiello {
namespace matter {

std::string DeviceTypeName(int id)
{
	switch (id) {
		case 10:  return "Serratura";
		case 14:  return "Aggregatore (bridge)";
		case 21:  return "Sensore di contatto";
		case 43:  return "Ventilatore";
		case 44:  return "Sensore qualita' aria";
		case 118: return "Rilevatore fumo/CO";
		case 256: return "Luce on/off";
		case 257: return "Luce dimmerabile";
		case 259: return "Interruttore luce on/off";
		case 260: return "Interruttore dimmer";
		case 261: return "Interruttore dimmer a colori";
		case 262: return "Sensore di luce";
		case 263: return "Sensore di presenza";
		case 266: return "Presa on/off";
		case 267: return "Presa dimmerabile";
		case 268: return "Luce a temperatura colore";
		case 269: return "Luce a colori";
		case 514: return "Tenda / oscurante";
		case 769: return "Termostato";
		case 770: return "Sensore di temperatura";
		case 773: return "Sensore di umidita'";
		default:  return "Sconosciuto";
	}
}

static std::string CommissioningText(int cm)
{
	switch (cm) {
		case 0:  return "Non in accoppiamento";
		case 1:  return "In accoppiamento (standard)";
		case 2:  return "In accoppiamento (avanzato)";
		default: return "";
	}
}

MatterInfo ParseMatterTxt(const std::vector<std::pair<std::string, std::string>>& txt)
{
	MatterInfo m;
	for (const std::pair<std::string, std::string>& kv : txt) {
		std::string key;
		for (char c : kv.first)
			key += (char)toupper((unsigned char)c);
		const std::string& v = kv.second;
		if (key == "VP") {
			size_t plus = v.find('+');
			m.vendorId = std::atoi(v.c_str());
			if (plus != std::string::npos)
				m.productId = std::atoi(v.c_str() + plus + 1);
		} else if (key == "DT") {
			m.deviceTypeId = std::atoi(v.c_str());
			m.deviceType = DeviceTypeName(m.deviceTypeId);
		} else if (key == "DN") {
			m.deviceName = v;
		} else if (key == "CM") {
			m.commissioningMode = std::atoi(v.c_str());
			m.commissioningText = CommissioningText(m.commissioningMode);
		} else if (key == "D") {
			m.discriminator = v;
		} else if (key == "SII") {
			m.sessionIdle = v;
		} else if (key == "SAI") {
			m.sessionActive = v;
		} else if (key == "SAT") {
			m.sessionThreshold = v;
		} else if (key == "T") {
			m.tcpSupported = (v == "1");
		}
	}
	return m;
}

} // namespace matter
} // namespace campiello
