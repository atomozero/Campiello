// AfpProbe.cpp
//
// See AfpProbe.h. Hand-rolled DSI GetStatus over plain TCP.

#include "AfpProbe.h"

#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace afp {

namespace {
uint16_t BE16(const std::string& b, size_t off)
{
	if (off + 2 > b.size())
		return 0;
	return ((uint8_t)b[off] << 8) | (uint8_t)b[off + 1];
}

// A Pascal string (1-byte length + bytes) at `off` in the block.
std::string PascalAt(const std::string& b, size_t off)
{
	if (off >= b.size())
		return "";
	size_t len = (uint8_t)b[off];
	if (off + 1 + len > b.size())
		len = b.size() - off - 1;
	return b.substr(off + 1, len);
}

// Read a count-prefixed list of Pascal strings at `off` (1-byte count, then that many strings).
std::vector<std::string> PascalList(const std::string& b, size_t off)
{
	std::vector<std::string> out;
	if (off >= b.size())
		return out;
	size_t count = (uint8_t)b[off];
	size_t p = off + 1;
	for (size_t i = 0; i < count && p < b.size(); ++i) {
		size_t len = (uint8_t)b[p];
		out.push_back(b.substr(p + 1, (p + 1 + len <= b.size()) ? len : b.size() - p - 1));
		p += 1 + len;
	}
	return out;
}
} // namespace

// GetSrvrInfo block layout (offsets are relative to the block start):
//   0: MachineType offset (u16)   2: AFPVersionCount offset (u16)   4: UAMCount offset (u16)
//   6: VolumeIconAndMask offset   8: Flags (u16)   10: ServerName (Pascal string)
Status ParseGetSrvrInfo(const std::string& block)
{
	Status s;
	if (block.size() < 12)
		return s;
	uint16_t machineOff = BE16(block, 0);
	uint16_t verOff     = BE16(block, 2);
	uint16_t uamOff     = BE16(block, 4);

	s.serverName  = PascalAt(block, 10);
	s.machineType = PascalAt(block, machineOff);
	s.versions    = PascalList(block, verOff);
	s.uams        = PascalList(block, uamOff);
	return s;
}

std::string BuildGetStatusRequest()
{
	std::string r;
	r.push_back(0x00); // flags: request
	r.push_back(0x03); // command: DSIGetStatus
	r.push_back(0x00); r.push_back(0x01); // requestID = 1
	for (int i = 0; i < 4; ++i) r.push_back(0x00); // write offset / error code = 0
	for (int i = 0; i < 4; ++i) r.push_back(0x00); // totalDataLength = 0
	for (int i = 0; i < 4; ++i) r.push_back(0x00); // reserved
	return r; // 16 bytes
}

namespace {
int TcpConnect(const std::string& host, int port)
{
	char portStr[8];
	std::snprintf(portStr, sizeof(portStr), "%d", port);
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || res == nullptr)
		return -1;
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) { freeaddrinfo(res); return -1; }
	if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		close(fd); freeaddrinfo(res); return -1;
	}
	freeaddrinfo(res);
	return fd;
}

bool ReadExactly(int fd, char* buf, size_t n)
{
	size_t got = 0;
	while (got < n) {
		ssize_t r = read(fd, buf + got, n - got);
		if (r <= 0)
			return false;
		got += r;
	}
	return true;
}
} // namespace

Status AfpProbe::GetStatus(bool* okOut)
{
	*okOut = false;
	Status s;
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return s;

	std::string req = BuildGetStatusRequest();
	if (write(fd, req.data(), req.size()) != (ssize_t)req.size()) { close(fd); return s; }

	char hdr[16];
	if (!ReadExactly(fd, hdr, 16)) { close(fd); return s; }
	// totalDataLength is at bytes 8..11 (big-endian).
	uint32_t len = ((uint8_t)hdr[8] << 24) | ((uint8_t)hdr[9] << 16)
		| ((uint8_t)hdr[10] << 8) | (uint8_t)hdr[11];
	if (len == 0 || len > 65535) { close(fd); return s; }

	std::string block(len, '\0');
	if (!ReadExactly(fd, &block[0], len)) { close(fd); return s; }
	close(fd);

	*okOut = true;
	return ParseGetSrvrInfo(block);
}

} // namespace afp
} // namespace campiello
