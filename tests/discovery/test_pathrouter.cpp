// test_pathrouter.cpp
//
// Tests the discovery path router: the root lists peers, "/<peer>" is a synthetic directory,
// "/<peer>/..." delegates to that peer's backend, handles are namespaced across peers, and
// unknown / unreachable peers map to the right status. Pure standard C++, no framework; uses
// fake in-memory backends. Non-zero exit on failure.

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "../../src/fondamenta/discovery/PathRouter.h"
#include "../../src/fondamenta/discovery/QueryAggregator.h"

using namespace campiello;
using namespace campiello::fondamenta;

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

// A tiny in-memory PeerBackend: a flat set of files at the root. Its handles start at 1, so two
// instances hand out the SAME backend-handle numbers (exercising the router's namespacing).
class FakeBackend : public PeerBackend {
public:
	void AddFile(const std::string& name, const std::string& content)
	{
		fFiles["/" + name] = content;
	}

	BackendStatus Stat(const std::string& path, wire::Entry& out) override
	{
		if (path == "/") {
			out.name = "/";
			out.stat = wire::Stat{};
			out.stat.mode = S_IFDIR | 0555;
			return BackendStatus::kOk;
		}
		auto it = fFiles.find(path);
		if (it == fFiles.end())
			return BackendStatus::kNotFound;
		out.name = path.substr(1);
		out.stat = wire::Stat{};
		out.stat.mode = S_IFREG | 0444;
		out.stat.size = it->second.size();
		return BackendStatus::kOk;
	}

	BackendStatus ReadDir(const std::string& path, std::vector<wire::Entry>& out) override
	{
		if (path != "/")
			return BackendStatus::kNotADirectory;
		out.clear();
		for (const auto& kv : fFiles) {
			wire::Entry e;
			e.name = kv.first.substr(1);
			e.stat = wire::Stat{};
			e.stat.mode = S_IFREG | 0444;
			e.stat.size = kv.second.size();
			out.push_back(e);
		}
		return BackendStatus::kOk;
	}

	BackendStatus Open(const std::string& path, uint64_t& handle, uint64_t& size) override
	{
		auto it = fFiles.find(path);
		if (it == fFiles.end())
			return BackendStatus::kNotFound;
		handle = fNext++;
		fOpen[handle] = it->second;
		size = it->second.size();
		return BackendStatus::kOk;
	}

	BackendStatus Read(uint64_t handle, uint64_t offset, uint32_t length,
		std::vector<uint8_t>& out) override
	{
		auto it = fOpen.find(handle);
		if (it == fOpen.end())
			return BackendStatus::kBadHandle;
		const std::string& c = it->second;
		out.clear();
		for (uint64_t i = offset; i < c.size() && out.size() < length; ++i)
			out.push_back(static_cast<uint8_t>(c[i]));
		return BackendStatus::kOk;
	}

	BackendStatus Close(uint64_t handle) override
	{
		auto it = fOpen.find(handle);
		if (it == fOpen.end())
			return BackendStatus::kBadHandle;
		fOpen.erase(it);
		return BackendStatus::kOk;
	}

	// Stand-in query: match files whose name contains the predicate substring. `unsupported` makes
	// the fake report kUnsupported (to exercise the aggregator skipping non-query peers).
	bool unsupported = false;
	BackendStatus Query(const std::string& predicate, std::vector<wire::Entry>& out) override
	{
		if (unsupported)
			return BackendStatus::kUnsupported;
		out.clear();
		for (const auto& kv : fFiles) {
			std::string rel = kv.first.substr(1);
			if (rel.find(predicate) != std::string::npos) {
				wire::Entry e;
				e.name = rel;
				e.stat = wire::Stat{};
				e.stat.mode = S_IFREG | 0444;
				e.stat.size = kv.second.size();
				out.push_back(e);
			}
		}
		return BackendStatus::kOk;
	}

private:
	std::map<std::string, std::string> fFiles; // "/name" -> content
	std::map<uint64_t, std::string>    fOpen;
	uint64_t                           fNext = 1;
};

// Peer source: "Studio" and "Cucina" resolve to their backends; "Offline" is listed but has no
// backend (known-but-unreachable); anything else is unknown.
class FakeSource : public PeerSource {
public:
	FakeBackend studio;
	FakeBackend cucina;

	std::vector<std::string> PeerNames() override
	{
		return {"Studio", "Cucina", "Offline"};
	}

	PeerBackend* BackendFor(const std::string& name) override
	{
		if (name == "Studio") return &studio;
		if (name == "Cucina") return &cucina;
		return nullptr; // Offline / unknown
	}
};

static bool HasEntry(const std::vector<wire::Entry>& v, const std::string& name)
{
	for (const auto& e : v)
		if (e.name == name)
			return true;
	return false;
}

static std::string ToStr(const std::vector<uint8_t>& b)
{
	return std::string(b.begin(), b.end());
}

