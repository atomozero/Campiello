// TlsConnection.h
//
// TLS 1.3, mutually authenticated, with SubjectPublicKeyInfo pinning: the native-mode
// secure transport. It wraps a socket in an OpenSSL SSL and exposes the same Send(Frame) /
// Receive(Frame) interface as the plain Connection. There is no CA; after the handshake the
// peer's SPKI fingerprint is checked against a pinned value (or captured on trust-on-first-
// use). This is why BSecureSocket was rejected in M0 (docs/VERIFIED.md section 6).
//
// Built on OpenSSL 3 (Apache-2.0) and the plain-TCP helpers in the transport layer.

#ifndef CAMPIELLO_TRAGHETTO_TLS_TLSCONNECTION_H
#define CAMPIELLO_TRAGHETTO_TLS_TLSCONNECTION_H

#include <cstdint>

#include <openssl/ssl.h>

#include "../transport/FrameChannel.h"
#include "../wire/Frame.h"
#include "../wire/FrameCodec.h"
#include "Identity.h"

namespace campiello {
namespace net {

// An SSL_CTX built from a node's Identity, requiring a peer certificate (mutual auth) and
// forcing TLS 1.3. One context can back many connections.
class TlsContext {
public:
	TlsContext() = default;
	~TlsContext();
	TlsContext(const TlsContext&) = delete;
	TlsContext& operator=(const TlsContext&) = delete;

	// Build the context from `identity`. `server` selects the server or client role.
	bool Init(const Identity& identity, bool server);

	SSL_CTX* Raw() const { return fCtx; }
	bool IsValid() const { return fCtx != nullptr; }

private:
	SSL_CTX* fCtx = nullptr;
};

class TlsConnection : public FrameChannel {
public:
	TlsConnection() = default;
	~TlsConnection();
	TlsConnection(TlsConnection&& other) noexcept;
	TlsConnection& operator=(TlsConnection&& other) noexcept;
	TlsConnection(const TlsConnection&) = delete;
	TlsConnection& operator=(const TlsConnection&) = delete;

	// Client: TCP-connect to host:port and run the TLS handshake. If `expectedPeer` is
	// non-null, the peer's SPKI fingerprint must equal it or the connection is refused
	// (pinning). If null, the peer's fingerprint is captured for trust-on-first-use and
	// available via PeerFingerprint().
	static bool Connect(TlsContext& ctx, const char* host, uint16_t port,
		const Fingerprint* expectedPeer, TlsConnection& out);

	// Server: run the TLS handshake over an already-accepted socket fd (from
	// Listener::AcceptRawFd). Same pinning/capture semantics. Takes ownership of `fd`.
	static bool Accept(TlsContext& ctx, int fd, const Fingerprint* expectedPeer,
		TlsConnection& out);

	bool Send(const wire::Frame& frame) override;
	bool Receive(wire::Frame& out) override;

	const Fingerprint& PeerFingerprint() const override { return fPeer; }
	bool IsOpen() const { return fSsl != nullptr; }
	void Close();
	void Shutdown() override;
	const char* Error() const { return fError; }

private:
	TlsConnection(SSL* ssl, int fd) : fSsl(ssl), fFd(fd) {}
	static bool Handshake(TlsContext& ctx, int fd, bool server,
		const Fingerprint* expectedPeer, TlsConnection& out);
	bool ReadMore();

	SSL* fSsl = nullptr;
	int  fFd = -1;
	Fingerprint fPeer{};
	wire::FrameParser fParser;
	const char* fError = nullptr;
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_TLS_TLSCONNECTION_H
