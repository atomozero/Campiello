// Paths.h
//
// Where Campiello keeps its per-node files: the long-lived identity (key + cert) and the
// trust store. On Haiku these live under B_USER_SETTINGS_DIRECTORY/Campiello (the M0
// decision, docs/VERIFIED.md section 8); off Haiku they fall back to $HOME/.config/Campiello
// so the code builds and tests in CI. The user never sees these paths.

#ifndef CAMPIELLO_TRAGHETTO_TRUST_PATHS_H
#define CAMPIELLO_TRAGHETTO_TRUST_PATHS_H

#include <string>

namespace campiello {
namespace net {

// The Campiello settings directory, created (0700) if needed. False if the base directory
// cannot be determined.
bool SettingsDir(std::string& out);

// <settings>/identity.pem : the node's long-lived key and self-signed certificate.
bool IdentityPath(std::string& out);

// <settings>/trusted : the paired-peer TrustStore file.
bool TrustStorePath(std::string& out);

// The default shared folder served to paired peers: <home>/Desktop/Condivisa, created (0755)
// if needed. On Haiku <home> is B_USER_DIRECTORY; off Haiku it is $HOME. This is the single
// safe-by-default shared scope (docs/PROPOSAL.md section 9); widening it is a deliberate opt
// in handled elsewhere. The name is user-facing, so it is Italian.
bool SharedRootPath(std::string& out);

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_TRUST_PATHS_H
