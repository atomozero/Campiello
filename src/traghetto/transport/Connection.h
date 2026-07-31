// Connection.h
//
// A message connection over a stream socket: send and receive whole CNP frames. This is
// the plain-TCP transport for Traghetto; TLS (OpenSSL mutual auth + SPKI pinning, decided
// in M0) will wrap this later. Pure POSIX sockets (no Haiku dependency), so it builds and
// its loopback test runs in CI on any platform, per docs/PROPOSAL.md section 15.
//
// Blocking I/O. One Connection is driven by one thread (the daemon runs a reader thread
// per peer, per docs/REUSE.md).

#ifndef CAMPIELLO_TRAGHETTO_TRANSPORT_CONNECTION_H
#define CAMPIELLO_TRAGHETTO_TRANSPORT_CONNECTION_H

#include <cstdint>

#include "../wire/Frame.h"
#include "../wire/FrameCodec.h"
#include "FrameChannel.h"

namespace campiello {
namespace net {

// Owns a connected socket fd; sends and receives framed messages.
class Connection : public FrameChannel {
public:
	Connection() = default;
	explicit Connection(int fd) : fFd(fd) {}
	~Connection();

	Connection(Connection&& other) noexcept;
	Connection& operator=(Connection&& other) noexcept;
	Connection(const Connection&) = delete;
	Connection& operator=(const Connection&) = delete;

	// Serialize and write a whole frame. Blocks until sent. False on I/O error.
	bool Send(const wire::Frame& frame) override;

	// Read until one whole frame is available. Blocks. False on EOF, I/O error, or a
	// protocol violation from the decoder (untrusted peer, docs/PROPOSAL.md rule 7).
	bool Receive(wire::Frame& out) override;

	bool IsOpen() const { return fFd >= 0; }
	void Close();
	void Shutdown() override;

	// Human-readable reason for the last failure, developer log only.
	const char* Error() const { return fError; }

private:
	bool ReadMore();

	int fFd = -1;
	wire::FrameParser fParser;
	const char* fError = nullptr;
};

// Low-level: open a TCP connection to host:port (numeric IPv4), returning a raw socket fd
// or -1 on failure. Shared with the TLS layer, which wraps the fd in an SSL.
int TcpConnect(const char* host, uint16_t port);

// Connect to host:port (host as a numeric IPv4 string, e.g. "127.0.0.1"). On success
// fills `out` with an open Connection and returns true.
bool Connect(const char* host, uint16_t port, Connection& out);

// A listening socket that accepts incoming connections.
class Listener {
public:
	Listener() = default;
	~Listener();
	Listener(const Listener&) = delete;
	Listener& operator=(const Listener&) = delete;

	// Bind and listen on host:port. Pass port 0 to get an ephemeral port (read it back
	// with Port()). host is a numeric IPv4 string, e.g. "127.0.0.1".
	bool Listen(const char* host, uint16_t port);

	// Accept one connection (blocks). False on error.
	bool Accept(Connection& out);

	// Low-level: accept and return the raw socket fd (-1 on error). For the TLS layer.
	int AcceptRawFd();

	// The actually-bound port (useful when Listen was given port 0).
	uint16_t Port() const;

	bool IsOpen() const { return fFd >= 0; }
	void Close();

private:
	int fFd = -1;
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_TRANSPORT_CONNECTION_H
