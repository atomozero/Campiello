// SmbMount.h
//
// Parses an SMB mount's configuration out of the arguments userlandfs hands the add-on (the
// remainder of the mount `-p` string) plus environment fallbacks, and returns the residual argv
// to forward to fuse_main. Recognized tokens are `key=value`: server, share, user, password,
// domain, path (base dir within the share). Anything unrecognized (the mount point, FUSE `-o`
// options) passes through to the FUSE arguments. Unset fields fall back to
// CAMPIELLO_SMB_SERVER/SHARE/USER/PASS/DOMAIN/PATH.
//
// The SMB counterpart to SftpMount. Pure standard C++ (no libsmb2, no Haiku), so parsing is
// unit-testable off Haiku; the add-on main that consumes it is Haiku-only.

#ifndef CAMPIELLO_OPTIONAL_SMB_SMBMOUNT_H
#define CAMPIELLO_OPTIONAL_SMB_SMBMOUNT_H

#include <string>
#include <vector>

#include "SmbBackend.h" // SmbConfig

namespace campiello {
namespace fondamenta {

struct SmbMount {
	SmbConfig                config;
	std::vector<std::string> fuseArgs; // argv to forward to fuse_main (argv[0] first)
	bool                     ok = false;
	std::string              error;    // set when ok is false
};

// Parse argv (and environment fallbacks) into an SmbMount. `ok` is false with an `error` if the
// server or share is missing after applying both sources.
SmbMount ParseSmbMount(int argc, char** argv);

// The inverse: build the whitespace-separated userlandfs mount parameter string from a config,
// beginning with the fs name "campiello_smb" followed by the key=value tokens ParseSmbMount
// reads. The connect helper passes this to fs_mount_volume. Values are percent-encoded
// (EncodeMountValue) so spaces (e.g. a share named "din esp8266 mini") survive userlandfs's
// whitespace tokenization; ParseSmbMount decodes them.
std::string BuildSmbMountParameters(const SmbConfig& config);

// Percent-encode a mount-parameter value so it contains no whitespace (which userlandfs uses to
// split the parameter string) and no '%'. Space -> %20, '%' -> %25, and any control byte -> %XX.
std::string EncodeMountValue(const std::string& value);

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_OPTIONAL_SMB_SMBMOUNT_H
