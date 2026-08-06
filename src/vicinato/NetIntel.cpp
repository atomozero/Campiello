// NetIntel.cpp - see NetIntel.h.
//
// Ported from LANterna (MIT): src/net/ArpCache.cpp, src/net/WakeOnLan.cpp, src/enrich/OuiDatabase.cpp,
// src/enrich/NetBiosEnricher.cpp, src/enrich/SsdpEnricher.cpp. Reworked into one module, with the
// pure encode/parse steps split out for unit testing.

#include "NetIntel.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef __HAIKU__
#include <FindDirectory.h> // the C find_directory(), in libroot (no BeAPI dependency)
#endif

namespace campiello {
namespace vicinato {

namespace {

std::string Trim(const std::string& s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
	return s.substr(a, b - a);
}

std::string Lower(std::string s)
{
	for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

} // namespace

// ============================================================================================
// 1. ARP cache
// ============================================================================================

std::map<std::string, std::string> ReadArpCache()
{
	std::map<std::string, std::string> result;
#if defined(__HAIKU__)
	// `arp -a` on Haiku prints "IP  MAC  state" rows, whitespace separated.
	std::FILE* f = popen("arp -a", "r");
	if (f == nullptr)
		return result;
	char line[256];
	char ip[64], mac[64], state[32];
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		if (std::sscanf(line, "%63s %63s %31s", ip, mac, state) < 2)
			continue;
		if (std::strcmp(mac, "00:00:00:00:00:00") == 0)
			continue;
		if (std::strcmp(ip, "0.0.0.0") == 0)
			continue;
		// Keep only rows whose second field parses as a MAC (skip banners/headers).
		if (std::strchr(mac, ':') == nullptr)
			continue;
		result[ip] = Lower(mac);
	}
	pclose(f);
#elif defined(__linux__)
	std::FILE* f = std::fopen("/proc/net/arp", "r");
	if (f == nullptr)
		return result;
	char line[256];
	if (std::fgets(line, sizeof(line), f) == nullptr) { std::fclose(f); return result; }
	char ip[64], hwType[16], flags[16], mac[64], mask[64], dev[64];
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		if (std::sscanf(line, "%63s %15s %15s %63s %63s %63s",
				ip, hwType, flags, mac, mask, dev) != 6)
			continue;
		if (std::strcmp(flags, "0x0") == 0)
			continue;
		if (std::strcmp(mac, "00:00:00:00:00:00") == 0)
			continue;
		result[ip] = Lower(mac);
	}
	std::fclose(f);
#endif
	return result;
}

// ============================================================================================
// 2. OUI database
// ============================================================================================

std::string OuiDatabase::OuiKey(const std::string& mac)
{
	std::string hex;
	for (char c : mac) {
		if (std::isxdigit(static_cast<unsigned char>(c)))
			hex += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		if (hex.size() == 6)
			break;
	}
	return hex.size() == 6 ? hex : std::string();
}

size_t OuiDatabase::LoadFromFile(const std::string& path)
{
	fByPrefix.clear();
	std::FILE* f = std::fopen(path.c_str(), "r");
	if (f == nullptr)
		return 0;
	char buf[512];
	while (std::fgets(buf, sizeof(buf), f) != nullptr) {
		std::string line(buf);
		size_t hexPos = line.find("(hex)");
		if (hexPos == std::string::npos)
			continue;
		std::string prefixPart = line.substr(0, hexPos);
		std::string vendorPart = Trim(line.substr(hexPos + 5));
		if (vendorPart.empty())
			continue;
		std::string key = OuiKey(prefixPart);
		if (key.empty())
			continue;
		fByPrefix[key] = vendorPart;
	}
	std::fclose(f);
	return fByPrefix.size();
}

std::string OuiDatabase::Lookup(const std::string& mac) const
{
	std::string key = OuiKey(mac);
	if (key.empty())
		return std::string();
	auto it = fByPrefix.find(key);
	return it == fByPrefix.end() ? std::string() : it->second;
}

std::string FindOuiFile()
{
#ifdef __HAIKU__
	const directory_which dirs[] = {
		B_USER_SETTINGS_DIRECTORY, B_USER_DATA_DIRECTORY, B_SYSTEM_DATA_DIRECTORY};
	for (directory_which which : dirs) {
		char base[1024];
		if (find_directory(which, -1, false, base, sizeof(base)) != B_OK)
			continue;
		std::string path = std::string(base) + "/Campiello/oui.txt";
		if (access(path.c_str(), R_OK) == 0)
			return path;
	}
#endif
	return std::string();
}

// ============================================================================================
// 3. Wake-on-LAN
// ============================================================================================

namespace {

bool ParseMacBytes(const std::string& s, uint8_t out[6])
{
	int byte = 0, nibble = 0;
	uint8_t cur = 0;
	for (char c : s) {
		if (c == ':' || c == '-' || c == ' ') continue;
		int v;
		if (c >= '0' && c <= '9') v = c - '0';
		else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
		else return false;
		cur = static_cast<uint8_t>((cur << 4) | v);
		if (++nibble == 2) {
			if (byte >= 6) return false;
			out[byte++] = cur;
			cur = 0;
			nibble = 0;
		}
	}
	return byte == 6 && nibble == 0;
}

} // namespace

bool BuildWolPacket(const std::string& mac, uint8_t out[102])
{
	uint8_t m[6];
	if (!ParseMacBytes(mac, m))
		return false;
	for (int i = 0; i < 6; ++i) out[i] = 0xFF;
	for (int rep = 0; rep < 16; ++rep)
		std::memcpy(&out[6 + rep * 6], m, 6);
	return true;
}

std::string DirectedBroadcast(const std::string& ip)
{
	struct in_addr a;
	if (inet_pton(AF_INET, ip.c_str(), &a) != 1)
		return std::string();
	uint32_t host = ntohl(a.s_addr);
	uint32_t bcast = (host & 0xFFFFFF00u) | 0x000000FFu; // assume /24
	struct in_addr b;
	b.s_addr = htonl(bcast);
	char out[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &b, out, sizeof(out)) == nullptr)
		return std::string();
	return out;
}

