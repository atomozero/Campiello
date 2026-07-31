// test_smbbackend.cpp
//
// Integration test for the SMB backend against a REAL SMB2/3 server (e.g. a Windows share). It
// needs a server, so it reads its target from the environment and SKIPs (exit 0) when
// unconfigured, keeping a serverless build green. To run it against a Windows box that has file
// sharing on, set CAMPIELLO_SMB_HOST, CAMPIELLO_SMB_SHARE, CAMPIELLO_SMB_USER, and
// CAMPIELLO_SMB_PASS (optionally CAMPIELLO_SMB_DOMAIN, CAMPIELLO_SMB_PATH, CAMPIELLO_SMB_FILE),
// then run ./test_smbbackend.
//
// It exercises: connect + auth, ReadDir and Stat against the base path, and an optional file
// Open/Read/Close. SMB has no host-key trust step (auth is user/password/domain), so unlike the
// SFTP test there is nothing to pin.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "SmbBackend.h"

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

int main()
{
	std::string host = Env("CAMPIELLO_SMB_HOST");
	std::string share = Env("CAMPIELLO_SMB_SHARE");
	if (host.empty() || share.empty()) {
		std::printf("SKIP smbbackend: set CAMPIELLO_SMB_HOST and CAMPIELLO_SMB_SHARE "
			"(and CAMPIELLO_SMB_USER/PASS) to run against a real SMB server\n");
		return 0;
	}

	SmbConfig config;
	config.server = host;
	config.share = share;
	config.user = Env("CAMPIELLO_SMB_USER");
	config.password = Env("CAMPIELLO_SMB_PASS");
	config.domain = Env("CAMPIELLO_SMB_DOMAIN");
	config.basePath = Env("CAMPIELLO_SMB_PATH");

	SmbBackend smb;

	// Enumerate the host's shares before mounting (so the helper can offer a picker). Requires
	// credentials on modern Windows; the configured share should appear in the list.
	std::vector<std::string> shares;
	BackendStatus es = smb.EnumShares(config, shares);
	if (es == BackendStatus::kOk) {
		std::printf("EnumShares -> %zu shares:\n", shares.size());
		for (const std::string& name : shares)
			std::printf("  %s\n", name.c_str());
		bool sawConfigured = false;
		for (const std::string& name : shares)
			if (name == config.share)
				sawConfigured = true;
		CHECK(sawConfigured); // the share we mount should be enumerable
	} else {
		std::printf("EnumShares skipped/failed: %s (status %d)\n",
			smb.Error() ? smb.Error() : "?", (int)es);
	}

	BackendStatus s = smb.Connect(config);
	if (s != BackendStatus::kOk) {
		std::printf("FAIL connect: %s (status %d)\n", smb.Error() ? smb.Error() : "?", (int)s);
		++gFailures;
		std::printf("%d checks, %d failures\n", gChecks, gFailures);
		return 1;
	}
	CHECK(smb.IsConnected());

	// Browse the base path (share root unless CAMPIELLO_SMB_PATH is set).
	std::vector<wire::Entry> entries;
	CHECK(smb.ReadDir("/", entries) == BackendStatus::kOk);
	std::printf("ReadDir(base) -> %zu entries:\n", entries.size());
	for (size_t i = 0; i < entries.size() && i < 10; ++i)
		std::printf("  %s (mode %06o, %llu bytes)\n", entries[i].name.c_str(),
			(unsigned)entries[i].stat.mode, (unsigned long long)entries[i].stat.size);

	// Stat the base path itself; it must be a directory.
	wire::Entry dirEntry;
	CHECK(smb.Stat("/", dirEntry) == BackendStatus::kOk);
	CHECK((dirEntry.stat.mode & 0170000) == 0040000); // S_IFDIR

	// A path that cannot exist maps to kNotFound.
	wire::Entry missing;
	CHECK(smb.Stat("/campiello_no_such_entry_zzz", missing) == BackendStatus::kNotFound);

	// Optional: read a file.
	std::string file = Env("CAMPIELLO_SMB_FILE");
	if (!file.empty()) {
		uint64_t handle = 0, size = 0;
		CHECK(smb.Open(file, handle, size) == BackendStatus::kOk);
		std::printf("Open(%s) -> size %llu\n", file.c_str(), (unsigned long long)size);
		std::vector<uint8_t> data;
		CHECK(smb.Read(handle, 0, 64, data) == BackendStatus::kOk);
		std::printf("Read 64 -> %zu bytes: %.*s\n", data.size(),
			(int)(data.size() > 40 ? 40 : data.size()), (const char*)data.data());
		CHECK(smb.Close(handle) == BackendStatus::kOk);
		CHECK(smb.Read(handle, 0, 16, data) == BackendStatus::kBadHandle); // closed
	}

	smb.Disconnect();
	CHECK(!smb.IsConnected());

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
