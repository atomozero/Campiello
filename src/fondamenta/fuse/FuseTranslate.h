// FuseTranslate.h
//
// The pure translation between Campiello's portable backend types and the POSIX shapes the
// FUSE front end hands to the kernel: a wire::Stat becomes a struct stat, and a BackendStatus
// becomes the negated errno FUSE callbacks must return. Kept separate from the FUSE glue
// (CampielloFuse) so it needs no fuse.h and is unit-testable off Haiku.
//
// Pure standard C++ + POSIX <sys/stat.h>; no Haiku, no libfuse.

#ifndef CAMPIELLO_FONDAMENTA_FUSE_FUSETRANSLATE_H
#define CAMPIELLO_FONDAMENTA_FUSE_FUSETRANSLATE_H

#include <sys/stat.h>

#include "../backend/PeerBackend.h"       // BackendStatus
#include "../../traghetto/wire/Listing.h" // wire::Stat

namespace campiello {
namespace fondamenta {

// Fill `*out` from a wire::Stat. `*out` is fully zeroed first, then mode, size, inode, and the
// modification/creation times are set; link count is 2 for a directory, 1 otherwise. Fields
// FUSE ignores (st_dev, st_blksize) are left zero.
void FillStat(const wire::Stat& in, struct stat* out);

// The value a FUSE callback should return for `status`: 0 for kOk, else the negated errno
// (e.g. kNotFound -> -ENOENT). kUnsupported maps to -ENOSYS; transport/protocol failures and
// anything unmapped map to -EIO.
int ErrnoFor(BackendStatus status);

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_FUSE_FUSETRANSLATE_H