bool SendWakeOnLan(const std::string& mac, const std::string& broadcastIp, int port)
{
	uint8_t packet[102];
	if (!BuildWolPacket(mac, packet))
		return false;

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return false;
	int one = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) < 0) {
		close(sock);
		return false;
	}
	struct sockaddr_in dst {};
	dst.sin_family = AF_INET;
	dst.sin_port = htons(static_cast<uint16_t>(port));
	if (inet_pton(AF_INET, broadcastIp.c_str(), &dst.sin_addr) != 1) {
		close(sock);
		return false;
	}
	ssize_t sent = sendto(sock, packet, sizeof(packet), 0,
		reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));
	close(sock);
	return sent == static_cast<ssize_t>(sizeof(packet));
}

// ============================================================================================
// 4. NetBIOS node status (UDP 137)
// ============================================================================================

namespace {

// RFC 1001/1002: the "*" wildcard name, half-ASCII encoded (each byte -> two chars).
void EncodeWildcardName(std::string& out)
{
	out += static_cast<char>(0x20);
	static const uint8_t name[16] = {'*', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	for (int i = 0; i < 16; ++i) {
		out += static_cast<char>(((name[i] >> 4) & 0x0F) + 'A');
		out += static_cast<char>((name[i] & 0x0F) + 'A');
	}
	out += '\0';
}

} // namespace

std::string BuildNbstatQuery()
{
	std::string pkt;
	pkt += '\0'; pkt += '\0';   // TRN_ID
	pkt += '\0'; pkt += '\x10'; // flags: query, broadcast
	pkt += '\0'; pkt += '\x01'; // QDCOUNT = 1
	pkt += '\0'; pkt += '\0';   // ANCOUNT
	pkt += '\0'; pkt += '\0';   // NSCOUNT
	pkt += '\0'; pkt += '\0';   // ARCOUNT
	EncodeWildcardName(pkt);
	pkt += '\0'; pkt += '\x21'; // QTYPE = NBSTAT (33)
	pkt += '\0'; pkt += '\x01'; // QCLASS = IN
	return pkt;
}

bool ParseNbstatResponse(const uint8_t* buf, size_t len, std::string& name, std::string& workgroup)
{
	if (len < 12) return false;
	uint16_t an = static_cast<uint16_t>((buf[6] << 8) | buf[7]);
	if (an == 0) return false;

	size_t offset = 12;
	while (offset < len) { // skip the answer name (explicit labels or a compression pointer)
		uint8_t b = buf[offset];
		if (b == 0) { offset++; break; }
		if ((b & 0xC0) == 0xC0) { offset += 2; break; }
		offset += b + 1;
	}
	if (offset + 10 > len) return false;
	offset += 10; // type(2) + class(2) + TTL(4) + RDLENGTH(2)
	if (offset >= len) return false;
	uint8_t numNames = buf[offset++];

	for (int i = 0; i < numNames && offset + 18 <= len; ++i) {
		std::string nm;
		for (int k = 0; k < 15; ++k) {
			char c = static_cast<char>(buf[offset + k]);
			if (c == ' ' || c == '\0') break;
			nm += c;
		}
		uint8_t type = buf[offset + 15];
		uint16_t flags = static_cast<uint16_t>((buf[offset + 16] << 8) | buf[offset + 17]);
		bool isGroup = (flags & 0x8000) != 0;
		offset += 18;
		if (nm.empty()) continue;
		if (!isGroup && name.empty() && (type == 0x00 || type == 0x20))
			name = nm;
		if (isGroup && workgroup.empty())
			workgroup = nm;
	}
	return !name.empty();
}

bool QueryNetBiosName(const std::string& ip, int timeoutMs, std::string& name,
	std::string& workgroup)
{
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return false;
	int fl = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, fl | O_NONBLOCK);

