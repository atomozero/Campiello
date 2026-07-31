// test_filewrite.cpp
//
// The M3 write path through FileServer, driven directly (Handle() is public, no socket needed).
// A read-only server refuses every mutation with ACCESS_DENIED but still serves reads; a
// writable server actually creates, writes, renames, truncates, unlinks, and (on Haiku) writes
// typed attributes, verified against the real filesystem. Also the write-side security guards:
// parent-escape, a bad leaf, a symlink leaf redirect, and an invalid attribute name.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <climits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/traghetto/server/FileServer.h"
#include "../../src/traghetto/wire/AttrOps.h"
#include "../../src/traghetto/wire/Attributes.h"
#include "../../src/traghetto/wire/Error.h"
#include "../../src/traghetto/wire/FileOps.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/Handshake.h"
#include "../../src/traghetto/wire/Namespace.h"

#ifdef __HAIKU__
#include <fs_attr.h>
#endif

using namespace campiello;
using campiello::net::FileServer;

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

static const char* kRoot = "cnp_write_test.d";
static const uint32_t kStringType = 0x43535452; // B_STRING_TYPE 'CSTR'

static wire::NodeIdentity MakeId()
{
	wire::NodeIdentity id;
	id.version = 1;
	id.node = "Berto";
	id.caps = { wire::kCapBfs };
	id.fingerprint = std::vector<uint8_t>(wire::kFingerprintBytes, 0x11);
	return id;
}

static std::string Root(const std::string& sub) { return std::string(kRoot) + sub; }

static void WriteFile(const std::string& path, const std::string& content)
{
	int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd >= 0) { (void)!::write(fd, content.data(), content.size()); ::close(fd); }
}

static std::string ReadFile(const std::string& path)
{
	int fd = ::open(path.c_str(), O_RDONLY);
	if (fd < 0) return "";
	char buf[4096];
	ssize_t n = ::read(fd, buf, sizeof(buf));
	::close(fd);
	return (n > 0) ? std::string(buf, (size_t)n) : std::string();
}

static void Cleanup()
{
	::unlink(Root("/nota.txt").c_str());
	::unlink(Root("/new.txt").c_str());
	::unlink(Root("/renamed.txt").c_str());
	::unlink(Root("/link").c_str());
	::rmdir(Root("/sub").c_str());
	::rmdir(kRoot);
}

static wire::ErrorCode ErrorOf(const wire::Frame& f)
{
	wire::ErrorReply e;
	if (f.type != wire::MessageType::kError || !wire::DecodeError(f.payload, e))
		return wire::ErrorCode::kInternal;
	return (wire::ErrorCode)e.code;
}

static bool Exists(const std::string& path)
{
	struct stat st;
	return ::lstat(path.c_str(), &st) == 0;
}

