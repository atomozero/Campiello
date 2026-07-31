// SftpBackend.cpp
//
// Implementation of the interop SFTP backend. See SftpBackend.h. Signatures verified against
// the installed libssh2 1.11.1 headers (libssh2.h, libssh2_sftp.h).

#include "SftpBackend.h"

#include <csignal>
#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

namespace campiello {
namespace fondamenta {

namespace {

// Connect a blocking TCP socket to host:port (host may be a name or a numeric address).
// Returns the fd, or -1 on failure.
int TcpConnect(const std::string& host, uint16_t port)
{
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	char portStr[16];
	std::snprintf(portStr, sizeof(portStr), "%u", (unsigned)port);

	struct addrinfo* res = nullptr;
	if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0)
		return -1;

	int fd = -1;
	for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

std::string LeafName(std::string path)
{
	while (path.size() > 1 && path.back() == '/')
		path.pop_back();
	if (path == "/" || path.empty())
		return ".";
	size_t slash = path.find_last_of('/');
	return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// Fill a wire::Stat from SFTP attributes. SFTP has no inode or creation time; inode is left 0
// and creation time mirrors the modification time (a sensible value for Tracker).
void FillWireStat(const LIBSSH2_SFTP_ATTRIBUTES& a, wire::Stat& s)
{
	s = wire::Stat{};
	if (a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
		s.mode = static_cast<uint32_t>(a.permissions);
	if (a.flags & LIBSSH2_SFTP_ATTR_SIZE)
		s.size = static_cast<uint64_t>(a.filesize);
	if (a.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
		s.mtime = static_cast<int64_t>(a.mtime);
		s.crtime = static_cast<int64_t>(a.mtime);
	}
	s.inode = 0;
}

} // namespace

SftpBackend::~SftpBackend()
{
	Disconnect();
}

std::string SftpBackend::RemotePath(const std::string& reqPath) const
{
	if (fBasePath.empty())
		return reqPath;
	if (reqPath == "/")
		return fBasePath;
	return fBasePath + reqPath; // fBasePath has no trailing slash; reqPath starts with '/'
}

BackendStatus SftpBackend::SftpError() const
{
	unsigned long e = libssh2_sftp_last_error(fSftp);
	switch (e) {
		case LIBSSH2_FX_NO_SUCH_FILE:
		case LIBSSH2_FX_NO_SUCH_PATH:       return BackendStatus::kNotFound;
		case LIBSSH2_FX_PERMISSION_DENIED:  return BackendStatus::kAccessDenied;
		case LIBSSH2_FX_NOT_A_DIRECTORY:    return BackendStatus::kNotADirectory;
		case LIBSSH2_FX_OP_UNSUPPORTED:     return BackendStatus::kUnsupported;
		default:                            return BackendStatus::kIoError;
	}
}

BackendStatus SftpBackend::Connect(const SftpConfig& config, SftpKnownHosts& knownHosts,
	const std::string& knownHostsPath, const HostKeyDecision& decision)
{
	if (IsConnected()) {
		fError = "already connected";
		return BackendStatus::kInvalidRequest;
	}
	// If the SSH connection drops (idle timeout, network loss), libssh2 may write to a broken
	// socket; the default SIGPIPE would terminate the whole process (in the add-on, that means
	// the userlandfs server dies and the mount becomes a dead zombie). Ignore it so a dropped
	// connection surfaces as an I/O error we can handle instead of killing the process.
	std::signal(SIGPIPE, SIG_IGN);

	libssh2_init(0); // idempotent; no matching exit (one backend per add-on process)

	fSocket = TcpConnect(config.host, config.port);
	if (fSocket < 0) {
		fError = "TCP connect failed";
		return BackendStatus::kTransportError;
	}

	fSession = libssh2_session_init();
	if (fSession == nullptr) {
		fError = "session init failed";
		Disconnect();
		return BackendStatus::kTransportError;
	}
	libssh2_session_set_blocking(fSession, 1);

	if (libssh2_session_handshake(fSession, fSocket) != 0) {
		fError = "SSH handshake failed";
		Disconnect();
		return BackendStatus::kTransportError;
	}

	// Host-key trust-on-first-use.
	size_t keyLen = 0;
	int keyType = 0;
	const char* keyData = libssh2_session_hostkey(fSession, &keyLen, &keyType);
	if (keyData == nullptr || keyLen == 0) {
		fError = "no host key";
		Disconnect();
		return BackendStatus::kTransportError;
	}
	std::vector<uint8_t> hostKey(keyData, keyData + keyLen);
	std::string host = config.host + ":" + std::to_string(config.port);
	HostKeyStatus hk = knownHosts.Evaluate(host, hostKey);
	if (hk != HostKeyStatus::kTrusted) {
		if (!decision || !decision(hk, hostKey)) {
			fError = "host key not accepted";
			Disconnect();
			return BackendStatus::kAccessDenied;
		}
		knownHosts.Pin(host, hostKey);
		knownHosts.SaveToFile(knownHostsPath); // best-effort persist
	}

	// Authentication: password when given, else a private key file.
	int authRc = -1;
	if (!config.password.empty()) {
		authRc = libssh2_userauth_password(fSession, config.user.c_str(),
			config.password.c_str());
	} else if (!config.privateKeyPath.empty()) {
		authRc = libssh2_userauth_publickey_fromfile(fSession, config.user.c_str(),
			config.publicKeyPath.empty() ? nullptr : config.publicKeyPath.c_str(),
			config.privateKeyPath.c_str(),
			config.keyPassphrase.empty() ? nullptr : config.keyPassphrase.c_str());
	} else {
		fError = "no authentication method configured";
		Disconnect();
		return BackendStatus::kInvalidRequest;
	}
	if (authRc != 0) {
		fError = "authentication failed";
		Disconnect();
		return BackendStatus::kAccessDenied;
	}

	fSftp = libssh2_sftp_init(fSession);
	if (fSftp == nullptr) {
		fError = "SFTP init failed";
		Disconnect();
		return BackendStatus::kTransportError;
	}

	fBasePath = config.basePath;
	fConfig = config;
	fKnownHostsPath = knownHostsPath;
	fError = nullptr;
	return BackendStatus::kOk;
}

bool SftpBackend::SessionDead() const
{
	if (fSession == nullptr)
		return true;
	switch (libssh2_session_last_errno(fSession)) {
		case LIBSSH2_ERROR_SOCKET_SEND:
		case LIBSSH2_ERROR_SOCKET_RECV:
		case LIBSSH2_ERROR_SOCKET_DISCONNECT:
		case LIBSSH2_ERROR_SOCKET_TIMEOUT:
		case LIBSSH2_ERROR_TIMEOUT:
			return true;
		default:
			return false;
	}
}

bool SftpBackend::Reconnect()
{
	Disconnect(); // drop the dead session and its (now invalid) handles

	// Reload the pinned host keys; the host is already trusted, so a reject decision is enough:
	// it is only consulted for an unknown or CHANGED key, both of which must abort a reconnect
	// (a key change on reconnect is a red flag, not something to auto-accept).
	SftpKnownHosts knownHosts;
	if (!fKnownHostsPath.empty())
		knownHosts.LoadFromFile(fKnownHostsPath);
	HostKeyDecision reject = [](HostKeyStatus, const std::vector<uint8_t>&) { return false; };

	return Connect(fConfig, knownHosts, fKnownHostsPath, reject) == BackendStatus::kOk;
}

bool SftpBackend::ReconnectIfDead()
{
	return SessionDead() && Reconnect();
}

void SftpBackend::Disconnect()
{
	for (auto& entry : fOpen)
		libssh2_sftp_close(entry.second);
	fOpen.clear();
	if (fSftp != nullptr) {
		libssh2_sftp_shutdown(fSftp);
		fSftp = nullptr;
	}
	if (fSession != nullptr) {
		libssh2_session_disconnect(fSession, "Campiello done");
		libssh2_session_free(fSession);
		fSession = nullptr;
	}
	if (fSocket >= 0) {
		close(fSocket);
		fSocket = -1;
	}
}

BackendStatus SftpBackend::Stat(const std::string& path, wire::Entry& out)
{
	std::string remote = RemotePath(path);
	for (int attempt = 0; attempt < 2; ++attempt) {
		if (!IsConnected() && !Reconnect())
			return BackendStatus::kTransportError;
		LIBSSH2_SFTP_ATTRIBUTES attrs;
		std::memset(&attrs, 0, sizeof(attrs));
		if (libssh2_sftp_stat(fSftp, remote.c_str(), &attrs) == 0) {
			out.name = LeafName(path);
			out.attrs.clear();
			FillWireStat(attrs, out.stat);
			return BackendStatus::kOk;
		}
		if (attempt == 0 && ReconnectIfDead())
			continue; // the session had died; reconnected, retry once
		return SftpError();
	}
	return BackendStatus::kIoError;
}

BackendStatus SftpBackend::ReadDir(const std::string& path, std::vector<wire::Entry>& out)
{
	std::string remote = RemotePath(path);
	for (int attempt = 0; attempt < 2; ++attempt) {
		if (!IsConnected() && !Reconnect())
			return BackendStatus::kTransportError;
		LIBSSH2_SFTP_HANDLE* dir = libssh2_sftp_opendir(fSftp, remote.c_str());
		if (dir == nullptr) {
			if (attempt == 0 && ReconnectIfDead())
				continue;
			return SftpError();
		}

		out.clear();
		bool readError = false;
		for (;;) {
			char name[512];
			LIBSSH2_SFTP_ATTRIBUTES attrs;
			std::memset(&attrs, 0, sizeof(attrs));
			int rc = libssh2_sftp_readdir(dir, name, sizeof(name), &attrs);
			if (rc == 0)
				break; // end of directory
			if (rc < 0) {
				readError = true;
				break;
			}
			std::string leaf(name, static_cast<size_t>(rc));
			if (leaf == "." || leaf == "..")
				continue;
			wire::Entry entry;
			entry.name = leaf;
			FillWireStat(attrs, entry.stat);
			out.push_back(std::move(entry));
		}
		libssh2_sftp_closedir(dir);
		if (!readError)
			return BackendStatus::kOk;
		if (attempt == 0 && ReconnectIfDead())
			continue;
		return SftpError();
	}
	return BackendStatus::kIoError;
}

BackendStatus SftpBackend::Open(const std::string& path, uint64_t& handle, uint64_t& size)
{
	std::string remote = RemotePath(path);
	for (int attempt = 0; attempt < 2; ++attempt) {
		if (!IsConnected() && !Reconnect())
			return BackendStatus::kTransportError;
		LIBSSH2_SFTP_HANDLE* h = libssh2_sftp_open(fSftp, remote.c_str(), LIBSSH2_FXF_READ, 0);
		if (h == nullptr) {
			if (attempt == 0 && ReconnectIfDead())
				continue;
			return SftpError();
		}

		size = 0;
		LIBSSH2_SFTP_ATTRIBUTES attrs;
		std::memset(&attrs, 0, sizeof(attrs));
		if (libssh2_sftp_fstat(h, &attrs) == 0 && (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE))
			size = static_cast<uint64_t>(attrs.filesize);

		handle = fNextHandle++;
		fOpen[handle] = h;
		return BackendStatus::kOk;
	}
	return BackendStatus::kIoError;
}

BackendStatus SftpBackend::Read(uint64_t handle, uint64_t offset, uint32_t length,
	std::vector<uint8_t>& out)
{
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return BackendStatus::kBadHandle;
	LIBSSH2_SFTP_HANDLE* h = it->second;

	libssh2_sftp_seek64(h, offset);
	out.assign(length, 0);
	size_t got = 0;
	// libssh2_sftp_read may return a short count without EOF, so loop until the request is
	// satisfied or the server reports end-of-file (0). FUSE treats a short read as EOF.
	while (got < length) {
		ssize_t n = libssh2_sftp_read(h, reinterpret_cast<char*>(out.data()) + got,
			length - got);
		if (n < 0) {
			out.clear();
			return BackendStatus::kIoError;
		}
		if (n == 0)
			break; // EOF
		got += static_cast<size_t>(n);
	}
	out.resize(got);
	return BackendStatus::kOk;
}

BackendStatus SftpBackend::Close(uint64_t handle)
{
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return BackendStatus::kBadHandle;
	libssh2_sftp_close(it->second);
	fOpen.erase(it);
	return BackendStatus::kOk;
}

} // namespace fondamenta
} // namespace campiello
