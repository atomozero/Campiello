// MdnsSocket.cpp
//
// See MdnsSocket.h. The multicast socket setup mirrors the proven pattern in
// LANterna/src/enrich/MdnsEnricher.cpp, extended from a one-shot querier to a persistent
// listener by adding the group membership (IP_ADD_MEMBERSHIP).

#include "MdnsSocket.h"

#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <unistd.h>

#include <cstring>

namespace campiello {
namespace bricola {
namespace mdns {

const char* const kMdnsGroup = "224.0.0.251";

std::vector<std::string> LocalIPv4Addresses(bool includeLoopback)
{
	std::vector<std::string> out;
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return out;

	char buf[8192];
	struct ifconf ifc;
	std::memset(&ifc, 0, sizeof(ifc));
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
		close(fd);
		return out;
	}

	char* p = static_cast<char*>(ifc.ifc_buf);
	char* end = p + ifc.ifc_len;
	while (p < end) {
		struct ifreq* r = reinterpret_cast<struct ifreq*>(p);
		// Entries are variable-length (address may carry its own sa_len); advance past this one.
		size_t entrySize = IF_NAMESIZE + r->ifr_addr.sa_len;
		if (entrySize < sizeof(struct ifreq))
			entrySize = sizeof(struct ifreq);

		if (r->ifr_addr.sa_family == AF_INET) {
			// Read this interface's flags by name; keep only up interfaces. IFF_MULTICAST is
			// deliberately not required: Haiku leaves it clear even where multicast works.
			struct ifreq fr;
			std::memset(&fr, 0, sizeof(fr));
			std::memcpy(fr.ifr_name, r->ifr_name, IF_NAMESIZE);
			fr.ifr_name[IF_NAMESIZE - 1] = '\0';
			int flags = 0;
			if (ioctl(fd, SIOCGIFFLAGS, &fr) == 0)
				flags = fr.ifr_flags;
			bool isUp = (flags & IFF_UP) != 0;
			bool isLoop = (flags & IFF_LOOPBACK) != 0;
			if (isUp && (includeLoopback || !isLoop)) {
				struct sockaddr_in* si = reinterpret_cast<struct sockaddr_in*>(&r->ifr_addr);
				char ip[INET_ADDRSTRLEN];
				if (inet_ntop(AF_INET, &si->sin_addr, ip, sizeof(ip)) != nullptr)
					out.emplace_back(ip);
			}
		}
		p += entrySize;
	}
	close(fd);
	return out;
}

std::string PrimaryMulticastIPv4()
{
	std::vector<std::string> addrs = LocalIPv4Addresses(false);
	return addrs.empty() ? std::string() : addrs.front();
}

MdnsSocket::~MdnsSocket()
{
	Close();
}

bool MdnsSocket::Open(const char* interfaceIpv4)
{
	Close();

	fInterface = (interfaceIpv4 != nullptr) ? interfaceIpv4 : "";

	// Resolve the chosen interface address (INADDR_ANY when none given).
	struct in_addr ifAddr;
	ifAddr.s_addr = htonl(INADDR_ANY);
	if (!fInterface.empty() && inet_pton(AF_INET, fInterface.c_str(), &ifAddr) != 1) {
		fError = "bad interface address";
		return false;
	}

	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		fError = "socket() failed";
		return false;
	}

	// Allow several mDNS listeners (other apps, and our own test's two sockets) to share the
	// port. Both flags are needed: REUSEADDR for the address, REUSEPORT for the port itself.
	int one = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0
		|| setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0) {
		fError = "SO_REUSEADDR/SO_REUSEPORT failed";
		close(fd);
		return false;
	}

	// Bind to the mDNS port on all interfaces so we receive group traffic sent there.
	struct sockaddr_in local;
	std::memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(kMdnsPort);
	if (bind(fd, reinterpret_cast<struct sockaddr*>(&local), sizeof(local)) < 0) {
		fError = "bind(5353) failed";
		close(fd);
		return false;
	}

	// Select the interface for outgoing multicast. On Haiku there is no default route for
	// 224.0.0.0/4, so this is what makes multicast actually leave and return on the right link.
	if (!fInterface.empty())
		setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifAddr, sizeof(ifAddr));

	// Join the mDNS group on the chosen interface.
	struct ip_mreq mreq;
	std::memset(&mreq, 0, sizeof(mreq));
	if (inet_pton(AF_INET, kMdnsGroup, &mreq.imr_multiaddr) != 1) {
		fError = "inet_pton(group) failed";
		close(fd);
		return false;
	}
	mreq.imr_interface = ifAddr;   // INADDR_ANY when no interface was chosen
	if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
		fError = "IP_ADD_MEMBERSHIP failed";
		close(fd);
		return false;
	}

	// Keep our multicast on the local link, and loop it back so same-host peers hear us.
	unsigned char ttl = 1;
	setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	unsigned char loop = 1;
	setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

	fFd = fd;
	fError = nullptr;
	return true;
}

void MdnsSocket::Close()
{
	if (fFd >= 0) {
		// Best-effort leave; the kernel also drops membership on close.
		struct ip_mreq mreq;
		std::memset(&mreq, 0, sizeof(mreq));
		if (inet_pton(AF_INET, kMdnsGroup, &mreq.imr_multiaddr) == 1) {
			mreq.imr_interface.s_addr = htonl(INADDR_ANY);
			if (!fInterface.empty())
				inet_pton(AF_INET, fInterface.c_str(), &mreq.imr_interface);
			setsockopt(fFd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
		}
		close(fFd);
		fFd = -1;
	}
}

bool MdnsSocket::SendMulticast(const void* data, size_t len)
{
	return SendTo(kMdnsGroup, kMdnsPort, data, len);
}

bool MdnsSocket::SendTo(const char* ipv4, uint16_t port, const void* data, size_t len)
{
	if (fFd < 0) {
		fError = "socket not open";
		return false;
	}
	struct sockaddr_in dst;
	std::memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_port = htons(port);
	if (inet_pton(AF_INET, ipv4, &dst.sin_addr) != 1) {
		fError = "bad destination address";
		return false;
	}
	ssize_t n = sendto(fFd, data, len, 0,
		reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));
	if (n < 0 || static_cast<size_t>(n) != len) {
		fError = "sendto() failed";
		return false;
	}
	return true;
}

int MdnsSocket::Receive(void* buf, size_t bufLen, int timeoutMs, std::string& srcIp,
	uint16_t& srcPort)
{
	if (fFd < 0) {
		fError = "socket not open";
		return -1;
	}

	struct pollfd p;
	p.fd = fFd;
	p.events = POLLIN;
	p.revents = 0;
	int rc = poll(&p, 1, timeoutMs);
	if (rc == 0)
		return 0;   // timeout
	if (rc < 0) {
		if (errno == EINTR)
			return 0;
		fError = "poll() failed";
		return -1;
	}

	struct sockaddr_in src;
	std::memset(&src, 0, sizeof(src));
	socklen_t slen = sizeof(src);
	ssize_t n = recvfrom(fFd, buf, bufLen, 0,
		reinterpret_cast<struct sockaddr*>(&src), &slen);
	if (n < 0) {
		fError = "recvfrom() failed";
		return -1;
	}

	char ipBuf[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &src.sin_addr, ipBuf, sizeof(ipBuf)) != nullptr)
		srcIp.assign(ipBuf);
	else
		srcIp.clear();
	srcPort = ntohs(src.sin_port);
	return static_cast<int>(n);
}

} // namespace mdns
} // namespace bricola
} // namespace campiello
