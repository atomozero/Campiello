// GatedHandler.h
//
// The server-side pairing gate: a RequestHandler decorator that enforces trust before any
// real request reaches the inner handler (the FileServer). It sits between the dispatcher and
// the backend so the backend stays purely about files.
//
// The gate requires the peer's first message to be HELLO. It cross-checks the identity the
// HELLO claims (`fp`) against the fingerprint TLS actually authenticated, then asks Pairing
// whether to admit the peer (silent for a pinned peer, a one-tap prompt otherwise). Until the
// peer is admitted, every request is refused with kAccessDenied. After admission, requests
// pass straight through.
//
// Trust is by key, never by the advertised name (docs/PROPOSAL.md section 9): the name in
// HELLO is only a hint for the prompt.

#ifndef CAMPIELLO_TRAGHETTO_SERVER_GATEDHANDLER_H
#define CAMPIELLO_TRAGHETTO_SERVER_GATEDHANDLER_H

#include <memory>

#include "../dispatch/Dispatch.h"
#include "../tls/Fingerprint.h"
#include "../trust/Pairing.h"
#include "../wire/Frame.h"

namespace campiello {
namespace net {

class GatedHandler : public RequestHandler {
public:
	// `peer` is the SPKI fingerprint TLS authenticated for this connection. `pairing` must
	// outlive the handler (it is shared across peers). `inner` is the backend to serve once
	// the peer is admitted; it is owned by the gate.
	GatedHandler(const Fingerprint& peer, Pairing& pairing,
		std::unique_ptr<RequestHandler> inner)
		: fPeer(peer), fPairing(pairing), fInner(std::move(inner)) {}

	wire::Frame Handle(const wire::Frame& request) override;

private:
	Fingerprint fPeer;
	Pairing& fPairing;
	std::unique_ptr<RequestHandler> fInner;
	bool fAdmitted = false;
};

// Wraps another HandlerFactory's handlers in a pairing gate. In native mode the daemon is
// given one of these around a FileServerFactory, sharing a single Pairing (one TrustStore)
// across all peers.
class GatedHandlerFactory : public HandlerFactory {
public:
	// Both references must outlive the factory.
	GatedHandlerFactory(Pairing& pairing, HandlerFactory& inner)
		: fPairing(pairing), fInner(inner) {}

	std::unique_ptr<RequestHandler> Create(const Fingerprint& peer) override;

private:
	Pairing& fPairing;
	HandlerFactory& fInner;
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_SERVER_GATEDHANDLER_H
