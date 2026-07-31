/*
 * campiello_smoke.c
 *
 * Minimal userlandfs FUSE filesystem that mounts an empty, read-only volume. This is an
 * M0/M1 smoke test to prove the userlandfs mount plumbing end to end (a volume appears in
 * Tracker), NOT the real Fondamenta: native mode will use the Haiku-native userlandfs
 * interface for typed attribute write (decision C, docs/VERIFIED.md section 1). Here the
 * FUSE bridge's read-only, type-lossy limitation does not matter because the volume is
 * empty.
 *
 * Model (verified against the Haiku userlandfs source): a userlandfs FUSE filesystem is a
 * shared library placed in add-ons/userlandfs/<name> that links libuserlandfs_fuse.so and
 * exports a main() symbol. userlandfs_create_file_system (in the linked library) looks up
 * main() via get_image_symbol and runs it; main() calls fuse_main() with the operation
 * table. See server/fuse/FUSEFileSystem.cpp:604 and server/fuse/fuse_main.cpp.
 *
 * Build and mount: see the Makefile and README.md in this directory.
 */

#define FUSE_USE_VERSION 26

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <fuse.h>

/* The volume has exactly one node: the root directory, which is empty. */

static int
campiello_getattr(const char* path, struct stat* st)
{
	memset(st, 0, sizeof(*st));
	if (strcmp(path, "/") == 0) {
		st->st_mode = S_IFDIR | 0555; /* read + execute, no write */
		st->st_nlink = 2;
		return 0;
	}
	return -ENOENT;
}

static int
campiello_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
	struct fuse_file_info* fi)
{
	(void)offset;
	(void)fi;
	if (strcmp(path, "/") != 0)
		return -ENOENT;

	/* An empty directory still lists the two standard entries. */
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	return 0;
}

static int
campiello_statfs(const char* path, struct statvfs* st)
{
	(void)path;
	memset(st, 0, sizeof(*st));
	st->f_namemax = 255;
	return 0;
}

static struct fuse_operations campiello_ops = {
	.getattr = campiello_getattr,
	.readdir = campiello_readdir,
	.statfs  = campiello_statfs,
};

int
main(int argc, char* argv[])
{
	return fuse_main(argc, argv, &campiello_ops, NULL);
}
