// TlsConnection.cpp
//
// Implementation of the mutually-authenticated, SPKI-pinned TLS connection. See
// TlsConnection.h.

#include "TlsConnection.h"

#include <sys/socket.h>
#include <unistd.h>

#include "../transport/Connection.h" // TcpConnect

namespace campiello {
namespace net {

namespace {

// Accept any certificate at the TLS layer (including self-signed): trust is decided after
// the handshake by pinning the SPKI, not by CA chain validation.
int AcceptAnyCert(int /*preverify*/, X509_STORE_CTX* /*ctx*/)
{
	return 1;
}

} // namespace

TlsContext::~TlsContext()
{
	if (fCtx != nullptr)
		SSL_CTX_free(fCtx);
}

bool TlsContext::Init(const Identity& identity, bool server)
{
	if (!identity.IsValid())
		return false;
	fCtx = SSL_CTX_new(server ? TLS_server_method() : TLS_client_method());
	if (fCtx == nullptr)
		return false;

	SSL_CTX_set_min_proto_version(fCtx, TLS1_3_VERSION);
	SSL_CTX_set_max_proto_version(fCtx, TLS1_3_VERSION);

	if (SSL_CTX_use_certificate(fCtx, identity.Cert()) != 1)
		return false;
	if (SSL_CTX_use_PrivateKey(fCtx, identity.Key()) != 1)
		return false;

	// Require the peer to present a certificate (mutual auth); accept it at the TLS layer
	// and pin the SPKI afterwards.
	SSL_CTX_set_verify(fCtx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
		AcceptAnyCert);
	return true;
}

TlsConnection::~TlsConnection()
{
	Close();
}

TlsConnection::TlsConnection(TlsConnection&& other) noexcept
	: fSsl(other.fSsl), fFd(other.fFd), fPeer(other.fPeer),
	  fParser(std::move(other.fParser)), fError(other.fError)
{
	other.fSsl = nullptr;
	other.fFd = -1;
}

TlsConnection& TlsConnection::operator=(TlsConnection&& other) noexcept
{
	if (this != &other) {
		Close();
		fSsl = other.fSsl;
		fFd = other.fFd;
		fPeer = other.fPeer;
		fParser = std::move(other.fParser);
		fError = other.fError;
		other.fSsl = nullptr;
		other.fFd = -1;
	}
	return *this;
}

void TlsConnection::Close()
{
	if (fSsl != nullptr) {
		SSL_shutdown(fSsl);
		SSL_free(fSsl);
		fSsl = nullptr;
	}
	// SSL_set_fd uses BIO_NOCLOSE, so the fd is ours to close.
	if (fFd >= 0) {
		::close(fFd);
		fFd = -1;
	}
}

void TlsConnection::Shutdown()
{
	// Shut the socket down only (not the SSL object): safe to call from another thread to
	// unblock a Receive() blocked in SSL_read on this connection.
	if (fFd >= 0)
		::shutdown(fFd, SHUT_RDWR);
}

bool TlsConnection::Handshake(TlsContext& ctx, int fd, bool server,
	const Fingerprint* expectedPeer, TlsConnection& out)
{
	if (!ctx.IsValid()) {
		::close(fd);
		return false;
	}
	SSL* ssl = SSL_new(ctx.Raw());
	if (ssl == nullptr) {
		::close(fd);
		return false;
	}
	SSL_set_fd(ssl, fd);

	int result = server ? SSL_accept(ssl) : SSL_connect(ssl);
	if (result != 1) {
		SSL_free(ssl);
		::close(fd);
		return false;
	}

	// Pin the peer by its SPKI fingerprint.
	X509* peer = SSL_get1_peer_certificate(ssl);
	if (peer == nullptr) {
		SSL_free(ssl);
		::close(fd);
		return false;
	}
	Fingerprint peerFp{};
	bool haveFp = FingerprintOfCert(peer, peerFp);
	X509_free(peer);
	if (!haveFp) {
		SSL_free(ssl);
		::close(fd);
		return false;
	}
	if (expectedPeer != nullptr && !(peerFp == *expectedPeer)) {
		// Pinning mismatch: a changed key is a stranger (docs/PROTOCOL.md trust model).
		SSL_free(ssl);
		::close(fd);
		return false;
	}

	out = TlsConnection(ssl, fd);
	out.fPeer = peerFp;
	return true;
}

bool TlsConnection::Connect(TlsContext& ctx, const char* host, uint16_t port,
	const Fingerprint* expectedPeer, TlsConnection& out)
{
	int fd = TcpConnect(host, port);
	if (fd < 0)
		return false;
	return Handshake(ctx, fd, /*server=*/false, expectedPeer, out);
}

bool TlsConnection::Accept(TlsContext& ctx, int fd, const Fingerprint* expectedPeer,
	TlsConnection& out)
{
	if (fd < 0)
		return false;
	return Handshake(ctx, fd, /*server=*/true, expectedPeer, out);
}

bool TlsConnection::Send(const wire::Frame& frame)
{
	if (fSsl == nullptr) {
		fError = "connection closed";
		return false;
	}
	std::vector<uint8_t> bytes;
	if (!wire::EncodeFrame(frame, bytes)) {
		fError = "frame payload too large to encode";
		return false;
	}
	size_t offset = 0;
	while (offset < bytes.size()) {
		int n = SSL_write(fSsl, bytes.data() + offset, (int)(bytes.size() - offset));
		if (n <= 0) {
			fError = "TLS write error";
			return false;
		}
		offset += (size_t)n;
	}
	return true;
}

bool TlsConnection::ReadMore()
{
	uint8_t buffer[4096];
	int n = SSL_read(fSsl, buffer, sizeof(buffer));
	if (n <= 0) {
		fError = "TLS read error or connection closed";
		return false;
	}
	fParser.Feed(buffer, (size_t)n);
	return true;
}

bool TlsConnection::Receive(wire::Frame& out)
{
	if (fSsl == nullptr) {
		fError = "connection closed";
		return false;
	}
	for (;;) {
		wire::ParseResult result = fParser.Next(out);
		if (result == wire::ParseResult::kFrame)
			return true;
		if (result == wire::ParseResult::kError) {
			fError = fParser.ErrorMessage();
			return false;
		}
		if (!ReadMore())
			return false;
	}
}

} // namespace net
} // namespace campiello
