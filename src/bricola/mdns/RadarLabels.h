// RadarLabels.h
//
// Human-readable interpretation of what the radar sees: it turns DNS-SD service types and their
// TXT attributes into legible Italian (a Hue bridge, a Matter dimmable light, a printer model),
// and renders a whole snapshot as a plain-text report for the export button.
//
// This is the knowledge layer of the radar debug tool (docs/RADAR.md): the service catalog, the
// TXT key/value decoders (HomeKit accessory categories, Matter device types, common model/name
// fields), and the report writer. Kept as a portable module (pure standard C++, no BeAPI) so the
// decoding tables are unit-tested off Haiku; the GUI is a thin consumer.
//
// End-user strings are Italian (working agreement rule 4).

#ifndef CAMPIELLO_BRICOLA_MDNS_RADARLABELS_H
#define CAMPIELLO_BRICOLA_MDNS_RADARLABELS_H

#include <string>
#include <utility>
#include <vector>

#include "MdnsRadar.h"

namespace campiello {
namespace bricola {
namespace mdns {

// A human label and category for a service type.
struct ServiceInfo {
	std::string label;
	std::string category;
};

// Look up a service type in the known-service catalog; an unknown type falls back to a derived
// label with category "Altro".
ServiceInfo LookupService(const std::string& type);

// Derive a readable label from an unknown service type: "_matterd._udp.local" -> "matterd".
std::string DeriveServiceLabel(const std::string& type);

// A friendly Italian label for a TXT key, service-aware (e.g. HomeKit "ci" -> "Categoria",
// printer "ty" -> "Modello"). Falls back to the raw key.
std::string TxtKeyLabel(const std::string& serviceType, const std::string& key);

// Decode a coded TXT value into words where known (HomeKit category id -> "Lampadina", Matter
// device-type id -> "Lampadina dimmerabile", HomeKit sf -> "abbinato"...). Falls back to the raw
// value.
std::string DecodeTxtValue(const std::string& serviceType, const std::string& key,
	const std::string& value);

// A short "what is it" line derived from an instance's TXT (model + category), or "" if nothing
// recognizable. Used to annotate the instance row.
std::string InstanceSummary(const std::string& serviceType,
	const std::vector<std::pair<std::string, std::string>>& txt);

// A full plain-text report of a snapshot, for the export-to-file button.
std::string BuildRadarReport(const RadarSnapshot& snap);

} // namespace mdns
} // namespace bricola
} // namespace campiello

#endif // CAMPIELLO_BRICOLA_MDNS_RADARLABELS_H
