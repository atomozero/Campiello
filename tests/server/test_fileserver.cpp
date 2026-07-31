// test_fileserver.cpp
//
// End-to-end: a CNP client talks to a daemon whose handler is a FileServer serving a real
// local directory. Exercises LIST/STAT/OPEN/READ/CLOSE against real files (and BFS
// attributes on Haiku), plus the security guards: `..` and symlink escapes, listing a file,
// a missing path, and a bad handle are all rejected. Plain transport (no cert setup).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/traghetto/dispatch/Dispatch.h"
#include "../../src/traghetto/server/Daemon.h"
#include "../../src/traghetto/server/FileServer.h"
#include "../../src/traghetto/transport/Connection.h"
#include "../../src/traghetto/wire/Error.h"
#include "../../src/traghetto/wire/FileOps.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/Handshake.h"
#include "../../src/traghetto/wire/Listing.h"

#ifdef __HAIKU__
#include <TypeConstants.h>
#include <fs_attr.h>
#endif

using namespace campiello;
using campiello::net::Connection;
using campiello::net::Listener;
using campiello::net::Client;
using campiello::net::Daemon;
using campiello::net::PlainChannelFactory;
using campiello::net::FileServerFactory;

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

static const char* kRoot = "cnp_fs_test.d";
static const std::string kContent = "Ciao dal file server di Campiello.\n";

static wire::NodeIdentity MakeId(const std::string& name)
{
	wire::NodeIdentity id;
	id.version = 1;
	id.node = name;
	id.caps = { wire::kCapBfs };
	id.fingerprint = std::vector<uint8_t>(wire::kFingerprintBytes, 0x11);
	return id;
}

static void WriteFile(const std::string& path, const std::string& content)
{
	int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd >= 0) {
		(void)!::write(fd, content.data(), content.size());
		::close(fd);
	}
}

static std::string Root(const std::string& sub) { return std::string(kRoot) + sub; }

static void Cleanup()
{
	::unlink(Root("/escape").c_str());
	::unlink(Root("/nota.txt").c_str());
	::unlink(Root("/musica/canzone.txt").c_str());
	::rmdir(Root("/musica").c_str());
	::rmdir(kRoot);
}

static void Setup()
{
	Cleanup();
	::mkdir(kRoot, 0755);
	::mkdir(Root("/musica").c_str(), 0755);
	WriteFile(Root("/nota.txt"), kContent);
	WriteFile(Root("/musica/canzone.txt"), "la la la");
	// A symlink inside the share pointing outside it: an escape the server must refuse.
	::symlink("/", Root("/escape").c_str());
#ifdef __HAIKU__
	int fd = ::open(Root("/nota.txt").c_str(), O_RDWR);
	if (fd >= 0) {
		int32 rating = 5;
		fs_write_attr(fd, "MyApp:rating", B_INT32_TYPE, 0, &rating, sizeof(rating));
		const char* comment = "ciao";
		fs_write_attr(fd, "MyApp:comment", B_STRING_TYPE, 0, comment, strlen(comment) + 1);
		::close(fd);
	}
#endif
}

static bool HasEntry(const std::vector<wire::Entry>& entries, const std::string& name)
{
	for (const auto& e : entries)
		if (e.name == name)
			return true;
	return false;
}

static wire::ErrorCode ErrorOf(const wire::Frame& f)
{
	wire::ErrorReply e;
	if (f.type != wire::MessageType::kError || !wire::DecodeError(f.payload, e))
		return wire::ErrorCode::kInternal;
	return (wire::ErrorCode)e.code;
}

int main()
{
	Setup();

	FileServerFactory handlerFactory(kRoot, MakeId("Berto"));
	CHECK(handlerFactory.IsValid());

	Listener listener;
	CHECK(listener.Listen("127.0.0.1", 0));
	uint16_t port = listener.Port();

	PlainChannelFactory channelFactory;
	Daemon daemon(listener, channelFactory, handlerFactory);
	CHECK(daemon.Start());

	Connection conn;
	CHECK(Connect("127.0.0.1", port, conn));
	Client rpc(conn);
	wire::Frame rep;

	// Handshake.
	CHECK(rpc.Request(wire::MakeHello(MakeId("Ada"), 0), rep));
	CHECK(rep.type == wire::MessageType::kWelcome);

	// LIST the shared root: the real files are there.
	CHECK(rpc.Request(wire::MakeListRequest("/", 0), rep));
	CHECK(rep.type == wire::MessageType::kList);
	std::vector<wire::Entry> entries;
	CHECK(wire::DecodeListing(rep.payload, entries));
	CHECK(HasEntry(entries, "nota.txt"));
	CHECK(HasEntry(entries, "musica"));

	// STAT a real file.
	CHECK(rpc.Request(wire::MakeStatRequest("/nota.txt", 0), rep));
	CHECK(rep.type == wire::MessageType::kStat);
	wire::Entry entry;
	CHECK(wire::DecodeStatReply(rep.payload, entry));
	CHECK(entry.name == "nota.txt");
	CHECK(entry.stat.size == kContent.size());
#ifdef __HAIKU__
	// Attributes travel with their types.
	bool haveRating = false, haveComment = false;
	for (const auto& a : entry.attrs) {
		if (a.name == "MyApp:rating" && a.type == (uint32_t)B_INT32_TYPE)
			haveRating = true;
		if (a.name == "MyApp:comment" && a.type == (uint32_t)B_STRING_TYPE)
			haveComment = true;
	}
	CHECK(haveRating);
	CHECK(haveComment);
#endif

	// OPEN / READ / CLOSE: read the real content back.
	CHECK(rpc.Request(wire::MakeOpenRequest("/nota.txt", wire::kOpenRead, 0), rep));
	CHECK(rep.type == wire::MessageType::kOpen);
	uint64_t handle = 0, size = 0;
	CHECK(wire::DecodeOpenReply(rep.payload, handle, size));
	CHECK(size == kContent.size());

	CHECK(rpc.Request(wire::MakeReadRequest(handle, 0, 4096, 0), rep));
	CHECK(rep.type == wire::MessageType::kRead);
	std::vector<uint8_t> data;
	CHECK(wire::DecodeReadReply(rep.payload, data));
	CHECK(data.size() == kContent.size());
	CHECK(memcmp(data.data(), kContent.data(), data.size()) == 0);

	CHECK(rpc.Request(wire::MakeCloseRequest(handle, 0), rep));
	CHECK(rep.type == wire::MessageType::kClose);
	CHECK(wire::DecodeOk(rep.payload));

	// Security: `..` traversal is refused.
	CHECK(rpc.Request(wire::MakeStatRequest("/..", 0), rep));
	CHECK(ErrorOf(rep) == wire::ErrorCode::kAccessDenied);

	// Security: a symlink pointing outside the share is refused.
	CHECK(rpc.Request(wire::MakeStatRequest("/escape", 0), rep));
	CHECK(ErrorOf(rep) == wire::ErrorCode::kAccessDenied);

	// Wrong-kind and missing-path errors.
	CHECK(rpc.Request(wire::MakeListRequest("/nota.txt", 0), rep));
	CHECK(ErrorOf(rep) == wire::ErrorCode::kNotADirectory);

	CHECK(rpc.Request(wire::MakeStatRequest("/nope", 0), rep));
	CHECK(ErrorOf(rep) == wire::ErrorCode::kNotFound);

	// A bogus read handle is refused.
	CHECK(rpc.Request(wire::MakeReadRequest(999999, 0, 16, 0), rep));
	CHECK(ErrorOf(rep) == wire::ErrorCode::kBadHandle);

	conn.Close();
	daemon.Stop();
	Cleanup();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
