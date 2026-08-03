// RadarLabels.cpp
//
// See RadarLabels.h. The tables here are best-effort readings of common DNS-SD service types and
// their TXT conventions (HAP/HomeKit, Matter, Bonjour printers, Google Cast, AirPlay); they do
// not need to be exhaustive, only helpful. Unknown things fall back to their raw form.

#include "RadarLabels.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace campiello {
namespace bricola {
namespace mdns {

namespace {

struct KnownService {
	const char* type;
	const char* label;
	const char* category;
};

// Common service types (RFC 6763 registrations and well-known vendor types).
const KnownService kKnown[] = {
	{"_campiello._tcp.local",      "Campiello",                  "Campiello"},
	{"_smb._tcp.local",            "Condivisione Windows (SMB)", "File"},
	{"_afpovertcp._tcp.local",     "Condivisione Apple (AFP)",   "File"},
	{"_nfs._tcp.local",            "Condivisione NFS",           "File"},
	{"_webdav._tcp.local",         "WebDAV",                     "File"},
	{"_webdavs._tcp.local",        "WebDAV sicuro",              "File"},
	{"_ftp._tcp.local",            "FTP",                        "File"},
	{"_sftp-ssh._tcp.local",       "SFTP (SSH)",                 "File"},
	{"_ssh._tcp.local",            "SSH",                        "Sistema"},
	{"_http._tcp.local",           "Web",                        "Web"},
	{"_https._tcp.local",          "Web sicuro",                 "Web"},
	{"_ipp._tcp.local",            "Stampante (IPP)",            "Stampa"},
	{"_ipps._tcp.local",           "Stampante (IPPS)",           "Stampa"},
	{"_printer._tcp.local",        "Stampante (LPR)",            "Stampa"},
	{"_pdl-datastream._tcp.local", "Stampante (RAW 9100)",       "Stampa"},
	{"_scanner._tcp.local",        "Scanner",                    "Stampa"},
	{"_uscan._tcp.local",          "Scanner (eSCL)",             "Stampa"},
	{"_hue._tcp.local",            "Philips Hue",                "Casa"},
	{"_hap._tcp.local",            "Apple HomeKit",              "Casa"},
	{"_matter._tcp.local",         "Matter",                     "Casa"},
	{"_matterc._udp.local",        "Matter (in abbinamento)",    "Casa"},
	{"_matterd._udp.local",        "Matter (operativo)",         "Casa"},
	{"_companion-link._tcp.local", "Apple Continuity",           "Casa"},
	{"_airplay._tcp.local",        "AirPlay",                    "Media"},
	{"_raop._tcp.local",           "AirPlay Audio",              "Media"},
	{"_airport._tcp.local",        "Apple AirPort",              "Rete"},
	{"_googlecast._tcp.local",     "Chromecast / Google Cast",   "Media"},
	{"_spotify-connect._tcp.local","Spotify Connect",            "Media"},
	{"_amzn-wplay._tcp.local",     "Amazon (Whisperplay)",       "Media"},
	{"_amzn-alexa._tcp.local",     "Amazon Alexa",               "Media"},
	{"_daap._tcp.local",           "Libreria iTunes (DAAP)",     "Media"},
	{"_workstation._tcp.local",    "Computer",                   "Computer"},
	{"_device-info._tcp.local",    "Info dispositivo",           "Sistema"},
	{"_rfb._tcp.local",            "Desktop remoto (VNC)",       "Sistema"},
	{"_sleap._tcp.local",          "Lutron",                     "Casa"},
	{"_dkapi._tcp.local",          "Condizionatore Daikin",      "Casa"},
	{"_esphomelib._tcp.local",     "Dispositivo ESPHome",        "Casa"},
	{"_nut._tcp.local",            "Gruppo di continuita (UPS)", "Sistema"},
	{"_eero._tcp.local",           "Router mesh eero",           "Rete"},
};

bool EndsWith(const std::string& s, const std::string& suffix)
{
	return s.size() >= suffix.size()
		&& s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsMatter(const std::string& type)
{
	return type.rfind("_matter", 0) == 0; // _matter._tcp, _matterc._udp, _matterd._udp
}

bool IsAmznWplay(const std::string& type)
{
	return type == "_amzn-wplay._tcp.local";
}

// Find a TXT value by key (case-sensitive, as advertised).
bool TxtGet(const std::vector<std::pair<std::string, std::string>>& txt, const char* key,
	std::string& out)
{
	for (const auto& kv : txt) {
		if (kv.first == key) {
			out = kv.second;
			return true;
		}
	}
	return false;
}

// Parse an integer that may be decimal ("257") or hex ("0x0101"). Returns false on garbage.
bool ParseInt(const std::string& s, long& out)
{
	if (s.empty())
		return false;
	char* end = nullptr;
	out = std::strtol(s.c_str(), &end, 0); // base 0: honors a 0x prefix
	return end != nullptr && *end == '\0';
}

// HomeKit accessory category identifiers (HAP "ci"), the meaningful ones.
const char* HomeKitCategory(long ci)
{
	switch (ci) {
		case 1:  return "Altro";
		case 2:  return "Bridge";
		case 3:  return "Ventilatore";
		case 4:  return "Apriporta garage";
		case 5:  return "Lampadina";
		case 6:  return "Serratura";
		case 7:  return "Presa";
		case 8:  return "Interruttore";
		case 9:  return "Termostato";
		case 10: return "Sensore";
		case 11: return "Sistema di sicurezza";
		case 12: return "Porta";
		case 13: return "Finestra";
		case 14: return "Tenda / tapparella";
		case 15: return "Interruttore programmabile";
		case 16: return "Range extender";
		case 17: return "Videocamera IP";
		case 18: return "Videocitofono";
		case 19: return "Purificatore d'aria";
		case 20: return "Riscaldatore";
		case 21: return "Condizionatore";
		case 22: return "Umidificatore";
		case 23: return "Deumidificatore";
		case 26: return "Irrigatore";
		case 27: return "Rubinetto";
		case 28: return "Doccia";
		case 29: return "Televisore";
		case 30: return "Telecomando";
		case 31: return "Router WiFi";
		case 32: return "Ricevitore audio";
		case 33: return "Set-top box TV";
		case 34: return "Chiavetta streaming TV";
		default: return nullptr;
	}
}

// Matter device-type identifiers (the "DT" TXT value), the common ones.
const char* MatterDeviceType(long dt)
{
	switch (dt) {
		case 0x0100: return "Lampadina on/off";
		case 0x0101: return "Lampadina dimmerabile";
		case 0x010C: return "Lampadina bianco variabile";
		case 0x010D: return "Lampadina RGB";
		case 0x010A: return "Presa on/off";
		case 0x010B: return "Presa dimmerabile";
		case 0x0103: return "Interruttore luce on/off";
		case 0x0104: return "Interruttore dimmer";
		case 0x0105: return "Interruttore colore";
		case 0x0106: return "Sensore di luce";
		case 0x0107: return "Sensore di presenza";
		case 0x000A: return "Serratura";
		case 0x000E: return "Aggregatore / bridge";
		case 0x0015: return "Sensore di contatto";
		case 0x0016: return "Nodo radice";
		case 0x002B: return "Ventilatore";
		case 0x002D: return "Purificatore d'aria";
		case 0x0041: return "Tenda motorizzata";
		case 0x0301: return "Termostato";
		case 0x0302: return "Sensore di temperatura";
		case 0x0306: return "Sensore di flusso";
		case 0x0307: return "Sensore di umidita";
		case 0x000F: return "Interruttore generico";
		case 0x0013: return "Nodo bridge";
		case 0x0022: return "Altoparlante";
		case 0x0023: return "Lettore video (casting)";
		case 0x0024: return "App contenuti";
		case 0x0027: return "Selettore modalita";
		case 0x0028: return "Lettore video";
		case 0x0029: return "Client video casting";
		case 0x002A: return "Telecomando video";
		default:     return nullptr;
	}
}

// Matter test/development vendor IDs (0xFFF1..0xFFF4), reserved by the CSA for prototypes.
bool IsMatterTestVendor(long vendorId)
{
	return vendorId >= 0xFFF1 && vendorId <= 0xFFF4;
}

} // namespace

std::string DeriveServiceLabel(const std::string& type)
{
	std::string s = type;
	if (EndsWith(s, ".local"))
		s.erase(s.size() - 6);
	if (EndsWith(s, "._tcp"))
		s.erase(s.size() - 5);
	else if (EndsWith(s, "._udp"))
		s.erase(s.size() - 5);
	if (!s.empty() && s.front() == '_')
		s.erase(s.begin());
	return s.empty() ? type : s;
}

ServiceInfo LookupService(const std::string& type)
{
	for (const KnownService& k : kKnown)
		if (type == k.type)
			return {k.label, k.category};
	return {DeriveServiceLabel(type), "Altro"};
}

std::string TxtKeyLabel(const std::string& serviceType, const std::string& key)
{
	// Service-specific keys first.
	if (serviceType == "_hap._tcp.local") {
		if (key == "md") return "Modello";
		if (key == "ci") return "Categoria";
		if (key == "sf") return "Stato abbinamento";
		if (key == "pv") return "Versione protocollo";
		if (key == "id") return "ID accessorio";
		if (key == "c#") return "Numero configurazione";
		if (key == "s#") return "Numero stato";
		if (key == "ff") return "Flag funzionalita";
		if (key == "sh") return "Hash configurazione";
	}
	if (IsAmznWplay(serviceType)) {
		if (key == "n")  return "Nome";
		if (key == "c")  return "Indirizzo MAC";
		if (key == "ad") return "Seriale dispositivo";
		if (key == "u")  return "ID univoco";
		if (key == "sp") return "Porta sicura";
		if (key == "tr") return "Trasporto";
		if (key == "pv") return "Versione protocollo";
		if (key == "dpv") return "Versione protocollo dati";
		if (key == "v")  return "Versione";
		if (key == "mv") return "Versione modello";
	}
	if (IsMatter(serviceType)) {
		if (key == "VP") return "Vendor / Prodotto";
		if (key == "DT") return "Tipo dispositivo";
		if (key == "DN") return "Nome dispositivo";
		if (key == "CM") return "Modo abbinamento";
		if (key == "D")  return "Discriminatore";
		if (key == "PH") return "Suggerimento abbinamento";
		if (key == "PI") return "Istruzioni abbinamento";
	}
	if (serviceType == "_dkapi._tcp.local") {
		if (key == "type") return "Tipo";
		if (key == "reg") return "Regione";
		if (key == "ver") return "Versione firmware";
		if (key == "adp_kind") return "Tipo adattatore";
		if (key == "adp_mode") return "Modo adattatore";
		if (key == "pow") return "Accensione";
		if (key == "led") return "LED";
	}
	if (serviceType == "_esphomelib._tcp.local") {
		if (key == "project_name") return "Progetto";
		if (key == "project_version") return "Versione progetto";
		if (key == "version") return "Versione ESPHome";
		if (key == "board") return "Scheda";
		if (key == "platform") return "Piattaforma";
		if (key == "network") return "Rete";
		if (key == "mac") return "Indirizzo MAC";
	}
	if (serviceType == "_eero._tcp.local") {
		if (key == "base_mac") return "MAC base";
	}
	// Generic keys shared across many services.
	if (key == "md" || key == "model" || key == "ty" || key == "usb_MDL") return "Modello";
	if (key == "fn") return "Nome";
	if (key == "usb_MFG") return "Produttore";
	if (key == "note") return "Posizione";
	if (key == "rp") return "Percorso";
	if (key == "pdl") return "Formati";
	if (key == "deviceid") return "ID dispositivo";
	if (key == "n") return "Nome";
	if (key == "srcvers" || key == "ve" || key == "vs") return "Versione";
	if (key == "bridgeid") return "ID bridge";
	if (key == "modelid") return "ID modello";
	if (key == "osxvers") return "Versione macOS";
	if (key == "rs") return "Stato";
	return key;
}

std::string DecodeTxtValue(const std::string& serviceType, const std::string& key,
	const std::string& value)
{
	if (serviceType == "_hap._tcp.local") {
		if (key == "ci") {
			long ci = 0;
			if (ParseInt(value, ci)) {
				const char* name = HomeKitCategory(ci);
				if (name != nullptr) {
					char buf[64];
					std::snprintf(buf, sizeof(buf), "%s (%ld)", name, ci);
					return buf;
				}
			}
		} else if (key == "sf") {
			long sf = 0;
			if (ParseInt(value, sf))
				return (sf & 1) ? "non abbinato (visibile)" : "abbinato";
		} else if (key == "ff") {
			long ff = 0;
			if (ParseInt(value, ff)) {
				std::string s;
				if (ff & 1) s += "coprocessore MFi";
				if (ff & 2) { if (!s.empty()) s += ", "; s += "autenticazione software"; }
				if (s.empty()) s = "nessuna";
				char buf[96];
				std::snprintf(buf, sizeof(buf), "%s (%ld)", s.c_str(), ff);
				return buf;
			}
		}
	}
	if (IsMatter(serviceType)) {
		if (key == "DT") {
			long dt = 0;
			if (ParseInt(value, dt)) {
				const char* name = MatterDeviceType(dt);
				if (name != nullptr) {
					char buf[64];
					std::snprintf(buf, sizeof(buf), "%s (%ld)", name, dt);
					return buf;
				}
			}
		} else if (key == "VP") {
			// "vendorId+productId"
			size_t plus = value.find('+');
			if (plus != std::string::npos) {
				std::string vendor = value.substr(0, plus);
				std::string product = value.substr(plus + 1);
				long vid = 0;
				const char* note = (ParseInt(vendor, vid) && IsMatterTestVendor(vid))
					? " (vendor di test)" : "";
				char buf[128];
				std::snprintf(buf, sizeof(buf), "Vendor %s, Prodotto %s%s", vendor.c_str(),
					product.c_str(), note);
				return buf;
			}
		}
	}
	return value;
}

std::string InstanceSummary(const std::string& serviceType,
	const std::vector<std::pair<std::string, std::string>>& txt)
{
	std::string v;
	// HomeKit: model plus decoded category.
	if (serviceType == "_hap._tcp.local") {
		std::string model, ci;
		TxtGet(txt, "md", model);
		std::string cat;
		if (TxtGet(txt, "ci", ci))
			cat = DecodeTxtValue(serviceType, "ci", ci);
		if (!model.empty() && !cat.empty())
			return model + " - " + cat;
		if (!model.empty())
			return model;
		if (!cat.empty())
			return cat;
	}
	// Matter: device name and/or decoded device type.
	if (IsMatter(serviceType)) {
		std::string dn;
		TxtGet(txt, "DN", dn);
		std::string decodedDt, dt;
		if (TxtGet(txt, "DT", dt)) {
			decodedDt = DecodeTxtValue(serviceType, "DT", dt);
			if (decodedDt == dt) // not recognized
				decodedDt.clear();
		}
		if (!dn.empty() && !decodedDt.empty())
			return dn + " - " + decodedDt;
		if (!decodedDt.empty())
			return decodedDt;
		if (!dn.empty())
			return dn;
	}
	// ESPHome: the firmware project name is the most descriptive label.
	if (serviceType == "_esphomelib._tcp.local") {
		std::string proj;
		if (TxtGet(txt, "project_name", proj) && !proj.empty())
			return proj;
	}
	// Cast / generic: friendly name and/or model.
	if (TxtGet(txt, "fn", v) && !v.empty()) {
		std::string model;
		if (TxtGet(txt, "md", model) && !model.empty())
			return v + " (" + model + ")";
		return v;
	}
	if (TxtGet(txt, "md", v) && !v.empty())
		return v;
	if (TxtGet(txt, "ty", v) && !v.empty()) // printer make/model
		return v;
	if (TxtGet(txt, "model", v) && !v.empty())
		return v;
	if (TxtGet(txt, "n", v) && !v.empty()) // Amazon Whisperplay and similar
		return v;
	return "";
}

std::string BuildRadarReport(const RadarSnapshot& snap)
{
	std::string r;
	char line[768];

	std::string iface = snap.interfaceIp.empty() ? "automatica" : snap.interfaceIp;
	long seconds = static_cast<long>((snap.nowMs - snap.startMs) / 1000);
	if (seconds < 0)
		seconds = 0;

	r += "Campiello Radar - report di rete (mDNS/DNS-SD)\n";
	std::snprintf(line, sizeof(line), "Interfaccia: %s\n", iface.c_str());
	r += line;
	std::snprintf(line, sizeof(line),
		"Pacchetti: %llu da %zu host, %llu record (%llu scartati), cattura di %lds\n\n",
		(unsigned long long)snap.totalPackets, snap.sources.size(),
		(unsigned long long)snap.totalRecords, (unsigned long long)snap.droppedPackets, seconds);
	r += line;

	// Correlate hosts to the devices they advertise.
	std::map<std::string, std::set<std::string>> hostDevices;
	for (const RadarInstance& inst : snap.instances) {
		ServiceInfo si = LookupService(inst.type);
		for (const std::string& addr : inst.addrs)
			hostDevices[addr].insert(si.label);
	}

	std::snprintf(line, sizeof(line), "HOST CHE TRASMETTONO (%zu)\n", snap.sources.size());
	r += line;
	for (const RadarSource& src : snap.sources) {
		std::string devices;
		auto it = hostDevices.find(src.ip);
		if (it != hostDevices.end()) {
			for (const std::string& d : it->second) {
				if (!devices.empty())
					devices += ", ";
				devices += d;
			}
		}
		if (devices.empty())
			std::snprintf(line, sizeof(line), "  %s - %llu pacchetti, %llu record\n",
				src.ip.c_str(), (unsigned long long)src.packets,
				(unsigned long long)src.records);
		else
			std::snprintf(line, sizeof(line), "  %s - %s - %llu pacchetti, %llu record\n",
				src.ip.c_str(), devices.c_str(), (unsigned long long)src.packets,
				(unsigned long long)src.records);
		r += line;
	}

	std::snprintf(line, sizeof(line), "\nSERVIZI (%zu)\n", snap.services.size());
	r += line;
	for (const RadarService& svc : snap.services) {
		ServiceInfo si = LookupService(svc.type);
		std::string header = (si.category == "Altro" || si.category == si.label)
			? si.label : (si.category + " - " + si.label);
		std::snprintf(line, sizeof(line), "  %s  [%s]  (%zu istanze)\n", header.c_str(),
			svc.type.c_str(), svc.instances);
		r += line;

		if (!svc.subtypes.empty()) {
			std::string subs;
			for (const std::string& s : svc.subtypes) {
				if (!subs.empty())
					subs += ", ";
				subs += s;
			}
			std::snprintf(line, sizeof(line), "    sottotipi: %s\n", subs.c_str());
			r += line;
		}

		for (const RadarInstance& inst : snap.instances) {
			if (inst.type != svc.type)
				continue;
			std::string addr = inst.addrs.empty() ? inst.host : inst.addrs[0];
			std::string summary = InstanceSummary(inst.type, inst.txt);
			if (summary.empty())
				std::snprintf(line, sizeof(line), "    %s  @ %s:%u\n", inst.name.c_str(),
					addr.c_str(), (unsigned)inst.port);
			else
				std::snprintf(line, sizeof(line), "    %s  @ %s:%u  [%s]\n", inst.name.c_str(),
					addr.c_str(), (unsigned)inst.port, summary.c_str());
			r += line;

			for (const auto& kv : inst.txt) {
				std::string label = TxtKeyLabel(inst.type, kv.first);
				std::string value = DecodeTxtValue(inst.type, kv.first, kv.second);
				if (value.empty())
					std::snprintf(line, sizeof(line), "      %s\n", label.c_str());
				else
					std::snprintf(line, sizeof(line), "      %s = %s\n", label.c_str(),
						value.c_str());
				r += line;
			}
		}
	}
	return r;
}

} // namespace mdns
} // namespace bricola
} // namespace campiello
