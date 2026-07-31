// CampielloFuse.cpp
//
// See CampielloFuse.h. Haiku-only: built against the userlandfs FUSE headers.

#define FUSE_USE_VERSION 26

#include "CampielloFuse.h"

#include <errno.h>
#include <fcntl.h>

#include <cstring>
#include <mutex>
#include <vector>

#include <fuse.h>

#include "FuseTranslate.h"

namespace campiello {
namespace fondamenta {

namespace {

// One volume per userlandfs add-on process. FUSE callbacks are plain C function pointers with
// no user parameter, so the backend + its lock live here, set by CampielloFuseMain before the
// event loop starts.
struct Context {
	PeerBackend* backend = nullptr;
	std::mutex   mutex;
};

Context gContext;

int cf_getattr(const char* path, struct stat* st)
{
	std::lock_guard<std::mutex> lock(gContext.mutex);
	wire::Entry entry;
	BackendStatus s = gContext.backend->Stat(path, entry);
	if (s != BackendStatus::kOk)
		return ErrnoFor(s);
	FillStat(entry.stat, st);
	return 0;
}

int cf_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
	struct fuse_file_info* fi)
{
	(void)offset;
	(void)fi;
	std::lock_guard<std::mutex> lock(gContext.mutex);
	std::vector<wire::Entry> entries;
	BackendStatus s = gContext.backend->ReadDir(path, entries);
	if (s != BackendStatus::kOk)
		return ErrnoFor(s);

