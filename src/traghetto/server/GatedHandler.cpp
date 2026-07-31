// GatedHandler.cpp
//
// Implementation of the server-side pairing gate. See GatedHandler.h.

#include "GatedHandler.h"

#include "../wire/Error.h"
#include "../wire/Handshake.h"

namespace campiello {
namespace net {

wire::Frame GatedHandler::Handle(const wire::Frame& request)
{
	if (!fAdmitted) {
		// The peer must pair before doing anything else, so the first message has to be HELLO.
		if (request.type != wire::MessageType::kHello)
			return wire::MakeError(wire::ErrorCode::kAccessDenied, "", 0);

		wire::NodeIdentity id;
		if (!wire::DecodeNodeIdentity(request.payload, id))
			return wire::MakeError(wire::ErrorCode::kInvalidRequest, "", 0);

		// The identity the HELLO claims must be the key TLS actually authenticated. Otherwise
		// a peer could try to pair as (or impersonate) a different fingerprint than it holds.
		if (!Equals(id.fingerprint, fPeer))
			return wire::MakeError(wire::ErrorCode::kAccessDenied, "", 0);

		if (!fPairing.Admit(fPeer, id.node))
			return wire::MakeError(wire::ErrorCode::kAccessDenied, "", 0);

		fAdmitted = true;
	}

	return fInner->Handle(request);
}

std::unique_ptr<RequestHandler> GatedHandlerFactory::Create(const Fingerprint& peer)
{
	std::unique_ptr<RequestHandler> inner = fInner.Create(peer);
	if (!inner)
		return nullptr;
	return std::unique_ptr<RequestHandler>(
		new GatedHandler(peer, fPairing, std::move(inner)));
}

} // namespace net
} // namespace campiello