int main()
{
	Cleanup();
	::mkdir(kRoot, 0755);
	WriteFile(Root("/nota.txt"), "ciao");
	// A symlink leaf pointing outside the share: writing through it must be refused.
	::symlink("/tmp", Root("/link").c_str());

	char canon[PATH_MAX];
	if (realpath(kRoot, canon) == nullptr) {
		std::printf("FAIL cannot resolve root\n");
		return 1;
	}
	FileServer ro(canon, MakeId(), false);
	FileServer rw(canon, MakeId(), true);

	using Bytes = std::vector<uint8_t>;
	wire::AttrSet attrs;
	{ wire::Attr a; a.name = "MyApp:tag"; a.type = kStringType;
	  a.value = Bytes{'x', 'y', 0}; attrs.push_back(a); }

	// --- Read-only server refuses every mutation, but still serves reads. ---
	CHECK(ErrorOf(ro.Handle(wire::MakeOpenRequest("/new.txt", wire::kOpenWrite, 0)))
		== wire::ErrorCode::kAccessDenied);
	CHECK(ErrorOf(ro.Handle(wire::MakeMkdirRequest("/sub", 0755, 0)))
		== wire::ErrorCode::kAccessDenied);
	CHECK(ErrorOf(ro.Handle(wire::MakeUnlinkRequest("/nota.txt", 0)))
		== wire::ErrorCode::kAccessDenied);
	CHECK(ErrorOf(ro.Handle(wire::MakeRenameRequest("/nota.txt", "/x.txt", 0)))
		== wire::ErrorCode::kAccessDenied);
	CHECK(ErrorOf(ro.Handle(wire::MakeTruncateRequest("/nota.txt", 0, 0)))
		== wire::ErrorCode::kAccessDenied);
	CHECK(ErrorOf(ro.Handle(wire::MakeWriteAttrsRequest("/nota.txt", attrs, 0)))
		== wire::ErrorCode::kAccessDenied);
	// A read is still allowed on the read-only server.
	CHECK(ro.Handle(wire::MakeReadAttrsRequest("/nota.txt", 0)).type
		== wire::MessageType::kReadAttrs);

	// --- Writable server: OPEN(write) -> WRITE -> CLOSE, verified on disk. ---
	wire::Frame r = rw.Handle(wire::MakeOpenRequest("/new.txt", wire::kOpenWrite, 0));
	CHECK(r.type == wire::MessageType::kOpen);
	uint64_t handle = 0, size = 0;
	CHECK(wire::DecodeOpenReply(r.payload, handle, size));
	CHECK(size == 0);

	Bytes payload = {'h', 'e', 'l', 'l', 'o'};
	r = rw.Handle(wire::MakeWriteRequest(handle, 0, payload, 0));
	CHECK(r.type == wire::MessageType::kWrite);
	uint64_t written = 0;
	CHECK(wire::DecodeWriteReply(r.payload, written));
	CHECK(written == payload.size());

	CHECK(wire::DecodeOk(rw.Handle(wire::MakeCloseRequest(handle, 0)).payload));
	CHECK(ReadFile(Root("/new.txt")) == "hello");

	// --- MKDIR, RENAME, TRUNCATE, UNLINK, all verified on disk. ---
	CHECK(rw.Handle(wire::MakeMkdirRequest("/sub", 0755, 0)).type == wire::MessageType::kMkdir);
	CHECK(Exists(Root("/sub")));

	CHECK(rw.Handle(wire::MakeRenameRequest("/new.txt", "/renamed.txt", 0)).type
		== wire::MessageType::kRename);
	CHECK(!Exists(Root("/new.txt")) && Exists(Root("/renamed.txt")));

	CHECK(rw.Handle(wire::MakeTruncateRequest("/renamed.txt", 2, 0)).type
		== wire::MessageType::kTruncate);
	CHECK(ReadFile(Root("/renamed.txt")) == "he");

	CHECK(rw.Handle(wire::MakeUnlinkRequest("/renamed.txt", 0)).type
		== wire::MessageType::kUnlink);
	CHECK(!Exists(Root("/renamed.txt")));
	CHECK(rw.Handle(wire::MakeUnlinkRequest("/sub", 0)).type == wire::MessageType::kUnlink);
	CHECK(!Exists(Root("/sub")));

	// --- WRITE_ATTRS: accepted; on Haiku the typed attribute round-trips. ---
	wire::Frame wa = rw.Handle(wire::MakeWriteAttrsRequest("/nota.txt", attrs, 0));
	CHECK(wa.type == wire::MessageType::kWriteAttrs && wire::DecodeOk(wa.payload));
#ifdef __HAIKU__
	{
		wire::Frame ra = rw.Handle(wire::MakeReadAttrsRequest("/nota.txt", 0));
		CHECK(ra.type == wire::MessageType::kReadAttrs);
		wire::AttrSet got;
		CHECK(wire::DecodeReadAttrsReply(ra.payload, got));
		bool found = false;
		for (const auto& a : got)
			if (a.name == "MyApp:tag" && a.type == kStringType)
				found = true;
		CHECK(found);
	}
#endif

	// --- Write-side security guards (on the writable server). ---
	// Parent escapes the root.
	CHECK(ErrorOf(rw.Handle(wire::MakeMkdirRequest("/../evil", 0755, 0)))
		== wire::ErrorCode::kAccessDenied);
	// A ".." leaf is rejected outright.
	CHECK(ErrorOf(rw.Handle(wire::MakeMkdirRequest("/..", 0755, 0)))
		== wire::ErrorCode::kInvalidRequest);
	// Writing through a symlink leaf that points outside is refused (O_NOFOLLOW).
	CHECK(ErrorOf(rw.Handle(wire::MakeOpenRequest("/link", wire::kOpenWrite, 0)))
		== wire::ErrorCode::kAccessDenied);
	// An attribute name with a control byte is rejected.
	{
		wire::AttrSet bad;
		wire::Attr a; a.name = std::string("\x01""bad", 4); a.type = kStringType;
		a.value = Bytes{'z'}; bad.push_back(a);
		CHECK(ErrorOf(rw.Handle(wire::MakeWriteAttrsRequest("/nota.txt", bad, 0)))
			== wire::ErrorCode::kInvalidRequest);
	}

	Cleanup();
	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
