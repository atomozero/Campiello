// campiello_smb_main.cpp
//
// The userlandfs FUSE add-on entry point for interop SMB mounts. A userlandfs FUSE filesystem is
// a shared library that exports main(); userlandfs_create_file_system finds it via
// get_image_symbol and runs it (verified M0, docs/VERIFIED.md section 1). This main() parses the
// SMB target from the mount arguments (SmbMount) and hands a backend to CampielloFuseMain, which
// runs fuse_main. With a named share it mounts that one share (SmbBackend); with no share it mounts
// the whole server Windows "\\server" style, the root listing the shares as folders
// (SmbServerBackend). Auth is user/password/domain, no host-key step.
//
// Haiku-only, OPTIONAL: links libuserlandfs_fuse.so and libsmb2 (LGPL-2.1). Ships in the separate
// campiello_smb package; the MIT core does not depend on it (docs/SMB.md). Build-verified as an
// add-on; mounting is a manual test on real hardware, not run here.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/fondamenta/fuse/CampielloFuse.h"
#include "SmbBackend.h"
#include "SmbMount.h"
#include "SmbServerBackend.h"

using namespace campiello::fondamenta;

int main(int argc, char** argv)
{
	SmbMount mount = ParseSmbMount(argc, argv);
	if (!mount.ok) {
		std::fprintf(stderr, "campiello_smb: %s\n", mount.error.c_str());
		return 1;
	}

	// A named share mounts that one share; no share mounts the server (root = the share list).
	SmbBackend shareBackend;
	SmbServerBackend serverBackend;
	PeerBackend* backend = nullptr;
	BackendStatus s;
	const char* error;
	if (mount.config.share.empty()) {
		s = serverBackend.ConnectServer(mount.config);
		backend = &serverBackend;
		error = serverBackend.Error();
	} else {
		s = shareBackend.Connect(mount.config);
		backend = &shareBackend;
		error = shareBackend.Error();
	}
	if (s != BackendStatus::kOk) {
		std::fprintf(stderr, "campiello_smb: connect failed: %s\n",
			error != nullptr ? error : "unknown error");
		return 1;
	}

	// Forward the residual arguments (mount point, FUSE options) to fuse_main.
	std::vector<char*> fuseArgv;
	fuseArgv.reserve(mount.fuseArgs.size());
	for (std::string& a : mount.fuseArgs)
		fuseArgv.push_back(&a[0]);

	int rc = CampielloFuseMain(static_cast<int>(fuseArgv.size()), fuseArgv.data(), *backend);

	if (mount.config.share.empty())
		serverBackend.Disconnect();
	else
		shareBackend.Disconnect();
	return rc;
}
