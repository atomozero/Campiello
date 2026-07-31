// test_connmanager.cpp
//
// End-to-end for the discovery connection manager: a real ServerNode (TLS + pairing gate +
// FileServer) runs on loopback; a fake PeerDirectory points a ConnectionManager at it; and a
// PathRouter over that manager browses the peer as /<peer>/... So the whole discovery client
// path - resolve peer -> TLS connect -> client-side pin -> CNP HELLO -> browse - is exercised
// with no mDNS and no mount. OpenSSL + sockets + threads; no Haiku.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "../../src/fondamenta/discovery/ConnectionManager.h"
#include "../../src/fondamenta/discovery/PathRouter.h"
#include "../../src/traghetto/server/ServerNode.h"
#include "../../src/traghetto/trust/Pairing.h"
#include "../../src/traghetto/trust/TrustStore.h"

using namespace campiello;
using campiello::net::Fingerprint;
using campiello::net::PairingPrompt;
using campiello::net::ServerConfig;
using campiello::net::ServerNode;
using campiello::net::TrustDecision;
using campiello::net::TrustStore;
using campiello::fondamenta::BackendStatus;
using campiello::fondamenta::ConnectionManager;
using campiello::fondamenta::PathRouter;
using campiello::fondamenta::PeerDirectory;
using campiello::fondamenta::PeerEndpoint;

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

// A directory with a single peer "Server" at a runtime-assigned loopback port.
class OneServerDirectory : public PeerDirectory {
public:
	uint16_t port = 0;
	std::vector<PeerEndpoint> Endpoints() override
	{
		std::vector<PeerEndpoint> out;
		if (port != 0)
			out.push_back(PeerEndpoint{"Server", "127.0.0.1", port});
		return out;
	}
};

static const char* kRoot = "connmgr_test.d";
static const char* kServerId = "connmgr_server_id.pem";
static const char* kServerTrust = "connmgr_server_trust";
static const char* kClientId = "connmgr_client_id.pem";
static const char* kClientTrust = "connmgr_client_trust";

static void Cleanup()
{
	std::remove((std::string(kRoot) + "/nota.txt").c_str());
	::rmdir(kRoot);
	std::remove(kServerId);
	std::remove(kServerTrust);
	std::remove(kClientId);
	std::remove(kClientTrust);
}

static bool HasEntry(const std::vector<wire::Entry>& v, const std::string& name)
{
	for (const auto& e : v)
		if (e.name == name)
			return true;
	return false;
}

int main()
{
	Cleanup();
	::mkdir(kRoot, 0755);
	std::ofstream(std::string(kRoot) + "/nota.txt") << "ciao dal peer";

	// A real server node on loopback, auto-allowing the pairing prompt.
	FakePrompt prompt;
	ServerConfig cfg;
	cfg.identityPath = kServerId;
	cfg.trustStorePath = kServerTrust;
	cfg.sharedRoot = kRoot;
	cfg.nodeName = "Server";
	cfg.bindHost = "127.0.0.1";
	cfg.port = 0;

	ServerNode node;
	CHECK(node.Start(cfg, prompt));
	CHECK(node.IsRunning());
	uint16_t port = node.Port();
	CHECK(port != 0);

	// The connection manager, pointed at the server through a fake directory.
	OneServerDirectory directory;
	directory.port = port;
	ConnectionManager manager(directory, kClientId, kClientTrust, "Client");
	CHECK(manager.Init());

	// The router presents the peer set as a filesystem.
	PathRouter router(manager);

	// Root lists the peer.
	std::vector<wire::Entry> entries;
	CHECK(router.ReadDir("/", entries) == BackendStatus::kOk);
	CHECK(HasEntry(entries, "Server"));

	// Entering the peer connects (TLS + pin + HELLO) and lists its shared folder.
	CHECK(router.ReadDir("/Server", entries) == BackendStatus::kOk);
	CHECK(HasEntry(entries, "nota.txt"));
	CHECK(prompt.calls == 1); // first contact raised the peer's allow prompt once

	// Stat + read a file on the peer through the router.
	wire::Entry entry;
	CHECK(router.Stat("/Server/nota.txt", entry) == BackendStatus::kOk);
	CHECK(entry.stat.size == std::string("ciao dal peer").size());

	uint64_t handle = 0, size = 0;
	CHECK(router.Open("/Server/nota.txt", handle, size) == BackendStatus::kOk);
	std::vector<uint8_t> data;
	CHECK(router.Read(handle, 0, 4096, data) == BackendStatus::kOk);
	CHECK(std::string(data.begin(), data.end()) == "ciao dal peer");
	CHECK(router.Close(handle) == BackendStatus::kOk);

	// A second browse reuses the cached connection: no new pairing prompt.
	CHECK(router.ReadDir("/Server", entries) == BackendStatus::kOk);
	CHECK(prompt.calls == 1);

	// An unknown peer is not found.
	CHECK(router.ReadDir("/Nessuno", entries) == BackendStatus::kNotFound);

	// The client pinned the server's key on first use.
	TrustStore clientTrust;
	CHECK(clientTrust.LoadFromFile(kClientTrust));
	CHECK(clientTrust.IsTrusted(node.IdentityFingerprint()));

	manager.CloseAll();
	node.Stop();
	Cleanup();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