	filler(buf, ".", nullptr, 0);
	filler(buf, "..", nullptr, 0);
	for (const wire::Entry& e : entries) {
		struct stat st;
		FillStat(e.stat, &st);
		if (filler(buf, e.name.c_str(), &st, 0) != 0)
			break; // buffer full
	}
	return 0;
}

int cf_open(const char* path, struct fuse_file_info* fi)
{
	std::lock_guard<std::mutex> lock(gContext.mutex);
	uint64_t handle = 0, size = 0;
	// A read open goes through Open; any write intent through OpenWrite. On a read-only mount the
	// kernel blocks write-intent opens before they reach us, so this stays safe there.
	BackendStatus s = ((fi->flags & O_ACCMODE) == O_RDONLY)
		? gContext.backend->Open(path, handle, size)
		: gContext.backend->OpenWrite(path, handle);
	if (s != BackendStatus::kOk)
		return ErrnoFor(s);
	fi->fh = handle;
	return 0;
}

int cf_create(const char* path, mode_t mode, struct fuse_file_info* fi)
{
	(void)mode;
	std::lock_guard<std::mutex> lock(gContext.mutex);
	uint64_t handle = 0;
	BackendStatus s = gContext.backend->OpenWrite(path, handle);
	if (s != BackendStatus::kOk)
		return ErrnoFor(s);
	fi->fh = handle;
	return 0;
}

int cf_write(const char* path, const char* buf, size_t size, off_t offset,
	struct fuse_file_info* fi)
{
	(void)path;
	std::lock_guard<std::mutex> lock(gContext.mutex);
	std::vector<uint8_t> data(buf, buf + size);
	uint64_t written = 0;
	BackendStatus s = gContext.backend->Write(fi->fh, static_cast<uint64_t>(offset), data, written);
	if (s != BackendStatus::kOk)
		return ErrnoFor(s);
	return static_cast<int>(written);
}

int cf_mkdir(const char* path, mode_t mode)
{
	std::lock_guard<std::mutex> lock(gContext.mutex);
	return ErrnoFor(gContext.backend->Mkdir(path, static_cast<uint32_t>(mode)));
}

int cf_unlink(const char* path)
{
	std::lock_guard<std::mutex> lock(gContext.mutex);
	return ErrnoFor(gContext.backend->Unlink(path));
}

int cf_rmdir(const char* path)
{
	std::lock_guard<std::mutex> lock(gContext.mutex);
	return ErrnoFor(gContext.backend->Unlink(path)); // Unlink removes a file or an empty directory
}

int cf_rename(const char* from, const char* to)
{
	std::lock_guard<std::mutex> lock(gContext.mutex);
	return ErrnoFor(gContext.backend->Rename(from, to));
}

int cf_truncate(const char* path, off_t size)
{
	std::lock_guard<std::mutex> lock(gContext.mutex);
	return ErrnoFor(gContext.backend->Truncate(path, static_cast<uint64_t>(size)));
}

int cf_ftruncate(const char* path, off_t size, struct fuse_file_info* fi)
{
	(void)fi;
	std::lock_guard<std::mutex> lock(gContext.mutex);
	return ErrnoFor(gContext.backend->Truncate(path, static_cast<uint64_t>(size)));
}

// ---- Extended attributes (BFS attribute read through the mount) ------------------------------
// The userlandfs FUSE bridge maps Haiku attribute read/list hooks onto FUSE getxattr/listxattr
// (verified against FUSEVolume.cpp; see docs/VERIFIED.md section 1). We serve those from the CNP
// READ_ATTRS path: READ_ATTRS returns the whole-node AttrSet, so listxattr enumerates the names and
// getxattr picks one. There is no separate LIST_ATTRS op.
//
// Attribute WRITE through the FUSE bridge is a documented limitation: the Haiku bridge never calls
// fuse_fs_setxattr, and the FUSE type surface is lossy (everything B_RAW_TYPE). setxattr is wired
// below for completeness and for any non-Haiku FUSE host, but on Haiku it is inert; typed attribute
// write to a Campiello peer is the WRITE_ATTRS wire op driven by the native userlandfs path
// (decision C), not this FUSE bridge.
int cf_getxattr(const char* path, const char* name, char* value, size_t size)
{
	std::lock_guard<std::mutex> lock(gContext.mutex);
	wire::AttrSet set;
	BackendStatus s = gContext.backend->ReadAttrs(path, set);
	if (s != BackendStatus::kOk)
		return ErrnoFor(s);
	for (const wire::Attr& a : set) {
		if (a.name == name) {
			if (size == 0)
				return static_cast<int>(a.value.size()); // size probe
			if (size < a.value.size())
				return -ERANGE;
			std::memcpy(value, a.value.data(), a.value.size());
			return static_cast<int>(a.value.size());
		}
	}
	return -ENOENT; // no such attribute (Haiku has no ENOATTR/ENODATA)
}

int cf_setxattr(const char* path, const char* name, const char* value, size_t size, int flags)
{
	(void)flags; // XATTR_CREATE/REPLACE not distinguished; WRITE_ATTRS replaces the named attr
	std::lock_guard<std::mutex> lock(gContext.mutex);
	wire::AttrSet set;
	wire::Attr a;
	a.name = name;
	a.type = 0; // raw/unknown from the POSIX xattr surface (see caveat above)
	a.value.assign(reinterpret_cast<const uint8_t*>(value),
		reinterpret_cast<const uint8_t*>(value) + size);
	set.push_back(std::move(a));
	return ErrnoFor(gContext.backend->WriteAttrs(path, set));
}

int cf_listxattr(const char* path, char* list, size_t size)
{
	std::lock_guard<std::mutex> lock(gContext.mutex);
	wire::AttrSet set;
	BackendStatus s = gContext.backend->ReadAttrs(path, set);
	if (s != BackendStatus::kOk)
		return ErrnoFor(s);
	size_t total = 0;
	for (const wire::Attr& a : set)
		total += a.name.size() + 1; // each name is NUL-terminated
	if (size == 0)
		return static_cast<int>(total); // size probe
	if (size < total)
		return -ERANGE;
	char* p = list;
	for (const wire::Attr& a : set) {
		std::memcpy(p, a.name.c_str(), a.name.size() + 1);
		p += a.name.size() + 1;
	}
	return static_cast<int>(total);
}

int cf_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
	(void)path;
	std::lock_guard<std::mutex> lock(gContext.mutex);
	std::vector<uint8_t> data;
	BackendStatus s = gContext.backend->Read(fi->fh, static_cast<uint64_t>(offset),
		static_cast<uint32_t>(size), data);
	if (s != BackendStatus::kOk)
		return ErrnoFor(s);
	if (!data.empty())
		std::memcpy(buf, data.data(), data.size());
	return static_cast<int>(data.size()); // a short read signals EOF
}

int cf_release(const char* path, struct fuse_file_info* fi)
{
	(void)path;
	std::lock_guard<std::mutex> lock(gContext.mutex);
	gContext.backend->Close(fi->fh);
	return 0; // release's return value is ignored by FUSE
}

int cf_statfs(const char* path, struct statvfs* st)
{
	(void)path;
	std::memset(st, 0, sizeof(*st));
	st->f_namemax = 255;
	return 0;
}

} // namespace

int CampielloFuseMain(int argc, char** argv, PeerBackend& backend)
{
	gContext.backend = &backend;

	struct fuse_operations ops;
	std::memset(&ops, 0, sizeof(ops));
	ops.getattr   = cf_getattr;
	ops.readdir   = cf_readdir;
	ops.open      = cf_open;
	ops.read      = cf_read;
	ops.release   = cf_release;
	ops.statfs    = cf_statfs;
	// Write operations. On a read-only mount the kernel never calls these; on a read-write mount a
	// backend that does not implement them returns kUnsupported -> -ENOSYS, so this is safe to wire
	// for every backend.
	ops.create    = cf_create;
	ops.write     = cf_write;
	ops.mkdir     = cf_mkdir;
	ops.unlink    = cf_unlink;
	ops.rmdir     = cf_rmdir;
	ops.rename    = cf_rename;
	ops.truncate  = cf_truncate;
	ops.ftruncate = cf_ftruncate;
	ops.getxattr  = cf_getxattr;
	ops.setxattr  = cf_setxattr;
	ops.listxattr = cf_listxattr;

	return fuse_main(argc, argv, &ops, nullptr);
}

} // namespace fondamenta
} // namespace campiello
