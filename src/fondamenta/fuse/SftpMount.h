// SftpMount.h
//
// Parses an SFTP mount's configuration out of the arguments userlandfs hands the add-on (the
// remainder of the mount `-p` string, per docs/VERIFIED.md section 2) plus environment
// fallbacks, and returns the residual argv to forward to fuse_main. Recognized tokens are
// `key=value`: host, port, user, password, key (private-key file), path (remote base). Anything
// unrecognized (the mount point, FUSE `-o` options) passes through to the FUSE arguments.
// Unset fields fall back to CAMPIELLO_SFTP_HOST/PORT/USER/PASS/KEY/PATH.
//
// Pure standard C++ (no libssh2, no Haiku), so the parsing is unit-testable off Haiku; the
// add-on main that consumes it is Haiku-only.

#ifndef CAMPIELLO_FONDAMENTA_FUSE_SFTPMOUNT_H
#define CAMPIELLO_FONDAMENTA_FUSE_SFTPMOUNT_H

#include <string>
#include <vector>

#include "../backend/SftpBackend.h" // SftpConfig

namespace campiello {
namespace fondamenta {

struct SftpMount {
	SftpConfig               config;
	std::vector<std::string> fuseArgs; // argv to forward to fuse_main (argv[0] first)
	bool                     ok = false;
	std::string              error;    // set when ok is false
};

// Parse argv (and environment fallbacks) into an SftpMount. `ok` is false with an `error` if
// the host or user is missing after applying both sources.
SftpMount ParseSftpMount(int argc, char** argv);

// The inverse: build the whitespace-separated userlandfs mount parameter string from a config,
// beginning with the fs name "campiello_sftp" (the first token userlandfs consumes) followed by
// the key=value tokens ParseSftpMount reads. The connect helper passes this to
// fs_mount_volume. NOTE: values must not contain whitespace (userlandfs splits on it); the
// helper avoids the issue by preferring key-file auth and rejecting spaces in a password.
std::string BuildMountParameters(const SftpConfig& config);

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_FUSE_SFTPMOUNT_H
