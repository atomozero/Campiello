// DaemonApp.h
//
// The resident Campiello daemon as a Haiku BApplication: it owns be_app (so the pairing
// prompt's BAlert has an app_server connection) and runs the CNP server. It is a thin shell
// over the portable ServerNode: on ReadyToRun it resolves the node's paths, derives the
// friendly name, and starts the node with a HaikuPairingPrompt; on quit it stops it.
//
// Haiku-only (guarded by __HAIKU__). There is no window and no visible UI in normal operation
// (experience rule): the only thing a user ever sees is the one-tap pairing prompt.

#ifndef CAMPIELLO_TRAGHETTO_SERVER_DAEMONAPP_H
#define CAMPIELLO_TRAGHETTO_SERVER_DAEMONAPP_H

#ifdef __HAIKU__

#include <Application.h>

#include "../../bricola/mdns/Bricola.h"
#include "HaikuPairingPrompt.h"
#include "ServerNode.h"

namespace campiello {
namespace net {

class DaemonApp : public BApplication {
public:
	DaemonApp();

	// Resolve paths, derive the node name, and start the server. Quits the app on failure.
	void ReadyToRun() override;

	// Stop discovery and the server before the app exits.
	bool QuitRequested() override;

private:
	HaikuPairingPrompt      fPrompt;
	ServerNode              fNode;
	bricola::mdns::Bricola  fBricola;
};

} // namespace net
} // namespace campiello

#endif // __HAIKU__

#endif // CAMPIELLO_TRAGHETTO_SERVER_DAEMONAPP_H
