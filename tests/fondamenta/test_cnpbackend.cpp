// test_cnpbackend.cpp
//
// End-to-end through the client abstraction: a CnpBackend (implementing PeerBackend) reads a
// real local directory served by a daemon+FileServer. Covers Hello, ReadDir, Stat (with
// typed attributes on Haiku), Open/Read/Close returning the real bytes, and error mapping
// (not-found, not-a-directory, access-denied escape, bad handle). Plain transport.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <mutex>

#include "../../src/fondamenta/backend/CnpBackend.h"
#include "../../src/fondamenta/backend/LiveQueryClient.h"
#include "../../src/traghetto/server/Daemon.h"
#include "../../src/traghetto/server/FileServer.h"
#include "../../src/traghetto/transport/Connection.h"
#include "../../src/traghetto/wire/Handshake.h"

#ifdef __HAIKU__
#include <TypeConstants.h>
#include <fs_attr.h>
#endif

using namespace campiello;
using campiello::net::Connection;
using campiello::net::Listener;
using campiello::net::Daemon;
using campiello::net::PlainChannelFactory;
using campiello::net::FileServerFactory;
using campiello::fondamenta::CnpBackend;
using campiello::fondamenta::LiveQueryClient;
using campiello::fondamenta::BackendStatus;

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

static const char* kRoot = "cnp_be_test.d";
static const std::string kContent = "Round-trip completo del CnpBackend.\n";

static wire::NodeIdentity MakeId(const std::string& name)
{
	wire::NodeIdentity id;
	id.version = 1;
	id.node = name;
	id.caps = { wire::kCapBfs };
	id.fingerprint = std::vector<uint8_t>(wire::kFingerprintBytes, 0x11);
	return id;
}

static std::string Root(const std::string& sub) { return std::string(kRoot) + sub; }

static void WriteFile(const std::string& path, const std::string& content)
{
	int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd >= 0) {
		(void)!::write(fd, content.data(), content.size());
		::close(fd);
	}
}

static void Cleanup()
{
	::unlink(Root("/nota.txt").c_str());
	::unlink(Root("/cnpq_zx9_marker.txt").c_str());
	::unlink(Root("/cnpq_live_zt7.txt").c_str());
	::unlink(Root("/musica/canzone.txt").c_str());
	::rmdir(Root("/musica").c_str());
	::unlink(Root("/scritto.txt").c_str());
	::unlink(Root("/mosso.txt").c_str());
	::rmdir(Root("/sub").c_str());
	::rmdir(kRoot);
}

