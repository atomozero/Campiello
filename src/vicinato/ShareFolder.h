// ShareFolder.h
//
// The WON "folder of service icons" (docs/NEIGHBORHOOD.md, Option C). It keeps a folder in sync
// with the discovered network shares: one shortcut file per share, each carrying the server in an
// attribute and a MIME type whose preferred app is the SMB connect helper, so a double-click in
// Tracker opens the helper prefilled. This is the producing half; the helper (RefsReceived) is
// the consuming half.
//
// Haiku-only (BMimeType / BNode attributes / BDirectory), MIT: it only writes the shortcut and
// the server string, never libsmb2. End-user strings are Italian (working agreement rule 4).

#ifndef CAMPIELLO_VICINATO_SHAREFOLDER_H
#define CAMPIELLO_VICINATO_SHAREFOLDER_H

#include <string>
#include <vector>

#include "NetworkDirectory.h"

namespace campiello {
namespace vicinato {

// Register the WON share-shortcut MIME type once: its preferred app is the SMB helper, plus an
// icon so Tracker shows the shortcuts distinctly. Idempotent.
void RegisterShareType();

// The default WON folder (<home>/WON), created if missing; "" if the home cannot be found.
std::string DefaultShareFolder();

// Sync `folderPath` with the browsable SMB services: create or update one shortcut file per
// service and delete WON shortcuts for services that are gone. The folder is created if missing.
void SyncShareFolder(const std::string& folderPath,
	const std::vector<NetworkService>& services);

} // namespace vicinato
} // namespace campiello

#endif // CAMPIELLO_VICINATO_SHAREFOLDER_H