	struct sockaddr_in dst {};
	dst.sin_family = AF_INET;
	dst.sin_port = htons(137);
	if (inet_pton(AF_INET, ip.c_str(), &dst.sin_addr) != 1) {
		close(sock);
		return false;
	}
	std::string q = BuildNbstatQuery();
	sendto(sock, q.data(), q.size(), 0, reinterpret_cast<struct sockaddr*>(&dst), sizeof(dst));

	struct pollfd p {};
	p.fd = sock;
	p.events = POLLIN;
	if (poll(&p, 1, timeoutMs) <= 0) {
		close(sock);
		return false;
	}
	uint8_t buf[1024];
	ssize_t n = recv(sock, buf, sizeof(buf), 0);
	close(sock);
	if (n <= 0)
		return false;
	return ParseNbstatResponse(buf, static_cast<size_t>(n), name, workgroup);
}

bool ResolveReverseDns(const std::string& ip, std::string& hostname)
{
	struct sockaddr_in sa {};
	sa.sin_family = AF_INET;
	if (inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1)
		return false;

	// NI_MAXHOST is gated behind _DEFAULT_SOURCE, which -std=c++17 does not set; use its value.
	char host[1025];
	// NI_NAMEREQD makes getnameinfo fail (rather than return the numeric IP) when there is no PTR
	// record, so a plain no-name host does not masquerade as having a hostname.
	int rc = getnameinfo(reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa),
		host, sizeof(host), nullptr, 0, NI_NAMEREQD);
	if (rc != 0)
		return false;

	hostname = host;
	if (!hostname.empty() && hostname.back() == '.')
		hostname.pop_back();
	// A resolver that echoed the address back (some do despite NI_NAMEREQD) is not a real name.
	if (hostname == ip)
		return false;
	return !hostname.empty();
}

// ============================================================================================
// 5. SSDP / UPnP
// ============================================================================================

