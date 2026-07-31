// test_sftpbackend.cpp
//
// Integration test for the SFTP backend against a REAL SFTP server. It needs a server, so it
// reads its target from the environment and SKIPs (exit 0) when unconfigured, keeping a
// serverless `make check` green. To run it against a host (e.g. the Windows OpenSSH box), set
// CAMPIELLO_SFTP_HOST, CAMPIELLO_SFTP_USER, and CAMPIELLO_SFTP_PASS (optionally
// CAMPIELLO_SFTP_PORT, CAMPIELLO_SFTP_PATH, CAMPIELLO_SFTP_FILE, CAMPIELLO_SFTP_KEY), then run
// ./test_sftpbackend.
//
// It exercises: connect + host-key TOFU (the host is pinned on first connect), ReadDir and
// Stat against the base directory, an optional file Open/Read/Close, and that a second connect
// finds the host already trusted (so a rejecting decision still succeeds).

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

#include "../../src/fondamenta/backend/SftpBackend.h"
#include "../../src/fondamenta/backend/SftpKnownHosts.h"

using namespace campiello;
using namespace campiello::fondamenta;

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

static std::string Env(const char* name, const std::string& fallback = "")
{
	const char* v = std::getenv(name);
	return (v != nullptr && v[0] != '\0') ? std::string(v) : fallback;
}

static const char* kKnownHostsPath = "sftp_known_hosts_it.tmp";

int main()
{
	std::string host = Env("CAMPIELLO_SFTP_HOST");
	std::string user = Env("CAMPIELLO_SFTP_USER");
	if (host.empty() || user.empty()) {
		std::printf("SKIP sftpbackend: set CAMPIELLO_SFTP_HOST and CAMPIELLO_SFTP_USER "
			"(and CAMPIELLO_SFTP_PASS) to run against a real SFTP server\n");
		return 0;
	}

	SftpConfig config;
	config.host = host;
	config.port = static_cast<uint16_t>(std::atoi(Env("CAMPIELLO_SFTP_PORT", "22").c_str()));
	config.user = user;
	config.password = Env("CAMPIELLO_SFTP_PASS");
	config.privateKeyPath = Env("CAMPIELLO_SFTP_KEY");
	config.basePath = Env("CAMPIELLO_SFTP_PATH", ".");

	::unlink(kKnownHostsPath);
	SftpKnownHosts knownHosts;

	// First connect: the host is unknown, so our decision is asked and accepts (trust on first
	// use). It should then be pinned.
	bool decisionCalled = false;
	auto accept = [&](HostKeyStatus status, const std::vector<uint8_t>& key) {
		decisionCalled = true;
		CHECK(status == HostKeyStatus::kUnknown);
		CHECK(!key.empty());
		return true;
	};

	SftpBackend sftp;
	BackendStatus s = sftp.Connect(config, knownHosts, kKnownHostsPath, accept);
	if (s != BackendStatus::kOk) {
		std::printf("FAIL connect: %s (status %d)\n", sftp.Error() ? sftp.Error() : "?", (int)s);
		++gFailures;
		std::printf("%d checks, %d failures\n", gChecks, gFailures);
		return 1;
	}
	CHECK(decisionCalled);
	CHECK(sftp.IsConnected());
	std::string pinned = host + ":" + std::to_string(config.port);
	CHECK(knownHosts.IsKnown(pinned));

	// Browse the base directory.
	std::vector<wire::Entry> entries;
	CHECK(sftp.ReadDir("/", entries) == BackendStatus::kOk);
	std::printf("ReadDir(base) -> %zu entries:\n", entries.size());
	for (size_t i = 0; i < entries.size() && i < 10; ++i)
		std::printf("  %s (mode %06o, %llu bytes)\n", entries[i].name.c_str(),
			(unsigned)entries[i].stat.mode, (unsigned long long)entries[i].stat.size);

	// Stat the base directory itself.
	wire::Entry dirEntry;
	CHECK(sftp.Stat("/", dirEntry) == BackendStatus::kOk);

	// Optional: read a file.
	std::string file = Env("CAMPIELLO_SFTP_FILE");
	if (!file.empty()) {
		uint64_t handle = 0, size = 0;
		CHECK(sftp.Open(file, handle, size) == BackendStatus::kOk);
		std::printf("Open(%s) -> size %llu\n", file.c_str(), (unsigned long long)size);
		std::vector<uint8_t> data;
		CHECK(sftp.Read(handle, 0, 64, data) == BackendStatus::kOk);
		std::printf("Read 64 -> %zu bytes: %.*s\n", data.size(),
			(int)(data.size() > 40 ? 40 : data.size()), (const char*)data.data());
		CHECK(sftp.Close(handle) == BackendStatus::kOk);
		CHECK(sftp.Read(handle, 0, 16, data) == BackendStatus::kBadHandle); // closed
	}

	sftp.Disconnect();
	CHECK(!sftp.IsConnected());

	// Second connect reuses the pinned host: a REJECTING decision must not even be consulted,
	// because the key is already trusted.
	auto reject = [](HostKeyStatus, const std::vector<uint8_t>&) { return false; };
	SftpBackend sftp2;
	CHECK(sftp2.Connect(config, knownHosts, kKnownHostsPath, reject) == BackendStatus::kOk);
	sftp2.Disconnect();

	::unlink(kKnownHostsPath);
	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
