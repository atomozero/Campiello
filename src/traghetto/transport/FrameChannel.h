// FrameChannel.h
//
// The abstract send/receive-frames interface. Both the plain Connection and the TLS
// TlsConnection implement it, so the dispatcher (ServeConnection, Client) works over either
// transport. Native mode always uses TLS; the plain transport is for tests and interop
// scaffolding.

#ifndef CAMPIELLO_TRAGHETTO_TRANSPORT_FRAMECHANNEL_H
#define CAMPIELLO_TRAGHETTO_TRANSPORT_FRAMECHANNEL_H

#include "../tls/Fingerprint.h"
#include "../wire/Frame.h"

namespace campiello {
namespace net {

class FrameChannel {
public:
	virtual ~FrameChannel() = default;

	// Send one whole frame. Blocks until sent. False on error.
	virtual bool Send(const wire::Frame& frame) = 0;

	// Receive the next whole frame. Blocks. False on EOF, I/O error, or a protocol
	// violation from the decoder.
	virtual bool Receive(wire::Frame& out) = 0;

	// Unblock a blocked Receive from another thread (shuts the socket down for I/O). Default
	// is a no-op; socket-backed channels override it. Safe to call concurrently with a
	// Receive in progress on another thread: it touches only the socket, not any SSL state.
	virtual void Shutdown() {}

	// The SPKI fingerprint of the authenticated peer, captured during the TLS handshake.
	// The TLS transport overrides this; the plain (unauthenticated) transport returns an
	// all-zero fingerprint, which carries no identity and pairing must never trust.
	virtual const Fingerprint& PeerFingerprint() const
	{
		static const Fingerprint kNone{};
		return kNone;
	}
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_TRANSPORT_FRAMECHANNEL_H
