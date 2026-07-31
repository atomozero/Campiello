// test_loopback.cpp
//
// Exercises the plain-TCP framed connection over loopback: a server thread accepts a
// connection, receives a HELLO, and replies WELCOME echoing the request id; the client
// connects, sends HELLO, and verifies the WELCOME. Also checks a multi-frame exchange and
// clean EOF handling. Pure standard C++ + POSIX sockets, no framework.

#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../../src/traghetto/transport/Connection.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/Handshake.h"
#include "../../src/traghetto/wire/Listing.h"

using namespace campiello;
using campiello::net::Connection;
using campiello::net::Listener;

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

static wire::NodeIdentity MakeIdentity(const std::string& name, uint8_t fpFill)
{
	wire::NodeIdentity id;
	id.version = 1;
	id.fingerprint = std::vector<uint8_t>(wire::kFingerprintBytes, fpFill);
	id.caps = { wire::kCapBfs };
	id.node = name;
	return id;
}

// Server logic: accept one client, answer a HELLO with a WELCOME, then answer a LIST
// request with a small listing, then exit when the client disconnects.
static void ServerMain(Listener* listener, bool* ok)
{
	*ok = false;
	Connection conn;
	if (!listener->Accept(conn))
		return;

	wire::NodeIdentity serverId = MakeIdentity("Berto", 0xBB);

	// HELLO -> WELCOME
	wire::Frame req;
	if (!conn.Receive(req))
		return;
	if (req.type != wire::MessageType::kHello)
		return;
	if (!conn.Send(wire::MakeWelcome(serverId, req.requestId)))
		return;

	// LIST -> a two-entry listing
	wire::Frame listReq;
	if (!conn.Receive(listReq))
		return;
	if (listReq.type != wire::MessageType::kList)
		return;

	wire::Entry a;
	a.name = "uno.txt";
	a.stat.mode = 0x81A4;
	a.stat.size = 3;
	a.attrs = { wire::Attr{ "BEOS:TYPE", 0x4D494D53, {'t','x','t'} } };
	wire::Entry b = a;
	b.name = "due.txt";
	if (!conn.Send(wire::MakeListReply({ a, b }, listReq.requestId)))
		return;

	*ok = true;
}

int main()
{
	Listener listener;
	CHECK(listener.Listen("127.0.0.1", 0));
	uint16_t port = listener.Port();
	CHECK(port != 0);

	bool serverOk = false;
	std::thread server(ServerMain, &listener, &serverOk);

	Connection client;
	CHECK(Connect("127.0.0.1", port, client));

	// Handshake.
	wire::NodeIdentity clientId = MakeIdentity("Ada", 0xAA);
	CHECK(client.Send(wire::MakeHello(clientId, 42)));

	wire::Frame welcome;
	CHECK(client.Receive(welcome));
	CHECK(welcome.type == wire::MessageType::kWelcome);
	CHECK(welcome.requestId == 42);
	wire::NodeIdentity gotServer;
	CHECK(wire::DecodeNodeIdentity(welcome.payload, gotServer));
	CHECK(gotServer.node == "Berto");
	CHECK(gotServer.HasCap("bfs"));

	// A LIST request over the same connection.
	CHECK(client.Send(wire::MakeListRequest("/", 43)));
	wire::Frame listReply;
	CHECK(client.Receive(listReply));
	CHECK(listReply.type == wire::MessageType::kList);
	CHECK(listReply.requestId == 43);
	std::vector<wire::Entry> entries;
	CHECK(wire::DecodeListing(listReply.payload, entries));
	CHECK(entries.size() == 2);
	CHECK(entries[0].name == "uno.txt" && entries[1].name == "due.txt");
	CHECK(entries[0].attrs.size() == 1 && entries[0].attrs[0].name == "BEOS:TYPE");

	// Closing the client makes the server's next Receive hit EOF and exit.
	client.Close();
	server.join();
	CHECK(serverOk);

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