int main()
{
	FakeSource source;
	source.studio.AddFile("nota.txt", "ciao da Studio");
	source.cucina.AddFile("ricetta.txt", "ciao da Cucina");
	PathRouter router(source);

	// Root is a directory listing the peers.
	wire::Entry entry;
	CHECK(router.Stat("/", entry) == BackendStatus::kOk);
	CHECK(S_ISDIR(entry.stat.mode));
	std::vector<wire::Entry> entries;
	CHECK(router.ReadDir("/", entries) == BackendStatus::kOk);
	CHECK(HasEntry(entries, "Studio") && HasEntry(entries, "Cucina") && HasEntry(entries, "Offline"));

	// A peer folder is a synthetic directory (no backend call needed).
	CHECK(router.Stat("/Studio", entry) == BackendStatus::kOk);
	CHECK(S_ISDIR(entry.stat.mode));
	// An unknown peer is not found.
	CHECK(router.Stat("/Nessuno", entry) == BackendStatus::kNotFound);

	// Listing a peer delegates to its backend.
	CHECK(router.ReadDir("/Studio", entries) == BackendStatus::kOk);
	CHECK(HasEntry(entries, "nota.txt"));
	CHECK(router.ReadDir("/Cucina", entries) == BackendStatus::kOk);
	CHECK(HasEntry(entries, "ricetta.txt"));

	// Stat of a file inside a peer delegates.
	CHECK(router.Stat("/Studio/nota.txt", entry) == BackendStatus::kOk);
	CHECK(S_ISREG(entry.stat.mode));
	CHECK(entry.stat.size == std::string("ciao da Studio").size());
	CHECK(router.Stat("/Studio/manca.txt", entry) == BackendStatus::kNotFound);

	// A known-but-unreachable peer (no backend) reports a transport error when entered.
	CHECK(router.ReadDir("/Offline", entries) == BackendStatus::kTransportError);

	// Open/Read/Close across two peers: the router namespaces handles even though both fake
	// backends hand out backend-handle 1.
	uint64_t hStudio = 0, hCucina = 0, size = 0;
	CHECK(router.Open("/Studio/nota.txt", hStudio, size) == BackendStatus::kOk);
	CHECK(size == std::string("ciao da Studio").size());
	CHECK(router.Open("/Cucina/ricetta.txt", hCucina, size) == BackendStatus::kOk);
	CHECK(hStudio != hCucina); // distinct router handles despite equal backend handles

	std::vector<uint8_t> data;
	CHECK(router.Read(hStudio, 0, 4096, data) == BackendStatus::kOk);
	CHECK(ToStr(data) == "ciao da Studio");
	CHECK(router.Read(hCucina, 0, 4096, data) == BackendStatus::kOk);
	CHECK(ToStr(data) == "ciao da Cucina");

	CHECK(router.Close(hStudio) == BackendStatus::kOk);
	CHECK(router.Read(hStudio, 0, 16, data) == BackendStatus::kBadHandle); // closed
	CHECK(router.Read(999, 0, 16, data) == BackendStatus::kBadHandle);     // never opened
	CHECK(router.Close(hCucina) == BackendStatus::kOk);

	// Opening a directory is refused.
	uint64_t h = 0;
	CHECK(router.Open("/", h, size) == BackendStatus::kIsADirectory);
	CHECK(router.Open("/Studio", h, size) == BackendStatus::kIsADirectory);

	// Write is not supported through the discovery surface (inherited default).
	CHECK(router.Mkdir("/Studio/x", 0755) == BackendStatus::kUnsupported);

	// --- M4 distributed query: aggregation + the virtual .query folder ---
	{
		FakeSource qsrc;
		qsrc.studio.AddFile("report.txt", "S report");
		qsrc.studio.AddFile("notes.txt", "S notes");
		qsrc.cucina.AddFile("report.txt", "C report"); // same name, different peer
		qsrc.cucina.AddFile("recipe.txt", "C recipe");
		PathRouter qrouter(qsrc);

		// "report" matches one file on each reachable peer; Offline (no backend) is skipped. Dedup
		// is by (peer, path), so the same name on two peers is kept as two distinct hits.
		std::vector<AggregatedEntry> agg = AggregateQuery(qsrc, "report");
		CHECK(agg.size() == 2);
		bool haveStudio = false, haveCucina = false;
		for (const AggregatedEntry& a : agg) {
			if (a.peer == "Studio") haveStudio = true;
			if (a.peer == "Cucina") haveCucina = true;
		}
		CHECK(haveStudio && haveCucina);

		// The quota caps the total.
		CHECK(AggregateQuery(qsrc, "report", 1).size() == 1);

		// A peer that does not support queries is skipped, not an error.
		qsrc.cucina.unsupported = true;
		CHECK(AggregateQuery(qsrc, "report").size() == 1);
		qsrc.cucina.unsupported = false;

		// The router exposes ".query" beside the peers, as a synthetic directory.
		std::vector<wire::Entry> qroot;
		CHECK(qrouter.ReadDir("/", qroot) == BackendStatus::kOk);
		CHECK(HasEntry(qroot, ".query"));
		wire::Entry st;
		CHECK(qrouter.Stat("/.query", st) == BackendStatus::kOk);
		CHECK(S_ISDIR(st.stat.mode));
		CHECK(qrouter.Stat("/.query/report", st) == BackendStatus::kOk);
		CHECK(S_ISDIR(st.stat.mode));

		// Listing the predicate folder yields the aggregated hits with unique, peer-tagged leaves.
		std::vector<wire::Entry> hits;
		CHECK(qrouter.ReadDir("/.query/report", hits) == BackendStatus::kOk);
		CHECK(hits.size() == 2);
		CHECK(HasEntry(hits, "Studio - report.txt"));
		CHECK(HasEntry(hits, "Cucina - report.txt"));

		// A result leaf stats as a file; opening it in place is a documented follow-up.
		CHECK(qrouter.Stat("/.query/report/Studio - report.txt", st) == BackendStatus::kOk);
		CHECK(S_ISREG(st.stat.mode));
		uint64_t qh = 0, qsz = 0;
		CHECK(qrouter.Open("/.query/report/Studio - report.txt", qh, qsz)
			== BackendStatus::kUnsupported);
	}

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
