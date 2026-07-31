// NfsProbe.cpp
//
// See NfsProbe.h. Hand-rolled ONC RPC over UDP: portmapper GETPORT + MOUNT EXPORT.

#include "NfsProbe.h"

#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace campiello {
namespace nfs {

namespace {
const uint32_t kProgPortmap = 100000, kPortmapVers = 2, kPmapGetPort = 3;
const uint32_t kProgMount   = 100005, kMountVers   = 3, kMountExport = 5;
const uint32_t kMsgReply = 1, kMsgAccepted = 0, kAcceptSuccess = 0;
const int kPortmapPort = 111;
} // namespace

namespace rpc {

void PutU32(std::string& b, uint32_t v)
{
	b.push_back((char)(v >> 24)); b.push_back((char)(v >> 16));
	b.push_back((char)(v >> 8));  b.push_back((char)(v & 0xff));
}

void PutString(std::string& b, const std::string& s)
{
	PutU32(b, (uint32_t)s.size());
	b += s;
	while (b.size() % 4) b.push_back('\0'); // XDR pad to a 4-byte boundary
}

uint32_t GetU32(const std::string& b, size_t* off)
{
	if (*off + 4 > b.size()) { *off = b.size() + 1; return 0; }
	uint32_t v = ((uint8_t)b[*off] << 24) | ((uint8_t)b[*off + 1] << 16)
		| ((uint8_t)b[*off + 2] << 8) | (uint8_t)b[*off + 3];
	*off += 4;
	return v;
}

std::string GetString(const std::string& b, size_t* off)
{
	uint32_t len = GetU32(b, off);
	if (*off + len > b.size()) { *off = b.size() + 1; return ""; }
	std::string s = b.substr(*off, len);
	*off += len;
	while (*off % 4) ++*off; // skip XDR padding
	return s;
}

std::string BuildCall(uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc)
{
	std::string b;
	PutU32(b, xid);
	PutU32(b, 0);      // mtype = CALL
	PutU32(b, 2);      // rpcvers
	PutU32(b, prog);
	PutU32(b, vers);
	PutU32(b, proc);
	PutU32(b, 0); PutU32(b, 0); // cred: AUTH_NULL, length 0
	PutU32(b, 0); PutU32(b, 0); // verf: AUTH_NULL, length 0
	return b;
}

bool ParseReplyHeader(const std::string& b, size_t* off)
{
	*off = 0;
	GetU32(b, off);                       // xid
	if (GetU32(b, off) != kMsgReply) return false;
	if (GetU32(b, off) != kMsgAccepted) return false; // reply_stat
	GetU32(b, off);                       // verf flavor
	uint32_t vlen = GetU32(b, off);       // verf length
	*off += vlen;
	while (*off % 4) ++*off;
	if (GetU32(b, off) != kAcceptSuccess) return false; // accept_stat
	return *off <= b.size();
}

// exportnode list: repeated { value_follows(1), dirpath(string), groups-list }, terminated by
// value_follows(0). A groups-list is repeated { value_follows(1), name(string) } ended by 0.
std::vector<std::string> ParseExportList(const std::string& b, size_t off)
{
	std::vector<std::string> out;
	while (off <= b.size()) {
		if (GetU32(b, &off) == 0)  // value_follows
			break;
		std::string dir = GetString(b, &off);
		if (off > b.size())
			break;
		if (!dir.empty())
			out.push_back(dir);
		while (off <= b.size()) {   // skip the groups list
			if (GetU32(b, &off) == 0)
				break;
			GetString(b, &off);
		}
	}
	return out;
}

} // namespace rpc

bool NfsProbe::UdpRpc(int port, const std::string& request, std::string* reply)
{
	char portStr[8];
	std::snprintf(portStr, sizeof(portStr), "%d", port);
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo(fHost.c_str(), portStr, &hints, &res) != 0 || res == nullptr)
		return false;
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) { freeaddrinfo(res); return false; }

	struct timeval tv;
	tv.tv_sec = 3; tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	bool ok = false;
	if (sendto(fd, request.data(), request.size(), 0, res->ai_addr, res->ai_addrlen)
			== (ssize_t)request.size()) {
		char buf[8192];
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n > 0) { reply->assign(buf, n); ok = true; }
	}
	close(fd);
	freeaddrinfo(res);
	return ok;
}

int NfsProbe::MountPort()
{
	std::string call = rpc::BuildCall(0x43414d50, kProgPortmap, kPortmapVers, kPmapGetPort);
	rpc::PutU32(call, kProgMount);
	rpc::PutU32(call, kMountVers);
	rpc::PutU32(call, 17); // IPPROTO_UDP
	rpc::PutU32(call, 0);
	std::string reply;
	if (!UdpRpc(kPortmapPort, call, &reply))
		return 0;
	size_t off = 0;
	if (!rpc::ParseReplyHeader(reply, &off))
		return 0;
	return (int)rpc::GetU32(reply, &off);
}

std::vector<std::string> NfsProbe::ListExports(bool* okOut)
{
	*okOut = false;
	int port = MountPort();
	if (port <= 0)
		return {};
	std::string call = rpc::BuildCall(0x43414d51, kProgMount, kMountVers, kMountExport);
	std::string reply;
	if (!UdpRpc(port, call, &reply))
		return {};
	size_t off = 0;
	if (!rpc::ParseReplyHeader(reply, &off))
		return {};
	*okOut = true;
	return rpc::ParseExportList(reply, off);
}

} // namespace nfs
} // namespace campiello
