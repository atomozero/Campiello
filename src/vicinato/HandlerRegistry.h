// HandlerRegistry.h
//
// The device add-on framework (docs/DEVICE_ADDONS.md). A "device handler" is an optional,
// separately installed app that interacts with a class of discovered devices (Philips Hue lights,
// a Fire TV remote, an IPP printer). Each handler ships a small manifest declaring which devices it
// handles (by mDNS service type and/or ServiceKind) and which actions it offers. The registry loads
// those manifests and matches them to discovered NetworkServices, so the WON folder/list can offer
// the right action per device.
//
// Pure data + std only (no Haiku, no vendor SDK): the matching is unit-testable, and the MIT core
// never depends on any device add-on. Handlers are launched as separate apps by signature.

#ifndef CAMPIELLO_VICINATO_HANDLERREGISTRY_H
#define CAMPIELLO_VICINATO_HANDLERREGISTRY_H

#include <string>
#include <vector>

#include "NetworkDirectory.h"

namespace campiello {
namespace vicinato {

// One action a handler offers. The action with id "open" is the default (double-click) action.
struct HandlerAction {
	std::string id;
	std::string label;
};

// A device add-on, as declared by its manifest.
struct DeviceHandler {
	std::string                signature;   // app_signature to launch (be_roster)
	std::string                name;        // human-readable name
	std::vector<std::string>   matchTypes;  // mDNS service types it handles (e.g. "_hue._tcp")
	std::vector<ServiceKind>   matchKinds;  // ...or whole ServiceKinds
	std::vector<HandlerAction> actions;     // offered actions; "open" is the default
};

// Canonical lowercase kind name used in manifests (match.kind), and its inverse.
const char* KindName(ServiceKind k);
bool KindFromName(const std::string& name, ServiceKind& out);

// Parse one manifest's text into `out`. Recognized lines (whitespace-tolerant, '#' starts a
// comment): "signature = ...", "name = ...", "match.type = ...", "match.kind = ...",
// "action.<id> = <label>". Returns true if a signature was found. Pure and testable.
bool ParseHandlerManifest(const std::string& text, DeviceHandler& out);

class HandlerRegistry {
public:
	void Add(const DeviceHandler& handler) { fHandlers.push_back(handler); }

	// Load every "*.handler" manifest from `dir` (best-effort; missing dir is fine).
	void LoadFromDir(const std::string& dir);

	// Handlers that match a discovered service (its serviceType is in matchTypes, or its kind is in
	// matchKinds), in registration order.
	std::vector<const DeviceHandler*> Match(const NetworkService& service) const;

	// The default handler for a service (the first match), or nullptr if none.
	const DeviceHandler* DefaultHandler(const NetworkService& service) const;

	size_t Count() const { return fHandlers.size(); }

private:
	std::vector<DeviceHandler> fHandlers;
};

} // namespace vicinato
} // namespace campiello

#endif // CAMPIELLO_VICINATO_HANDLERREGISTRY_H
