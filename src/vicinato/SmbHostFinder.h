// SmbHostFinder.h
//
// Finds Windows/SMB hosts on the LAN that mDNS cannot see. Windows announces file sharing over
// NetBIOS / WS-Discovery, not Bonjour, so it never appears in the radar; here we find it by
// probing TCP 445 across the local /24. A background worker rescans on a slow cadence and exposes
// a thread-safe list of hosts, which the Vicinato merges into the neighborhood as SMB services.
//
// Portable: POSIX sockets only, no BeAPI. The pure helpers (subnet derivation, host -> service,
// merge/dedup) are unit-tested off Haiku; the live scan needs a network.

#ifndef CAMPIELLO_VICINATO_SMBHOSTFINDER_H
#define CAMPIELLO_VICINATO_SMBHOSTFINDER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "NetworkDirectory.h"

namespace campiello {
namespace vicinato {

// Derive the /24 prefix from a local IPv4 ("192.168.2.100" -> "192.168.2"); "" if malformed.
std::string SubnetPrefix(const std::string& localIpv4);

// Scan `prefix`.1..254 for TCP `port` open, giving the whole sweep up to `budgetMs`. Returns the
// responding hosts (numeric IPv4). Non-blocking connects polled for writability.
std::vector<std::string> ScanSubnetPort(const std::string& prefix, uint16_t port, int budgetMs);

// Turn found SMB hosts into browsable SMB NetworkServices (password auth, SmbBackend).
std::vector<NetworkService> SmbHostsToServices(const std::vector<std::string>& hosts);

// Merge scanned SMB hosts into an existing service list, skipping any host already represented by
// a browsable SMB service (e.g. a Samba box that DID advertise _smb._tcp via mDNS).
std::vector<NetworkService> MergeSmbHosts(std::vector<NetworkService> base,
	const std::vector<std::string>& smbHosts);

// The background finder: rescans the local subnet(s) for SMB hosts on a timer.
class SmbHostFinder {
public:
	SmbHostFinder() = default;
	~SmbHostFinder();
	SmbHostFinder(const SmbHostFinder&) = delete;
	SmbHostFinder& operator=(const SmbHostFinder&) = delete;

	// Start the worker: rescan every `intervalSec`, probing TCP `port`.
	void Start(uint16_t port = 445, int intervalSec = 30);
	void Stop();
	bool IsRunning() const { return fThread.joinable(); }

	// A thread-safe snapshot of the hosts found so far.
	std::vector<std::string> Hosts() const;

private:
	void Run();

	uint16_t                 fPort = 445;
	int                      fIntervalSec = 30;
	std::thread              fThread;
	std::atomic<bool>        fStop{false};
	mutable std::mutex       fMutex;
	std::vector<std::string> fHosts;
};

} // namespace vicinato
} // namespace campiello

#endif // CAMPIELLO_VICINATO_SMBHOSTFINDER_H
