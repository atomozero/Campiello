// test_dispatch.cpp
//
// Exercises the request/response dispatcher over loopback: a synchronous client gets
// correctly-correlated replies (HELLO->WELCOME, LIST->listing), an unhandled request comes
// back as an ERROR, request_ids auto-increment, and several clients are served concurrently
// (one server thread per connection). Plain Connection transport, std::thread; no framework.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../../src/traghetto/dispatch/Dispatch.h"
#include "../../src/traghetto/transport/Connection.h"
#include "../../src/traghetto/wire/Error.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/Handshake.h"
#include "../../src/traghetto/wire/Listing.h"

using namespace campiello;
using campiello::net::Connection;
using campiello::net::Listener;
using campiello::net::Client;
using campiello::net::ServeConnection;

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

// Answers HELLO with WELCOME and LIST with a one-entry listing; anything else is an ERROR.
// Stateless apart from the server identity, so it is safe to share across server threads.
class TestHandler : public net::RequestHandler {
public:
	wire::NodeIdentity serverId;

	wire::Frame Handle(const wire::Frame& request) override
	{
		switch (request.type) {
			case wire::MessageType::kHello:
				return wire::MakeWelcome(serverId, 0);
			case wire::MessageType::kList: {
				wire::Entry e;
				e.name = "uno.txt";
				e.stat.mode = 0x81A4;
				e.stat.size = 3;
				return wire::MakeListReply({ e }, 0);
			}
			default:
				return wire::MakeError(wire::ErrorCode::kUnsupported, "unsupported", 0);
		}
	}
};

static void TestSynchronous(TestHandler& handler)
{
	Listener listener;
	CHECK(listener.Listen("127.0.0.1", 0));
	uint16_t port = listener.Port();

	std::thread server([&]() {
		Connection conn;
		if (listener.Accept(conn))
			ServeConnection(conn, handler);
	});

	Connection clientConn;
	CHECK(Connect("127.0.0.1", port, clientConn));
	Client rpc(clientConn);

	// HELLO -> WELCOME
	wire::Frame reply;
	CHECK(rpc.Request(wire::MakeHello(MakeId("Ada"), 0), reply));
	CHECK(reply.type == wire::MessageType::kWelcome);
	CHECK(reply.requestId == rpc.LastRequestId());
	wire::NodeIdentity got;
	CHECK(wire::DecodeNodeIdentity(reply.payload, got));
	CHECK(got.node == "Berto");

	// LIST -> listing
	CHECK(rpc.Request(wire::MakeListRequest("/", 0), reply));
	CHECK(reply.type == wire::MessageType::kList);
	std::vector<wire::Entry> entries;
	CHECK(wire::DecodeListing(reply.payload, entries));
	CHECK(entries.size() == 1 && entries[0].name == "uno.txt");

	// Unhandled (STAT) -> ERROR
	CHECK(rpc.Request(wire::MakeStatRequest("/x", 0), reply));
	CHECK(reply.type == wire::MessageType::kError);
	wire::ErrorReply err;
	CHECK(wire::DecodeError(reply.payload, err));
	CHECK(err.code == (uint32_t)wire::ErrorCode::kUnsupported);

	// request_ids incremented across the three calls.
	CHECK(rpc.LastRequestId() == 3);

	clientConn.Close();
	server.join();
}

static void TestConcurrent(TestHandler& handler)
{
	Listener listener;
	CHECK(listener.Listen("127.0.0.1", 0));
	uint16_t port = listener.Port();

	const int kClients = 4;
	std::vector<std::thread> serverThreads;
	std::thread acceptor([&]() {
		for (int i = 0; i < kClients; i++) {
			Connection conn;
			if (!listener.Accept(conn))
				return;
			serverThreads.emplace_back(
				[conn = std::move(conn), &handler]() mutable {
					ServeConnection(conn, handler);
				});
		}
	});

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
	acceptor.join();
	for (auto& t : serverThreads)
		t.join();

	CHECK(okCount.load() == kClients);
}

int main()
{
	TestHandler handler;
	handler.serverId = MakeId("Berto");

	TestSynchronous(handler);
	TestConcurrent(handler);

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
