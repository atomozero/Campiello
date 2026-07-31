// test_fusetranslate.cpp
//
// Tests the FUSE translation layer: wire::Stat -> struct stat (mode, size, inode, link count,
// times, and that the rest is zeroed) and BackendStatus -> negated errno. Pure standard C++,
// no fuse.h, no framework; non-zero exit on failure.

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/stat.h>

#include "../../src/fondamenta/fuse/FuseTranslate.h"

using namespace campiello::fondamenta;
using campiello::wire::Stat;

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                            \
	do {                                                                       \
		++gChecks;                                                             \
		if (!(cond)) {                                                         \
			++gFailures;                                                       \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
		}                                                                      \
	} while (0)

static void TestFillStatFile()
{
	Stat in;
	in.mode = S_IFREG | 0644;
	in.size = 123456;
	in.mtime = 1600000000;
	in.crtime = 1500000000;
	in.inode = 42;

	// Start from a dirty buffer to prove FillStat zeroes what it does not set.
	struct stat st;
	std::memset(&st, 0xAB, sizeof(st));
	FillStat(in, &st);

	CHECK(st.st_mode == (mode_t)(S_IFREG | 0644));
	CHECK(S_ISREG(st.st_mode));
	CHECK(st.st_size == 123456);
	CHECK(st.st_ino == 42);
	CHECK(st.st_nlink == 1);            // a regular file
	CHECK(st.st_mtim.tv_sec == 1600000000);
	CHECK(st.st_ctim.tv_sec == 1500000000);
	CHECK(st.st_dev == 0);              // untouched field zeroed
}

static void TestFillStatDir()
{
	Stat in;
	in.mode = S_IFDIR | 0755;
	in.size = 4096;
	in.inode = 2;

	struct stat st;
	FillStat(in, &st);
	CHECK(S_ISDIR(st.st_mode));
	CHECK(st.st_nlink == 2);            // a directory
	CHECK(st.st_size == 4096);
}

static void TestErrnoFor()
{
	CHECK(ErrnoFor(BackendStatus::kOk) == 0);
	CHECK(ErrnoFor(BackendStatus::kNotFound) == -ENOENT);
	CHECK(ErrnoFor(BackendStatus::kAccessDenied) == -EACCES);
	CHECK(ErrnoFor(BackendStatus::kNotADirectory) == -ENOTDIR);
	CHECK(ErrnoFor(BackendStatus::kIsADirectory) == -EISDIR);
	CHECK(ErrnoFor(BackendStatus::kUnsupported) == -ENOSYS);
	CHECK(ErrnoFor(BackendStatus::kBadHandle) == -EBADF);
	CHECK(ErrnoFor(BackendStatus::kInvalidRequest) == -EINVAL);
	// Transport/protocol/io all collapse to -EIO.
	CHECK(ErrnoFor(BackendStatus::kIoError) == -EIO);
	CHECK(ErrnoFor(BackendStatus::kTransportError) == -EIO);
	CHECK(ErrnoFor(BackendStatus::kProtocolError) == -EIO);
	// Every error maps to nonzero. The exact sign follows the -errno convention the userlandfs
	// FUSE bridge uses (the same as the M0 passthrough): note that on Haiku the errno macros are
	// themselves negative, so -errno is a positive value there. We assert equality with the
	// -EXXX macros above (portable) rather than a hard-coded sign.
	CHECK(ErrnoFor(BackendStatus::kNotFound) != 0);
	CHECK(ErrnoFor(BackendStatus::kNotFound) == -ENOENT);
}

int main()
{
	TestFillStatFile();
	TestFillStatDir();
	TestErrnoFor();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
