/*
 * campiello_passthrough.c
 *
 * Read-only passthrough userlandfs FUSE filesystem: it mirrors a real local directory so
 * real files (with their BFS attributes) appear as a mounted volume in Tracker. This is
 * the second smoke test, extending campiello_smoke.c from an empty volume to real content.
 *
 * Two purposes:
 *   1. See real files and read their content through a Campiello-built mount (M1 shape).
 *   2. Empirically exercise the attribute path. The userlandfs FUSE bridge is read-only on
 *      attributes and flattens their type to B_RAW_TYPE except the MIME type (verified in
 *      M0, docs/VERIFIED.md). Mounting a directory whose files carry typed BFS attributes
 *      lets us observe that flattening at runtime, confirming why native mode must use the
 *      Haiku-native userlandfs interface instead (decision C).
 *
 * This is a test artifact, not the real Fondamenta. It is read-only and passes attribute
 * VALUES through FUSE getxattr/listxattr; the type loss is inherent to the FUSE channel.
 *
 * The backing directory is the first mount parameter token after the fs name, e.g.
 *   mount -t userlandfs -p "campiello_passthrough /boot/home/campiello_share" <dir>
 * falling back to a compile-time default.
 */

#define FUSE_USE_VERSION 26

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <fs_attr.h>

#include <fuse.h>

static char gRoot[PATH_MAX] = "/boot/home/campiello_share";

/* Map a mount-relative path onto the backing directory. */
static void
build_path(char* out, size_t outSize, const char* path)
{
	if (strcmp(path, "/") == 0)
		snprintf(out, outSize, "%s", gRoot);
	else
		snprintf(out, outSize, "%s%s", gRoot, path); /* path begins with '/' */
}

static int
campiello_getattr(const char* path, struct stat* st)
{
	char full[PATH_MAX];
	build_path(full, sizeof(full), path);
	if (lstat(full, st) < 0)
		return -errno;
	return 0;
}

static int
campiello_readlink(const char* path, char* buf, size_t size)
{
	char full[PATH_MAX];
	build_path(full, sizeof(full), path);
	ssize_t n = readlink(full, buf, size - 1);
	if (n < 0)
		return -errno;
	buf[n] = '\0';
	return 0;
}

static int
campiello_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
	struct fuse_file_info* fi)
{
	(void)offset;
	(void)fi;
	char full[PATH_MAX];
	build_path(full, sizeof(full), path);
	DIR* dir = opendir(full);
	if (dir == NULL)
		return -errno;
	struct dirent* de;
	while ((de = readdir(dir)) != NULL)
		filler(buf, de->d_name, NULL, 0);
	closedir(dir);
	return 0;
}

static int
campiello_open(const char* path, struct fuse_file_info* fi)
{
	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES; /* read-only mount */
	char full[PATH_MAX];
	build_path(full, sizeof(full), path);
	int fd = open(full, O_RDONLY);
	if (fd < 0)
		return -errno;
	fi->fh = (uint64_t)fd;
	return 0;
}

static int
campiello_read(const char* path, char* buf, size_t size, off_t offset,
	struct fuse_file_info* fi)
{
	(void)path;
	ssize_t n = pread((int)fi->fh, buf, size, offset);
	if (n < 0)
		return -errno;
	return (int)n;
}

static int
campiello_release(const char* path, struct fuse_file_info* fi)
{
	(void)path;
	close((int)fi->fh);
	return 0;
}

/* listxattr: enumerate the backing file's BFS attribute names, null-separated. The FUSE
 * bridge calls this first with size 0 to learn the length, then again with a buffer. */
static int
campiello_listxattr(const char* path, char* list, size_t size)
{
	char full[PATH_MAX];
	build_path(full, sizeof(full), path);
	DIR* dir = fs_open_attr_dir(full);
	if (dir == NULL)
		return -errno;

	size_t needed = 0;
	struct dirent* de;
	while ((de = fs_read_attr_dir(dir)) != NULL)
		needed += strlen(de->d_name) + 1;

	if (size == 0) {
		fs_close_attr_dir(dir);
		return (int)needed;
	}
	if (size < needed) {
		fs_close_attr_dir(dir);
		return -ERANGE;
	}

	fs_rewind_attr_dir(dir);
	size_t off = 0;
	while ((de = fs_read_attr_dir(dir)) != NULL) {
		size_t len = strlen(de->d_name) + 1;
		memcpy(list + off, de->d_name, len);
		off += len;
	}
	fs_close_attr_dir(dir);
	return (int)off;
}

/* getxattr: read one BFS attribute's raw bytes. The type is known here (fs_stat_attr) but
 * cannot travel through the FUSE xattr channel, which is exactly the M0 limitation. */
static int
campiello_getxattr(const char* path, const char* name, char* value, size_t size)
{
	char full[PATH_MAX];
	build_path(full, sizeof(full), path);
	int fd = open(full, O_RDONLY);
	if (fd < 0)
		return -errno;

	struct attr_info info;
	if (fs_stat_attr(fd, name, &info) < 0) {
		int err = errno;
		close(fd);
		return err ? -err : -ENOENT;
	}
	if (size == 0) {
		close(fd);
		return (int)info.size;
	}
	if (size < (size_t)info.size) {
		close(fd);
		return -ERANGE;
	}
	ssize_t n = fs_read_attr(fd, name, info.type, 0, value, (size_t)info.size);
	int err = errno;
	close(fd);
	if (n < 0)
		return -err;
	return (int)n;
}

static int
campiello_statfs(const char* path, struct statvfs* st)
{
	(void)path;
	if (statvfs(gRoot, st) < 0)
		return -errno;
	return 0;
}

static struct fuse_operations campiello_ops = {
	.getattr   = campiello_getattr,
	.readlink  = campiello_readlink,
	.readdir   = campiello_readdir,
	.open      = campiello_open,
	.read      = campiello_read,
	.release   = campiello_release,
	.listxattr = campiello_listxattr,
	.getxattr  = campiello_getxattr,
	.statfs    = campiello_statfs,
};

int
main(int argc, char* argv[])
{
	/* The first parameter token (argv[1]) selects the backing directory. */
	if (argc > 1 && argv[1][0] != '-')
		snprintf(gRoot, sizeof(gRoot), "%s", argv[1]);

	/* userlandfs's fuse_main ignores the mountpoint (it uses an internal channel), so we
	 * hand it just the program name. */
	char* fuseArgv[] = { argv[0] };
	return fuse_main(1, fuseArgv, &campiello_ops, NULL);
}
