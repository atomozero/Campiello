// CampielloFuse.h
//
// The Fondamenta FUSE front end: presents a PeerBackend as a read-only userlandfs volume that
// Tracker can browse. It implements the read subset of struct fuse_operations (getattr,
// readdir, open, read, release, statfs) by forwarding to the backend, translating types with
// FuseTranslate. Interop mode (SftpBackend) uses this; native mode reads can too, though native
// attribute fidelity needs the fs_interface front end (decision C, docs/VERIFIED.md section 1).
//
// The write callbacks are intentionally absent, and open() refuses write modes with -EROFS:
// M1 is read-only. Access to the single backend is serialized with a mutex, since one backend
// session is not thread-safe and FUSE may call in on several threads.
//
// Haiku-only (built against the userlandfs FUSE headers, links libuserlandfs_fuse.so). The
// portable translation it relies on lives in FuseTranslate and is tested off Haiku.

#ifndef CAMPIELLO_FONDAMENTA_FUSE_CAMPIELLOFUSE_H
#define CAMPIELLO_FONDAMENTA_FUSE_CAMPIELLOFUSE_H

#include "../backend/PeerBackend.h"

namespace campiello {
namespace fondamenta {

// Run the FUSE main loop, presenting `backend` as a read-only volume. `argv` are the FUSE /
// mount arguments (mount point, options) as delivered by userlandfs. Returns fuse_main's exit
// code. One volume per process: `backend` must outlive the call.
int CampielloFuseMain(int argc, char** argv, PeerBackend& backend);

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_FUSE_CAMPIELLOFUSE_H
