// BricolaDirectory.h
//
// The production PeerDirectory: turns Bricola's live peer snapshot into the endpoints the
// ConnectionManager connects to. Peer names are sanitized for the filesystem (no '/'), and only
// peers with a resolved address and port are offered. Bricola keeps Peers() thread-safe, so the
// FUSE callback thread can read it directly.
//
// Depends on Bricola (discovery); pure logic otherwise. The ConnectionManager stays decoupled
// from Bricola through the PeerDirectory interface (tests use a fake directory instead).

#ifndef CAMPIELLO_FONDAMENTA_DISCOVERY_BRICOLADIRECTORY_H
#define CAMPIELLO_FONDAMENTA_DISCOVERY_BRICOLADIRECTORY_H

#include <string>
#include <vector>

#include "../../bricola/mdns/Bricola.h"
#include "ConnectionManager.h" // PeerDirectory, PeerEndpoint

namespace campiello {
namespace fondamenta {

class BricolaDirectory : public PeerDirectory {
public:
	explicit BricolaDirectory(bricola::mdns::Bricola& bricola) : fBricola(bricola) {}

	std::vector<PeerEndpoint> Endpoints() override;

	// A filesystem-safe name from a peer's friendly label (fold '/', whitespace, control bytes
	// to '-'; drop leading dots). Exposed for testing.
	static std::string SanitizeName(const std::string& instance);

private:
	bricola::mdns::Bricola& fBricola;
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_DISCOVERY_BRICOLADIRECTORY_H
