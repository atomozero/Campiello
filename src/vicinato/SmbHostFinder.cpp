// SmbHostFinder.cpp
//
// See SmbHostFinder.h.

#include "SmbHostFinder.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../bricola/mdns/MdnsSocket.h"

namespace campiello {
namespace vicinato {

std::string SubnetPrefix(const std::string& localIpv4)
{
	// Keep everything up to the last dot: "192.168.2.100" -> "192.168.2".
	size_t dot = localIpv4.find_last_of('.');
	if (dot == std::string::npos || dot == 0)
		return "";
	// Reject an address with fewer than 3 dots (not a dotted quad).
	std::string head = localIpv4.substr(0, dot);
	if (std::count(head.begin(), head.end(), '.') != 2)
		return "";
	return head;
}

// Confirm a host actually speaks SMB (not just "port 445 is open"): connect, send an SMB negotiate
// that advertises both SMB1 and SMB2 dialects, and require a reply carrying the SMB magic. This
// filters out non-SMB services on 445 and half-open / spurious connects, so the neighborhood shows
// real Windows/Samba shares only, not every reachable address.
static bool VerifySmb(const std::string& ip, uint16_t port, int timeoutMs)
{
	// SMB_COM_NEGOTIATE over NetBIOS session service, dialects: NT LM 0.12, SMB 2.002, SMB 2.???.
	static const unsigned char kNegProt[] = {
		0x00, 0x00, 0x00, 0x45,                         // NetBIOS: length 0x45
		0xFF, 'S', 'M', 'B', 0x72,                      // SMB1 header: \xFFSMB, negotiate
		0x00, 0x00, 0x00, 0x00, 0x18, 0x01, 0x28, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // signature
		0x00, 0x00, 0x00, 0x00, 0x2F, 0x4B, 0x00, 0x00, 0x00, 0x00,
		0x00,                                           // word count
		0x22, 0x00,                                     // byte count = 34
		0x02, 'N','T',' ','L','M',' ','0','.','1','2', 0x00,
		0x02, 'S','M','B',' ','2','.','0','0','2', 0x00,
		0x02, 'S','M','B',' ','2','.','?','?','?', 0x00,
	};

	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return false;
	int fl = ::fcntl(fd, F_GETFL, 0);
	::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	sockaddr_in a;
	std::memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	::inet_aton(ip.c_str(), &a.sin_addr);

	bool smb = false;
	(void)::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)); // EINPROGRESS expected
	timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	fd_set wf;
	FD_ZERO(&wf);
	FD_SET(fd, &wf);
	if (::select(fd + 1, nullptr, &wf, nullptr, &tv) > 0) {
		int err = 0;
		socklen_t len = sizeof(err);
		::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
		if (err == 0 && ::send(fd, kNegProt, sizeof(kNegProt), 0) == (ssize_t)sizeof(kNegProt)) {
			fd_set rf;
			FD_ZERO(&rf);
			FD_SET(fd, &rf);
			tv.tv_sec = timeoutMs / 1000;
			tv.tv_usec = (timeoutMs % 1000) * 1000;
			if (::select(fd + 1, &rf, nullptr, nullptr, &tv) > 0) {
				unsigned char buf[8];
				ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
				// NetBIOS(4) then \xFF or \xFE then "SMB".
				smb = (n >= 8) && (buf[4] == 0xFF || buf[4] == 0xFE)
					&& buf[5] == 'S' && buf[6] == 'M' && buf[7] == 'B';
			}
		}
	}
	::close(fd);
	return smb;
}

