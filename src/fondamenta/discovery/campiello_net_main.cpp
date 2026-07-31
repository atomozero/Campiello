// campiello_net_main.cpp
//
// The userlandfs FUSE add-on for native Campiello discovery (the "network neighborhood" volume,
// docs/DISCOVERY_FS.md commit 3, v1). Mounted top-level (e.g. /Campiello), its root lists the
// machines Bricola finds on the LAN, and each peer folder browses that machine's shared
// "Condivisa" over CNP.
//
// It owns a browse-only Bricola (the live peer source), a ConnectionManager (lazy per-peer CNP
// over TLS, with client-side key pinning), and a PathRouter that ties the two into one
// PeerBackend, which CampielloFuseMain serves. v1 is FUSE and read-only, so a folder refreshes
// when Tracker re-reads it; live node-monitor push is v2 on the native front end.
//
// Haiku-only. Build-verified as an add-on; mounting is a manual test.

#include <cstdio>
#include <csignal>
#include <string>

#include <sys/stat.h>
#include <unistd.h> // gethostname

#include <FindDirectory.h>

#include "../../bricola/mdns/Bricola.h"
#include "../fuse/CampielloFuse.h"
#include "BricolaDirectory.h"
#include "ConnectionManager.h"
#include "PathRouter.h"

using namespace campiello::fondamenta;

namespace {

// <settings>/Campiello/<leaf>, creating the Campiello directory. Falls back to a relative path.
std::string SettingsFile(const char* leaf)
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) == B_OK) {
		std::string dir = std::string(settings) + "/Campiello";
		::mkdir(dir.c_str(), 0755);
		return dir + "/" + leaf;
	}
	return leaf;
}

std::string NodeName()
{
	char host[256];
	if (gethostname(host, sizeof(host)) == 0 && host[0] != '\0') {
		host[sizeof(host) - 1] = '\0';
		return std::string(host);
	}
	return "Haiku";
}

} // namespace

int main(int argc, char** argv)
{
	// A dropped peer connection must not kill the mount (see the SFTP add-on note).
	std::signal(SIGPIPE, SIG_IGN);

	campiello::bricola::mdns::Bricola bricola;

	// The client uses this node's own identity (shared with the daemon) and a discovery-specific
	// client trust store for the peers it pins on connect.
	BricolaDirectory directory(bricola);
	ConnectionManager manager(directory, SettingsFile("identity.pem"),
		SettingsFile("discovery_trusted"), NodeName());
	if (!manager.Init()) {
		std::fprintf(stderr, "campiello_net: %s\n",
			manager.Error() != nullptr ? manager.Error() : "init failed");
		return 1;
	}

	// Start discovery (no observer; the directory reads Bricola's peer snapshot).
	if (!bricola.StartBrowsing(nullptr)) {
		std::fprintf(stderr, "campiello_net: discovery not started: %s\n",
			bricola.Error() != nullptr ? bricola.Error() : "unknown error");
		return 1;
	}

	PathRouter router(manager);
	int rc = CampielloFuseMain(argc, argv, router);

	bricola.Stop();
	manager.CloseAll();
	return rc;
}
