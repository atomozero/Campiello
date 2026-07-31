// Listing.h
//
// CNP STAT and LIST message payloads, plus the shared Stat and Entry structures.
//
// STAT and LIST requests share the body { path }. A reply reuses the request's frame type
// and request_id (a failure would come back as an ERROR frame instead). STAT replies with
// a single Entry; LIST replies with { entries: [...] }.
//
// An Entry is { name, stat, attrs }: the leaf name, a Stat, and the full AttrSet so BFS
// attribute fidelity travels with every listing (docs/PROPOSAL.md section 10). Stat
// carries the fields relevant to Tracker browsing and BFS identity, from the verified
// struct stat (headers/posix/sys/stat.h): mode, size, modification time, creation time
// (st_crtim, the BFS-meaningful one), and inode. uid/gid are omitted (per-machine
// sharing, no user matrix, PROPOSAL.md section 2); sub-second times and atime are deferred.
//
// Pure standard C++ (no Haiku dependency), built on Attributes.h / Cbor.h / Frame.h.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_LISTING_H
#define CAMPIELLO_TRAGHETTO_WIRE_LISTING_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Attributes.h"
#include "Frame.h"

namespace campiello {
namespace wire {

// B_PATH_NAME_LENGTH = MAXPATHLEN = 1024, and a leaf name is at most
// B_FILE_NAME_LENGTH - 1 = 255 (verified against StorageDefs.h / limits.h).
static const size_t kMaxPathBytes = 1024;
static const size_t kMaxEntryNameBytes = 255;

// Cap on entries in a single LIST reply. Large directories will stream/paginate later
// (PROPOSAL.md section 14); for now one reply carries up to this many.
static const size_t kMaxEntriesPerReply = 65536;

struct Stat {
	uint32_t mode = 0;    // "m", st_mode (type bits + permissions)
	uint64_t size = 0;    // "sz", st_size
	int64_t  mtime = 0;   // "mt", modification time, seconds since epoch
	int64_t  crtime = 0;  // "ct", creation time (st_crtim), seconds since epoch
	uint64_t inode = 0;   // "ino", st_ino
};

struct Entry {
	std::string name;     // "name", leaf name
	Stat stat;            // "stat"
	AttrSet attrs;        // "attrs"
};

// Composable writers/readers (used inside larger structures; no end-of-stream check).
void WriteStat(CborWriter& w, const Stat& s);
bool ReadStat(CborReader& r, Stat& out);
void WriteEntry(CborWriter& w, const Entry& e);
bool ReadEntry(CborReader& r, Entry& out);

// Request body { path } shared by STAT and LIST.
std::vector<uint8_t> EncodePathRequest(const std::string& path);
bool DecodePathRequest(const std::vector<uint8_t>& payload, std::string& path);

// STAT reply body: a single Entry.
std::vector<uint8_t> EncodeStatReply(const Entry& entry);
bool DecodeStatReply(const std::vector<uint8_t>& payload, Entry& out);

// LIST reply body: { entries: [Entry...] }.
std::vector<uint8_t> EncodeListing(const std::vector<Entry>& entries);
bool DecodeListing(const std::vector<uint8_t>& payload, std::vector<Entry>& out);

// Frame builders. Replies echo the request's requestId.
Frame MakeStatRequest(const std::string& path, uint32_t requestId);
Frame MakeStatReply(const Entry& entry, uint32_t requestId);
Frame MakeListRequest(const std::string& path, uint32_t requestId);
Frame MakeListReply(const std::vector<Entry>& entries, uint32_t requestId);

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_LISTING_H
