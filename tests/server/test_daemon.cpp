// test_daemon.cpp
//
// Exercises the multi-peer daemon: a plain daemon serves several concurrent clients and
// then shuts down cleanly (no hang), and a TLS daemon serves a pinned client. Uses the
// dispatcher's Client and a minimal handler. OpenSSL + POSIX sockets + std::thread.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../../src/traghetto/dispatch/Dispatch.h"
#include "../../src/traghetto/server/Daemon.h"
#include "../../src/traghetto/tls/Identity.h"
#include "../../src/traghetto/tls/TlsConnection.h"
#include "../../src/traghetto/transport/Connection.h"
#include "../../src/traghetto/wire/Error.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/Handshake.h"

using namespace campiello;
using campiello::net::Connection;
using campiello::net::Listener;
using campiello::net::Client;
using campiello::net::Daemon;
using campiello::net::PlainChannelFactory;
using campiello::net::TlsChannelFactory;
using campiello::net::TlsContext;
using campiello::net::TlsConnection;
using campiello::net::Identity;
using campiello::net::Fingerprint;

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

static wire::NodeIdentity MakeId(const std::string& name)
{
	wire::NodeIdentity id;
	id.version = 1;
	id.node = name;
	id.caps = { wire::kCapBfs };
	id.fingerprint = std::vector<uint8_t>(wire::kFingerprintBytes, 0x11);
	return id;
}

class MinimalHandler : public net::RequestHandler {
public:
	wire::NodeIdentity serverId;

	wire::Frame Handle(const wire::Frame& request) override
	{
		if (request.type == wire::MessageType::kHello)
			return wire::MakeWelcome(serverId, 0);
		return wire::MakeError(wire::ErrorCode::kUnsupported, "", 0);
	}
};

// Produces a fresh MinimalHandler per connection, each carrying the server identity.
class MinimalHandlerFactory : public net::HandlerFactory {
public:
	wire::NodeIdentity serverId;

	std::unique_ptr<net::RequestHandler> Create(const net::Fingerprint&) override
	{
		auto handler = std::unique_ptr<MinimalHandler>(new MinimalHandler());
		handler->serverId = serverId;
		return handler;
	}
};

static void TestPlainDaemonConcurrentThenCleanStop(MinimalHandlerFactory& factory)
{
	Listener listener;
	CHECK(listener.Listen("127.0.0.1", 0));
	uint16_t port = listener.Port();

	PlainChannelFactory channelFactory;
	Daemon daemon(listener, channelFactory, factory);
	CHECK(daemon.Start());

	const int kClients = 4;
	std::atomic<int> okCount{0};
	std::vector<std::thread> clients;
	for (int i = 0; i < kClients; i++) {
		clients.emplace_back([&]() {
			Connection conn;
			if (!Connect("127.0.0.1", port, conn))
				return;
			Client rpc(conn);
			wire::Frame reply;
			if (rpc.Request(wire::MakeHello(MakeId("Cli"), 0), reply)
				&& reply.type == wire::MessageType::kWelcome) {
				okCount++;
			}
			conn.Close();
		});
	}
	for (auto& t : clients)
		t.join();

	daemon.Stop(); // must complete without hanging
	CHECK(okCount.load() == kClients);
}

static void TestTlsDaemonPinnedClient(MinimalHandlerFactory& handlerFactory)
{
	Identity serverId;
	Identity clientId;
	CHECK(Identity::Generate(serverId));
	CHECK(Identity::Generate(clientId));
	Fingerprint serverFp = serverId.GetFingerprint();

	TlsContext serverCtx;
	TlsContext clientCtx;
	CHECK(serverCtx.Init(serverId, /*server=*/true));
	CHECK(clientCtx.Init(clientId, /*server=*/false));

	Listener listener;
	CHECK(listener.Listen("127.0.0.1", 0));
	uint16_t port = listener.Port();

	TlsChannelFactory channelFactory(serverCtx);
	Daemon daemon(listener, channelFactory, handlerFactory);
	CHECK(daemon.Start());

	TlsConnection conn;
	CHECK(TlsConnection::Connect(clientCtx, "127.0.0.1", port, &serverFp, conn));
	Client rpc(conn);
	wire::Frame reply;
	CHECK(rpc.Request(wire::MakeHello(MakeId("Ada"), 0), reply));
	CHECK(reply.type == wire::MessageType::kWelcome);
	wire::NodeIdentity got;
	CHECK(wire::DecodeNodeIdentity(reply.payload, got));
	CHECK(got.node == "Berto");

	conn.Close();
	daemon.Stop();
}

int main()
{
	MinimalHandlerFactory factory;
	factory.serverId = MakeId("Berto");

	TestPlainDaemonConcurrentThenCleanStop(factory);
	TestTlsDaemonPinnedClient(factory);

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
