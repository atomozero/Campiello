// DaemonApp.cpp
//
// Implementation of the resident daemon BApplication. See DaemonApp.h. Verified against the
// Haiku headers: BApplication(const char* signature) (Application.h:36), ReadyToRun /
// QuitRequested / Run (Application.h:49,51,53); B_QUIT_REQUESTED is a standard app message.

#ifdef __HAIKU__

#include "DaemonApp.h"

#include <cstdio>
#include <string>

#include <unistd.h> // gethostname

#include <Deskbar.h>
#include <Entry.h>
#include <Roster.h>

#include "../../bricola/mdns/Bricola.h"
#include "../tls/Fingerprint.h"
#include "../trust/Paths.h"
#include "../wire/Frame.h"

namespace campiello {
namespace net {

namespace {

// Application MIME signature, following the sibling convention (e.g. LocalSend's
// "application/x-vnd.LocalSend").
const char* const kSignature = "application/x-vnd.Campiello-daemon";

// The Deskbar replicant that shows discovered peers: its MIME signature (for the roster to
// find the installed add-on) and the shelf item name (to check it is not already installed).
const char* const kReplicantSignature = "application/x-vnd.Campiello-replicant";
const char* const kReplicantItem = "CampielloPeers";

// Add (or refresh) the peer-presence replicant in the Deskbar, so it appears by itself. Best-effort;
// does nothing if the replicant is not installed. A package update swaps the replicant add-on file,
// which leaves the Deskbar holding a stale (blank) instance while HasItem still reports it present -
// so re-add from the currently-installed add-on rather than skipping when it is already there.
void InstallReplicant()
{
	entry_ref ref;
	if (be_roster->FindApp(kReplicantSignature, &ref) != B_OK)
		return; // not installed / not registered
	BDeskbar deskbar;
	if (deskbar.HasItem(kReplicantItem))
		deskbar.RemoveItem(kReplicantItem);
	deskbar.AddItem(&ref, nullptr);
}

// The friendly node name shown to peers: the machine's hostname, or a plain fallback. This is
// only a label; identity is the key.
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

DaemonApp::DaemonApp()
	: BApplication(kSignature)
{
}

void DaemonApp::ReadyToRun()
{
	ServerConfig config;
	if (!IdentityPath(config.identityPath) || !TrustStorePath(config.trustStorePath)
		|| !SharedRootPath(config.sharedRoot)) {
		std::fprintf(stderr, "Campiello: cannot resolve node paths\n");
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	config.nodeName = NodeName();
	// config.port and config.bindHost keep their defaults (kDefaultCnpPort, all interfaces).

	if (!fNode.Start(config, fPrompt)) {
		std::fprintf(stderr, "Campiello: daemon failed to start: %s\n",
			fNode.Error() != nullptr ? fNode.Error() : "unknown error");
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	// Developer log only; never surfaced to the user (error-surface rule).
	std::fprintf(stderr, "Campiello: serving \"%s\" on port %u, sharing %s\n",
		config.nodeName.c_str(), (unsigned)fNode.Port(), config.sharedRoot.c_str());

	// Advertise on the LAN and browse for peers (Bricola). The node's real bound port and its
	// identity fingerprint (a hint only, never trusted) go into the DNS-SD record. The A-record
	// address is left empty for now: browsers fall back to the announce's sender address, which
	// is this node. Discovery is best-effort; a failure to open the multicast socket is logged
	// but does not stop serving (a peer can still connect by address).
	bricola::mdns::ServiceInfo info;
	info.instance = config.nodeName;
	info.hostname = config.nodeName + ".local";
	info.port = fNode.Port();
	info.protocolVersion = wire::kProtocolVersion;
	info.bfsAttrs = true; // native mode carries typed BFS attributes
	info.fingerprintHex = ToHex(fNode.IdentityFingerprint());
	if (!fBricola.Start(info, nullptr)) {
		std::fprintf(stderr, "Campiello: discovery not started: %s\n",
			fBricola.Error() != nullptr ? fBricola.Error() : "unknown error");
	}

	// Put the peer-presence replicant in the Deskbar so it shows up on its own.
	InstallReplicant();
}

bool DaemonApp::QuitRequested()
{
	fBricola.Stop();
	fNode.Stop();
	return true;
}

} // namespace net
} // namespace campiello

#endif // __HAIKU__
