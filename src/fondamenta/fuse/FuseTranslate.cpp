// FuseTranslate.cpp
//
// See FuseTranslate.h.

#include "FuseTranslate.h"

#include <cerrno>
#include <cstring>

namespace campiello {
namespace fondamenta {

void FillStat(const wire::Stat& in, struct stat* out)
{
	std::memset(out, 0, sizeof(*out));
	out->st_mode = in.mode;
	out->st_size = static_cast<off_t>(in.size);
	out->st_ino = static_cast<ino_t>(in.inode);
	out->st_nlink = S_ISDIR(in.mode) ? 2 : 1;
	out->st_mtim.tv_sec = static_cast<time_t>(in.mtime);
#ifdef __HAIKU__
	// Haiku carries a real creation time; elsewhere fold it into ctime for a sensible value.
	out->st_crtim.tv_sec = static_cast<time_t>(in.crtime);
#endif
	out->st_ctim.tv_sec = static_cast<time_t>(in.crtime);
}

int ErrnoFor(BackendStatus status)
{
	switch (status) {
		case BackendStatus::kOk:             return 0;
		case BackendStatus::kNotFound:       return -ENOENT;
		case BackendStatus::kAccessDenied:   return -EACCES;
		case BackendStatus::kNotADirectory:  return -ENOTDIR;
		case BackendStatus::kIsADirectory:   return -EISDIR;
		case BackendStatus::kUnsupported:    return -ENOSYS;
		case BackendStatus::kBadHandle:      return -EBADF;
		case BackendStatus::kInvalidRequest: return -EINVAL;
		case BackendStatus::kIoError:
		case BackendStatus::kTransportError:
		case BackendStatus::kProtocolError:
		default:                             return -EIO;
	}
}

} // namespace fondamenta
} // namespace campiello
