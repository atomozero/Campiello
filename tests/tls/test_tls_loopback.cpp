// test_tls_loopback.cpp
//
// Exercises the mutually-authenticated, SPKI-pinned TLS transport over loopback: two
// identities pin each other, exchange an encrypted HELLO/WELCOME, and the CNP-layer
// fingerprint in the WELCOME matches the pinned TLS fingerprint. A negative case checks
// that a wrong pin is refused. OpenSSL + POSIX sockets + std::thread; no framework.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../../src/traghetto/tls/Identity.h"
#include "../../src/traghetto/tls/TlsConnection.h"
#include "../../src/traghetto/transport/Connection.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/Handshake.h"

using namespace campiello;
using campiello::net::Identity;
using campiello::net::Fingerprint;
using campiello::net::Listener;
using campiello::net::TlsContext;
using campiello::net::TlsConnection;

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                            \
	do {                                                                       \
		++gChecks;                                                             \
		if (!(cond)) {                                                         \
			++gFailures;                                                       \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
		}                                                                      \
	} while (0)

// Build a CNP-layer NodeIdentity whose fingerprint mirrors the TLS SPKI fingerprint.
static wire::NodeIdentity MakeWireId(const std::string& name, const Fingerprint& fp)
{
	wire::NodeIdentity id;
	id.version = 1;
	id.node = name;
	id.caps = { wire::kCapBfs };
	id.fingerprint.assign(fp.begin(), fp.end());
	return id;
}

struct ServerResult {
	bool accepted = false;
	bool served = false;
	Fingerprint peerFp{};
};

// Accept one TLS connection (optionally pinning the client), and if `welcome` is given,
// answer a HELLO with a WELCOME.
static void ServerRun(Listener* listener, TlsContext* ctx, const Fingerprint* pin,
	const wire::NodeIdentity* welcome, ServerResult* result)
{
	int fd = listener->AcceptRawFd();
	if (fd < 0)
		return;
	TlsConnection conn;
	if (!TlsConnection::Accept(*ctx, fd, pin, conn))
		return;
	result->accepted = true;
	result->peerFp = conn.PeerFingerprint();

	if (welcome != nullptr) {
		wire::Frame req;
		if (!conn.Receive(req) || req.type != wire::MessageType::kHello)
			return;
		if (!conn.Send(wire::MakeWelcome(*welcome, req.requestId)))
			return;
		result->served = true;
	}
}

static void TestMutualAuthAndPinning(const Identity& serverId, const Identity& clientId,
	TlsContext& serverCtx, TlsContext& clientCtx)
{
	Fingerprint serverFp = serverId.GetFingerprint();
	Fingerprint clientFp = clientId.GetFingerprint();

	Listener listener;
	CHECK(listener.Listen("127.0.0.1", 0));
	uint16_t port = listener.Port();
	CHECK(port != 0);

	wire::NodeIdentity welcome = MakeWireId("Berto", serverFp);
	ServerResult sr;
	std::thread server(ServerRun, &listener, &serverCtx, &clientFp, &welcome, &sr);

	// Client pins the server.
	TlsConnection conn;
	CHECK(TlsConnection::Connect(clientCtx, "127.0.0.1", port, &serverFp, conn));
	CHECK(conn.PeerFingerprint() == serverFp);

	CHECK(conn.Send(wire::MakeHello(MakeWireId("Ada", clientFp), 7)));
	wire::Frame reply;
	CHECK(conn.Receive(reply));
	CHECK(reply.type == wire::MessageType::kWelcome);
	CHECK(reply.requestId == 7);

	wire::NodeIdentity got;
	CHECK(wire::DecodeNodeIdentity(reply.payload, got));
	CHECK(got.node == "Berto");
	// The CNP-layer fingerprint in the WELCOME must equal the pinned TLS fingerprint.
	Fingerprint wireFp{};
	CHECK(got.fingerprint.size() == wireFp.size());
	std::copy(got.fingerprint.begin(), got.fingerprint.end(), wireFp.begin());
	CHECK(wireFp == serverFp);

	conn.Close();
	server.join();
	CHECK(sr.accepted);
	CHECK(sr.served);
	CHECK(sr.peerFp == clientFp); // the server pinned the client's real fingerprint
}

static void TestWrongPinRefused(const Identity& serverId, TlsContext& serverCtx,
	TlsContext& clientCtx)
{
	Listener listener;
	CHECK(listener.Listen("127.0.0.1", 0));
	uint16_t port = listener.Port();

	ServerResult sr;
	std::thread server(ServerRun, &listener, &serverCtx,
		(const Fingerprint*)nullptr, (const wire::NodeIdentity*)nullptr, &sr);

	// Expect the wrong server fingerprint: the handshake completes but the pin check fails.
	Fingerprint wrong = serverId.GetFingerprint();
	wrong[0] ^= 0xFF;
	TlsConnection bad;
	CHECK(!TlsConnection::Connect(clientCtx, "127.0.0.1", port, &wrong, bad));
	CHECK(!bad.IsOpen());

	server.join();
	CHECK(sr.accepted); // server side handshakes fine; the rejection is on the client
}

int main()
{
	Identity serverId;
	Identity clientId;
	CHECK(Identity::Generate(serverId));
	CHECK(Identity::Generate(clientId));

	TlsContext serverCtx;
	TlsContext clientCtx;
	CHECK(serverCtx.Init(serverId, /*server=*/true));
	CHECK(clientCtx.Init(clientId, /*server=*/false));

	TestMutualAuthAndPinning(serverId, clientId, serverCtx, clientCtx);
	TestWrongPinRefused(serverId, serverCtx, clientCtx);

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