std::vector<std::string> ScanSubnetPort(const std::string& prefix, uint16_t port, int budgetMs)
{
	std::vector<std::string> found;
	if (prefix.empty())
		return found;

	std::vector<int> fds;
	std::vector<std::string> ips;
	for (int h = 1; h <= 254; ++h) {
		char ip[32];
		std::snprintf(ip, sizeof(ip), "%s.%d", prefix.c_str(), h);
		int fd = ::socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0)
			continue;
		int fl = ::fcntl(fd, F_GETFL, 0);
		::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
		sockaddr_in a;
		std::memset(&a, 0, sizeof(a));
		a.sin_family = AF_INET;
		a.sin_port = htons(port);
		::inet_aton(ip, &a.sin_addr);
		(void)::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)); // EINPROGRESS expected
		fds.push_back(fd);
		ips.push_back(ip);
	}

	std::vector<bool> open(fds.size(), false);
	int elapsed = 0;
	const int step = 500;
	while (elapsed < budgetMs) {
		fd_set wf;
		FD_ZERO(&wf);
		int maxfd = 0;
		bool any = false;
		for (size_t i = 0; i < fds.size(); ++i) {
			if (fds[i] >= 0) {
				FD_SET(fds[i], &wf);
				if (fds[i] > maxfd)
					maxfd = fds[i];
				any = true;
			}
		}
		if (!any)
			break;
		timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = step * 1000;
		::select(maxfd + 1, nullptr, &wf, nullptr, &tv);
		for (size_t i = 0; i < fds.size(); ++i) {
			if (fds[i] < 0 || !FD_ISSET(fds[i], &wf))
				continue;
			int err = 0;
			socklen_t len = sizeof(err);
			::getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &len);
			if (err == 0)
				open[i] = true;
			::close(fds[i]);
			fds[i] = -1;
		}
		elapsed += step;
	}
	for (size_t i = 0; i < fds.size(); ++i)
		if (fds[i] >= 0)
			::close(fds[i]);

	// Only report candidates that actually answer an SMB negotiate (real shares), not every 445.
	for (size_t i = 0; i < ips.size(); ++i)
		if (open[i] && VerifySmb(ips[i], port, 600))
			found.push_back(ips[i]);
	return found;
}

std::vector<NetworkService> SmbHostsToServices(const std::vector<std::string>& hosts)
{
	std::vector<NetworkService> out;
	out.reserve(hosts.size());
	for (const std::string& host : hosts) {
		NetworkService s = ClassifyServiceType("_smb._tcp.local");
		s.id = "smbhost/" + host;
		s.host = host;
		s.port = 445;
		s.category = "File";
		s.label = "Condivisione Windows (" + host + ")";
		out.push_back(std::move(s));
	}
	return out;
}

std::vector<NetworkService> MergeSmbHosts(std::vector<NetworkService> base,
	const std::vector<std::string>& smbHosts)
{
	for (const std::string& host : smbHosts) {
		// Skip a 445 host that is already known by ANY service at that address (e.g. a Fire TV
		// that advertises media over mDNS but also has 445 open): the richer mDNS identity wins,
		// so the host is not mislabelled as a Windows share.
		bool covered = false;
		for (const NetworkService& s : base) {
			if (s.host == host) {
				covered = true;
				break;
			}
		}
		if (covered)
			continue;
		std::vector<NetworkService> one = SmbHostsToServices({host});
		base.push_back(std::move(one.front()));
	}
	return base;
}

SmbHostFinder::~SmbHostFinder()
{
	Stop();
}

std::vector<std::string> SmbHostFinder::Hosts() const
{
	std::lock_guard<std::mutex> lock(fMutex);
	return fHosts;
}

void SmbHostFinder::Start(uint16_t port, int intervalSec)
{
	if (IsRunning())
		return;
	fPort = port;
	fIntervalSec = intervalSec;
	fStop.store(false);
	fThread = std::thread(&SmbHostFinder::Run, this);
}

void SmbHostFinder::Stop()
{
	if (!IsRunning())
		return;
	fStop.store(true);
	fThread.join();
}

void SmbHostFinder::Run()
{
	using namespace std::chrono;
	int64_t lastScanSec = -1000000;
	while (!fStop.load()) {
		int64_t nowSec = duration_cast<seconds>(steady_clock::now().time_since_epoch()).count();
		if (nowSec - lastScanSec >= fIntervalSec) {
			// Scan each local /24 (usually one) and accumulate the hosts, excluding our own IPs.
			std::vector<std::string> locals =
				bricola::mdns::LocalIPv4Addresses(false);
			std::vector<std::string> hosts;
			for (const std::string& local : locals) {
				std::string prefix = SubnetPrefix(local);
				std::vector<std::string> found = ScanSubnetPort(prefix, fPort, 2000);
				for (const std::string& h : found)
					if (h != local && std::find(hosts.begin(), hosts.end(), h) == hosts.end())
						hosts.push_back(h);
			}
			{
				std::lock_guard<std::mutex> lock(fMutex);
				fHosts = std::move(hosts);
			}
			lastScanSec = nowSec;
		}
		// Sleep in short slices so Stop() is responsive.
		std::this_thread::sleep_for(milliseconds(200));
	}
}

} // namespace vicinato
} // namespace campiello
