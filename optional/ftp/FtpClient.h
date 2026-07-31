// FtpClient.h
//
// Minimal FTP client (RFC 959) for a file server discovered via _ftp._tcp. Enough to browse and
// download: a control connection on port 21 (greeting, USER/PASS login, TYPE I), passive-mode data
// connections (PASV), directory listing (LIST) and file download (RETR).
//
// Pure sockets, encoded by hand: no third-party dependency (MIT-clean), links only libbe + the
// network kit. Plain FTP only (no TLS); FTPS is a documented follow-up.
//
// Reference: RFC 959 (File Transfer Protocol) for the command/reply grammar, PASV, and the transfer
// commands; the Unix "ls -l" LIST format for parsing directory entries.

#ifndef CAMPIELLO_FTP_FTPCLIENT_H
#define CAMPIELLO_FTP_FTPCLIENT_H

#include <string>
#include <vector>

namespace campiello {
namespace ftp {

struct Entry {
	std::string name;
	bool        isDir = false;
	long long   size = 0;
};

class FtpClient {
public:
	FtpClient(const std::string& host, int port = 21,
		const std::string& user = "anonymous", const std::string& pass = "anonymous@")
		: fHost(host), fPort(port), fUser(user), fPass(pass) {}
	~FtpClient() { Disconnect(); }

	// Connect the control channel and log in. Returns true on success (LastError() explains failures).
	bool Connect();
	void Disconnect();

	// List a directory (empty path = current). `okOut` reports success.
	std::vector<Entry> List(const std::string& path, bool* okOut);

	// Download a remote file to a local path. Returns true on success.
	bool Retrieve(const std::string& remoteFile, const std::string& localPath);

	const std::string& LastError() const { return fError; }

private:
	int SendCmd(const std::string& cmd, std::string* reply); // returns the 3-digit reply code
	int ReadReply(std::string* text);
	int OpenPasv();                                          // returns a connected data socket, or -1

	std::string fHost;
	int fPort;
	std::string fUser, fPass, fError, fInBuf;
	int fControl = -1;
};

// Parse a Unix "ls -l" style LIST response into entries (exposed for testing).
std::vector<Entry> ParseListing(const std::string& raw);

} // namespace ftp
} // namespace campiello

#endif // CAMPIELLO_FTP_FTPCLIENT_H