static void Setup()
{
	Cleanup();
	::mkdir(kRoot, 0755);
	::mkdir(Root("/musica").c_str(), 0755);
	WriteFile(Root("/nota.txt"), kContent);
	WriteFile(Root("/musica/canzone.txt"), "la la la");
	// A distinctively named file for the distributed-query test (a name query uses the always-live
	// BFS name index, so it is found immediately after creation).
	WriteFile(Root("/cnpq_zx9_marker.txt"), "marker");
#ifdef __HAIKU__
	int fd = ::open(Root("/nota.txt").c_str(), O_RDWR);
	if (fd >= 0) {
		int32 rating = 7;
		fs_write_attr(fd, "MyApp:rating", B_INT32_TYPE, 0, &rating, sizeof(rating));
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
	CnpBackend backend(conn);

	// Handshake through the backend.
	wire::NodeIdentity peer;
	CHECK(backend.Hello(MakeId("Ada"), peer) == BackendStatus::kOk);
	CHECK(peer.node == "Berto");
	CHECK(peer.HasCap("bfs"));

	// ReadDir.
	std::vector<wire::Entry> entries;
	CHECK(backend.ReadDir("/", entries) == BackendStatus::kOk);
	CHECK(HasEntry(entries, "nota.txt"));
	CHECK(HasEntry(entries, "musica"));

	// Stat.
	wire::Entry entry;
	CHECK(backend.Stat("/nota.txt", entry) == BackendStatus::kOk);
	CHECK(entry.name == "nota.txt");
	CHECK(entry.stat.size == kContent.size());
#ifdef __HAIKU__
	bool haveRating = false;
	for (const auto& a : entry.attrs)
		if (a.name == "MyApp:rating" && a.type == (uint32_t)B_INT32_TYPE)
			haveRating = true;
	CHECK(haveRating);
#endif

	// Open / Read / Close: real bytes back.
	uint64_t handle = 0, size = 0;
	CHECK(backend.Open("/nota.txt", handle, size) == BackendStatus::kOk);
	CHECK(size == kContent.size());
	std::vector<uint8_t> data;
	CHECK(backend.Read(handle, 0, 4096, data) == BackendStatus::kOk);
	CHECK(data.size() == kContent.size());
	CHECK(memcmp(data.data(), kContent.data(), data.size()) == 0);
	CHECK(backend.Close(handle) == BackendStatus::kOk);

	// Error mapping.
	CHECK(backend.Stat("/nope", entry) == BackendStatus::kNotFound);
	CHECK(backend.ReadDir("/nota.txt", entries) == BackendStatus::kNotADirectory);
	CHECK(backend.Stat("/..", entry) == BackendStatus::kAccessDenied);
	CHECK(backend.Read(987654, 0, 16, data) == BackendStatus::kBadHandle);

	// The read-only server refuses writes through the backend.
	CHECK(backend.Mkdir("/newdir", 0755) == BackendStatus::kAccessDenied);
	CHECK(backend.OpenWrite("/x.txt", handle) == BackendStatus::kAccessDenied);
	// ReadAttrs is a read: allowed on the read-only server.
	wire::AttrSet roAttrs;
	CHECK(backend.ReadAttrs("/nota.txt", roAttrs) == BackendStatus::kOk);

	// Distributed query (M4): a name query resolves through the real BQuery on the shared root's
	// volume. The volume may not support queries (tolerate kUnsupported); when it does, the marker
	// file appears, named by its path relative to the share, and a no-match predicate is empty.
	{
		std::vector<wire::Entry> hits;
		BackendStatus qs = backend.Query("name==\"cnpq_zx9_marker.txt\"", hits);
		CHECK(qs == BackendStatus::kOk || qs == BackendStatus::kUnsupported);
		if (qs == BackendStatus::kOk)
			CHECK(HasEntry(hits, "cnpq_zx9_marker.txt"));
		std::vector<wire::Entry> none;
		BackendStatus ns = backend.Query("name==\"no_such_file_zzqq_9x.txt\"", none);
		CHECK(ns == BackendStatus::kOk || ns == BackendStatus::kUnsupported);
		if (ns == BackendStatus::kOk)
			CHECK(none.empty());
	}

	// --- Write path against a second daemon serving the same root read-write. ---
	FileServerFactory rwFactory(kRoot, MakeId("Berto"), /*writable=*/true);
	Listener rwListener;
	CHECK(rwListener.Listen("127.0.0.1", 0));
	uint16_t rwPort = rwListener.Port();
	Daemon rwDaemon(rwListener, channelFactory, rwFactory);
	CHECK(rwDaemon.Start());
	Connection rwConn;
	CHECK(Connect("127.0.0.1", rwPort, rwConn));
	CnpBackend rw(rwConn);
	CHECK(rw.Hello(MakeId("Ada"), peer) == BackendStatus::kOk);

	// OpenWrite -> Write -> Close, then read the content back over the same channel.
	uint64_t wh = 0, written = 0;
	CHECK(rw.OpenWrite("/scritto.txt", wh) == BackendStatus::kOk);
	std::vector<uint8_t> payload = {'C', 'a', 'm', 'p', 'i', 'e', 'l', 'l', 'o'};
	CHECK(rw.Write(wh, 0, payload, written) == BackendStatus::kOk);
	CHECK(written == payload.size());
	CHECK(rw.Close(wh) == BackendStatus::kOk);

	wire::Entry we;
	CHECK(rw.Stat("/scritto.txt", we) == BackendStatus::kOk);
	CHECK(we.stat.size == payload.size());
	uint64_t rh = 0, rs = 0;
	CHECK(rw.Open("/scritto.txt", rh, rs) == BackendStatus::kOk);
	std::vector<uint8_t> back;
	CHECK(rw.Read(rh, 0, 4096, back) == BackendStatus::kOk);
	CHECK(back == payload);
	CHECK(rw.Close(rh) == BackendStatus::kOk);

	// Namespace mutations, verified through Stat.
	CHECK(rw.Mkdir("/sub", 0755) == BackendStatus::kOk);
	CHECK(rw.Stat("/sub", we) == BackendStatus::kOk);
	CHECK(rw.Rename("/scritto.txt", "/mosso.txt") == BackendStatus::kOk);
	CHECK(rw.Stat("/scritto.txt", we) == BackendStatus::kNotFound);
	CHECK(rw.Truncate("/mosso.txt", 4) == BackendStatus::kOk);
	CHECK(rw.Stat("/mosso.txt", we) == BackendStatus::kOk && we.stat.size == 4);
	CHECK(rw.Unlink("/mosso.txt") == BackendStatus::kOk);
	CHECK(rw.Stat("/mosso.txt", we) == BackendStatus::kNotFound);
	CHECK(rw.Unlink("/sub") == BackendStatus::kOk);

	// WRITE_ATTRS accepted (and round-trips a typed attribute on Haiku).
	wire::AttrSet toWrite;
	{ wire::Attr a; a.name = "MyApp:tag"; a.type = 0x43535452 /*B_STRING_TYPE*/;
	  a.value = std::vector<uint8_t>{'h', 'i', 0}; toWrite.push_back(a); }
	CHECK(rw.WriteAttrs("/nota.txt", toWrite) == BackendStatus::kOk);
#ifdef __HAIKU__
	wire::AttrSet after;
	CHECK(rw.ReadAttrs("/nota.txt", after) == BackendStatus::kOk);
	bool found = false;
	for (const auto& a : after)
		if (a.name == "MyApp:tag" && a.type == 0x43535452u)
			found = true;
	CHECK(found);
#endif

	rwConn.Close();
	rwDaemon.Stop();

	conn.Close();
	daemon.Stop();
	// Live distributed query end to end: open a live query, then change a matching file on the
	// server and confirm the QUERY_UPDATE is pushed back to the client. Uses its own daemon +
	// connection (the query stays open for the duration, so it wants a dedicated channel).
	{
		FileServerFactory liveFactory(kRoot, MakeId("Berto"));
		Listener liveListener;
		CHECK(liveListener.Listen("127.0.0.1", 0));
		uint16_t livePort = liveListener.Port();
		Daemon liveDaemon(liveListener, channelFactory, liveFactory);
		CHECK(liveDaemon.Start());

		Connection liveConn;
		CHECK(Connect("127.0.0.1", livePort, liveConn));
		{
			LiveQueryClient live(liveConn, 555);
			std::mutex umx;
			std::vector<LiveQueryClient::Update> updates;
			std::vector<wire::Entry> initial;
			bool opened = live.Open("name==\"cnpq_live_zt7.txt\"", initial,
				[&](const LiveQueryClient::Update& u) {
					std::lock_guard<std::mutex> lock(umx);
					updates.push_back(u);
				});
			CHECK(opened);
			if (opened) {
				CHECK(initial.empty()); // the live file does not exist yet

				auto sawUpdate = [&](bool added) {
					for (int i = 0; i < 60; ++i) {
						{
							std::lock_guard<std::mutex> lock(umx);
							for (const auto& u : updates)
								if (u.added == added && u.entry.name == "cnpq_live_zt7.txt")
									return true;
						}
						usleep(100000); // 100 ms; live node-monitor events are near-instant
					}
					return false;
				};

				WriteFile(Root("/cnpq_live_zt7.txt"), "live"); // appears -> added update
				CHECK(sawUpdate(true));
				::unlink(Root("/cnpq_live_zt7.txt").c_str()); // vanishes -> removed update
				CHECK(sawUpdate(false));

				live.Close();
			}
		}
	}

	Cleanup();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