std::string SsdpHeaderValue(const std::string& msg, const char* name)
{
	std::string lower = Lower(msg);
	std::string key = Lower(name);
	key += ":";
	size_t pos = lower.find(key);
	if (pos == std::string::npos)
		return {};
	pos += key.size();
	while (pos < msg.size() && (msg[pos] == ' ' || msg[pos] == '\t'))
		pos++;
	size_t end = msg.find('\r', pos);
	if (end == std::string::npos) end = msg.find('\n', pos);
	if (end == std::string::npos) end = msg.size();
	return msg.substr(pos, end - pos);
}

std::string InferSsdpType(const SsdpDevice& d)
{
	std::string srv = Lower(d.server);
	std::string st = Lower(d.deviceType);

	if (srv.find("sonos") != std::string::npos)
		return "Sonos";
	if (srv.find("dlna") != std::string::npos || st.find("mediaserver") != std::string::npos)
		return "Server multimediale DLNA";
	if (st.find("mediarenderer") != std::string::npos)
		return "Lettore multimediale DLNA";
	if (st.find("internetgatewaydevice") != std::string::npos || srv.find("router") != std::string::npos)
		return "Router (UPnP IGD)";
	if (srv.find("samsung") != std::string::npos && srv.find("tv") != std::string::npos)
		return "Smart TV Samsung";
	if (srv.find("webos") != std::string::npos || (srv.find("lg") != std::string::npos && st.find("tv") != std::string::npos))
		return "Smart TV LG";
	if (st.find("printer") != std::string::npos)
		return "Stampante UPnP";
	if (srv.find("synology") != std::string::npos)
		return "NAS Synology";
	if (srv.find("qnap") != std::string::npos)
		return "NAS QNAP";
	if (!d.server.empty())
		return "UPnP: " + d.server;
	return "Dispositivo UPnP";
}

std::map<std::string, SsdpDevice> DiscoverSsdp(int timeoutMs)
{
	std::map<std::string, SsdpDevice> byIp;

	static const char* kQuery =
		"M-SEARCH * HTTP/1.1\r\n"
		"HOST: 239.255.255.250:1900\r\n"
		"MAN: \"ssdp:discover\"\r\n"
		"MX: 2\r\n"
		"ST: ssdp:all\r\n"
		"\r\n";

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return byIp;
	int fl = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, fl | O_NONBLOCK);
	int one = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	struct sockaddr_in local {};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = 0;
	(void)bind(sock, reinterpret_cast<struct sockaddr*>(&local), sizeof(local));

	unsigned char ttl = 2;
	setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

	struct sockaddr_in ssdp {};
	ssdp.sin_family = AF_INET;
	ssdp.sin_addr.s_addr = inet_addr("239.255.255.250");
	ssdp.sin_port = htons(1900);
	sendto(sock, kQuery, std::strlen(kQuery), 0,
		reinterpret_cast<struct sockaddr*>(&ssdp), sizeof(ssdp));

	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while (std::chrono::steady_clock::now() < deadline) {
		int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now()).count());
		if (remaining <= 0)
			break;
		struct pollfd p {};
		p.fd = sock;
		p.events = POLLIN;
		if (poll(&p, 1, remaining) <= 0)
			continue;

		char buf[4096];
		struct sockaddr_in src {};
		socklen_t slen = sizeof(src);
		ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
			reinterpret_cast<struct sockaddr*>(&src), &slen);
		if (n <= 0)
			continue;
		buf[n] = '\0';
		if (std::strncmp(buf, "HTTP/1.1 200", 12) != 0 && std::strncmp(buf, "HTTP/1.0 200", 12) != 0)
			continue;

		char ipBuf[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &src.sin_addr, ipBuf, sizeof(ipBuf));
		std::string msg(buf, static_cast<size_t>(n));
		SsdpDevice& dev = byIp[ipBuf];
		if (dev.server.empty()) dev.server = SsdpHeaderValue(msg, "SERVER");
		if (dev.deviceType.empty()) dev.deviceType = SsdpHeaderValue(msg, "ST");
		if (dev.location.empty()) dev.location = SsdpHeaderValue(msg, "LOCATION");
	}
	close(sock);
	return byIp;
}

} // namespace vicinato
} // namespace campiello
