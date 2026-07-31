// test_servernode.cpp
//
// End-to-end test of the wired server stack (ServerNode): a real TLS client pairs through the
// gate and browses the shared folder, an unknown peer is refused when the prompt denies, and
// the pairing is persisted to the trust store. Exercises identity -> TLS -> pairing gate ->
// FileServer over loopback, with a fake prompt (no Haiku, no UI). OpenSSL + sockets + threads.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "../../src/fondamenta/backend/CnpBackend.h"
#include "../../src/traghetto/server/ServerNode.h"
#include "../../src/traghetto/tls/Identity.h"
#include "../../src/traghetto/tls/TlsConnection.h"
#include "../../src/traghetto/trust/Pairing.h"
#include "../../src/traghetto/trust/TrustStore.h"
#include "../../src/traghetto/wire/Handshake.h"
#include "../../src/traghetto/wire/Listing.h"

using namespace campiello;
using campiello::net::Fingerprint;
using campiello::net::Identity;
using campiello::net::PairingPrompt;
using campiello::net::ServerConfig;
using campiello::net::ServerNode;
using campiello::net::TlsConnection;
using campiello::net::TlsContext;
using campiello::net::TrustDecision;
using campiello::net::TrustStore;
using campiello::fondamenta::BackendStatus;
using campiello::fondamenta::CnpBackend;

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

class FakePrompt : public PairingPrompt {
public:
	bool answer = true;
	int calls = 0;
	bool Ask(const std::string&, const Fingerprint&, TrustDecision) override
	{
		++calls;
		return answer;
	}
};

static const char* kRoot = "servernode_test.d";
static const char* kIdPath = "servernode_id.pem";
static const char* kTrustPath = "servernode_trust";

static void Setup()
{
	::mkdir(kRoot, 0755);
	std::ofstream(std::string(kRoot) + "/nota.txt") << "ciao";
	std::remove(kIdPath);
	std::remove(kTrustPath);
}

static void Cleanup()
{
	std::remove((std::string(kRoot) + "/nota.txt").c_str());
	::rmdir(kRoot);
	std::remove(kIdPath);
	std::remove(kTrustPath);
}

// Build a client HELLO identity carrying the client's REAL fingerprint (the gate cross-checks
// it against the TLS-authenticated key).
static wire::NodeIdentity ClientId(const Identity& id, const std::string& name)
{
	wire::NodeIdentity out;
	Fingerprint fp = id.GetFingerprint();
	out.fingerprint.assign(fp.begin(), fp.end());
	out.node = name;
	out.caps = { wire::kCapBfs };
	return out;
}

static bool HasEntry(const std::vector<wire::Entry>& entries, const std::string& name)
{
	for (const auto& e : entries)
		if (e.name == name)
			return true;
	return false;
}

static void TestPairServeAndDeny()
{
	FakePrompt prompt;
	prompt.answer = true;

	ServerConfig cfg;
	cfg.identityPath = kIdPath;
	cfg.trustStorePath = kTrustPath;
	cfg.sharedRoot = kRoot;
	cfg.nodeName = "Server";
	cfg.bindHost = "127.0.0.1";
	cfg.port = 0; // ephemeral

	ServerNode node;
	CHECK(node.Start(cfg, prompt));
	CHECK(node.IsRunning());
	uint16_t port = node.Port();
	CHECK(port != 0);
	Fingerprint serverFp = node.IdentityFingerprint();

	// A pinning client pairs and browses.
	Identity clientId;
	CHECK(Identity::Generate(clientId));
	Fingerprint clientFp = clientId.GetFingerprint();
	TlsContext clientCtx;
	CHECK(clientCtx.Init(clientId, /*server=*/false));

	TlsConnection conn;
	CHECK(TlsConnection::Connect(clientCtx, "127.0.0.1", port, &serverFp, conn));
	CnpBackend backend(conn);

	wire::NodeIdentity peer;
	CHECK(backend.Hello(ClientId(clientId, "Client"), peer) == BackendStatus::kOk);
	CHECK(peer.node == "Server");
	CHECK(peer.HasCap("bfs"));
	CHECK(prompt.calls == 1); // first contact raised the prompt

	std::vector<wire::Entry> entries;
	CHECK(backend.ReadDir("/", entries) == BackendStatus::kOk); // admitted: serving works
	CHECK(HasEntry(entries, "nota.txt"));
	conn.Close();

	// A different, unknown peer is refused at HELLO when the prompt denies.
	prompt.answer = false;
	Identity strangerId;
	CHECK(Identity::Generate(strangerId));
	Fingerprint strangerFp = strangerId.GetFingerprint();
	TlsContext strangerCtx;
	CHECK(strangerCtx.Init(strangerId, /*server=*/false));

	TlsConnection conn2;
	CHECK(TlsConnection::Connect(strangerCtx, "127.0.0.1", port, &serverFp, conn2));
	CnpBackend backend2(conn2);
	wire::NodeIdentity peer2;
	CHECK(backend2.Hello(ClientId(strangerId, "Stranger"), peer2)
		== BackendStatus::kAccessDenied);
	conn2.Close();

	node.Stop();
	CHECK(!node.IsRunning());

	// The allowed peer is persisted; the denied one is not.
	TrustStore reloaded;
	CHECK(reloaded.LoadFromFile(kTrustPath));
	CHECK(reloaded.IsTrusted(clientFp));
	CHECK(!reloaded.IsTrusted(strangerFp));
}

static void TestBadSharedRootFails()
{
	FakePrompt prompt;
	ServerConfig cfg;
	cfg.identityPath = kIdPath;
	cfg.trustStorePath = kTrustPath;
	cfg.sharedRoot = "servernode_nonexistent_dir";
	cfg.nodeName = "X";
	cfg.bindHost = "127.0.0.1";
	cfg.port = 0;

	ServerNode node;
	CHECK(!node.Start(cfg, prompt));
	CHECK(node.Error() != nullptr);
	CHECK(!node.IsRunning());
}

int main()
{
	Setup();
	TestPairServeAndDeny();
	TestBadSharedRootFails();
	Cleanup();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
