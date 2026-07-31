// campiello_sftp_main.cpp
//
// The userlandfs FUSE add-on entry point for interop SFTP mounts (M1). A userlandfs FUSE
// filesystem is a shared library that exports main(); userlandfs_create_file_system finds it
// via get_image_symbol and runs it (verified M0, docs/VERIFIED.md section 1). This main()
// parses the SFTP target from the mount arguments (SftpMount), connects an SftpBackend with
// host-key trust-on-first-use, and hands it to CampielloFuseMain, which runs fuse_main.
//
// Haiku-only. Build-verified as an add-on (links libuserlandfs_fuse.so and libssh2); mounting
// is a manual test on real hardware, not run here.

#include <cstdio>
#include <string>
#include <vector>

#include <sys/stat.h>

#include <FindDirectory.h>

#include "../backend/SftpBackend.h"
#include "../backend/SftpKnownHosts.h"
#include "CampielloFuse.h"
#include "SftpMount.h"

using namespace campiello::fondamenta;

namespace {

// Where the SFTP host-key store lives: <settings>/Campiello/sftp_known_hosts. The Campiello
// directory is created if absent. Falls back to a relative path if find_directory fails.
std::string KnownHostsPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) == B_OK) {
		std::string dir = std::string(settings) + "/Campiello";
		::mkdir(dir.c_str(), 0755); // ensure it exists; ignore EEXIST
		return dir + "/sftp_known_hosts";
	}
	return "sftp_known_hosts";
}

} // namespace

int main(int argc, char** argv)
{
	SftpMount mount = ParseSftpMount(argc, argv);
	if (!mount.ok) {
		std::fprintf(stderr, "campiello_sftp: %s\n", mount.error.c_str());
		return 1;
	}

	std::string knownHostsPath = KnownHostsPath();
	SftpKnownHosts knownHosts;
	knownHosts.LoadFromFile(knownHostsPath); // ok if absent

	// Headless mount policy: trust a host key on first sight (pin it), but refuse a changed key
	// (a possible man-in-the-middle). The interactive "unknown host, allow?" prompt belongs to
	// the connect helper (M5); an already-running mount cannot ask.
	SftpBackend::HostKeyDecision decision =
		[](HostKeyStatus status, const std::vector<uint8_t>&) {
			return status == HostKeyStatus::kUnknown;
		};

	SftpBackend backend;
	BackendStatus s = backend.Connect(mount.config, knownHosts, knownHostsPath, decision);
	if (s != BackendStatus::kOk) {
		std::fprintf(stderr, "campiello_sftp: connect failed: %s\n",
			backend.Error() != nullptr ? backend.Error() : "unknown error");
		return 1;
	}

	// Forward the residual arguments (mount point, FUSE options) to fuse_main.
	std::vector<char*> fuseArgv;
	fuseArgv.reserve(mount.fuseArgs.size());
	for (std::string& a : mount.fuseArgs)
		fuseArgv.push_back(&a[0]);

	int rc = CampielloFuseMain(static_cast<int>(fuseArgv.size()), fuseArgv.data(), backend);

	backend.Disconnect();
	return rc;
}
