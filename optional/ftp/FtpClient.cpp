// FtpClient.cpp
//
// See FtpClient.h. Hand-rolled FTP (RFC 959) over plain sockets.

#include "FtpClient.h"

#include <Catalog.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

// Haiku Locale Kit: fError carries user-facing text (shown via LastError() in the status bar), so it
// goes through B_TRANSLATE too, sharing the FTP context/catalog with campiello_ftp.cpp.
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FTP"

namespace campiello {
namespace ftp {

std::vector<Entry> ParseListing(const std::string& raw)
{
	std::vector<Entry> out;
	size_t pos = 0;
	while (pos < raw.size()) {
		size_t nl = raw.find('\n', pos);
		std::string line = raw.substr(pos, (nl == std::string::npos ? raw.size() : nl) - pos);
		pos = (nl == std::string::npos) ? raw.size() : nl + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		if (line.empty())
			continue;

		// Unix "ls -l": perms links owner group size month day time/year name...
		char type = line[0];
		if (type != 'd' && type != '-' && type != 'l' && type != 'b' && type != 'c'
			&& type != 'p' && type != 's')
			continue; // not a recognized long-format line (skip "total N", MLSD, etc.)

		// find the start of the 9th field (the name), skipping 8 whitespace-delimited fields
		size_t i = 0;
		int field = 0;
		long long size = 0;
		size_t sizeStart = std::string::npos;
		while (i < line.size() && field < 8) {
			while (i < line.size() && line[i] == ' ') ++i;
			size_t tokStart = i;
			while (i < line.size() && line[i] != ' ') ++i;
			if (field == 4) sizeStart = tokStart;
			++field;
		}
		while (i < line.size() && line[i] == ' ') ++i;
		if (i >= line.size())
			continue;
		if (sizeStart != std::string::npos)
			size = std::atoll(line.c_str() + sizeStart);

		std::string name = line.substr(i);
		if (type == 'l') { // "name -> target": keep the link name
			size_t arrow = name.find(" -> ");
			if (arrow != std::string::npos)
				name = name.substr(0, arrow);
		}
		if (name == "." || name == "..")
			continue;

		Entry e;
		e.name = name;
		e.isDir = (type == 'd');
		e.size = size;
		out.push_back(e);
	}
	return out;
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
} // namespace

// Read one FTP reply from the control channel. Handles multi-line replies ("NNN-...\r\n...NNN ...").
int FtpClient::ReadReply(std::string* text)
{
	std::string code;
	for (;;) {
		// find a complete final line in the buffer
		size_t start = 0;
		while (true) {
			size_t nl = fInBuf.find('\n', start);
			if (nl == std::string::npos)
				break;
			std::string line = fInBuf.substr(start, nl - start);
			// a final line is "NNN " (digit digit digit space)
			if (line.size() >= 4 && isdigit(line[0]) && isdigit(line[1]) && isdigit(line[2])
				&& line[3] == ' ') {
				if (text != nullptr) *text = fInBuf.substr(0, nl);
				int c = std::atoi(line.substr(0, 3).c_str());
				fInBuf.erase(0, nl + 1);
				return c;
			}
			start = nl + 1;
		}
		char buf[2048];
		ssize_t n = read(fControl, buf, sizeof(buf));
		if (n <= 0)
			return -1;
		fInBuf.append(buf, n);
	}
}

int FtpClient::SendCmd(const std::string& cmd, std::string* reply)
{
	std::string line = cmd + "\r\n";
	if (write(fControl, line.data(), line.size()) < 0)
		return -1;
	return ReadReply(reply);
}

bool FtpClient::Connect()
{
	fControl = TcpConnect(fHost, fPort);
	if (fControl < 0) { fError = B_TRANSLATE("Connessione non riuscita."); return false; }
	if (ReadReply(nullptr) != 220) { fError = B_TRANSLATE("Nessun saluto dal server."); return false; }

	std::string reply;
	int c = SendCmd("USER " + fUser, &reply);
	if (c == 331)
		c = SendCmd("PASS " + fPass, &reply);
	if (c != 230) { fError = B_TRANSLATE("Accesso negato."); return false; }

	SendCmd("TYPE I", &reply); // binary
	return true;
}

void FtpClient::Disconnect()
{
	if (fControl >= 0) {
		write(fControl, "QUIT\r\n", 6);
		close(fControl);
		fControl = -1;
	}
}

// Parse a 227 reply "Entering Passive Mode (h1,h2,h3,h4,p1,p2)" and connect the data socket.
int FtpClient::OpenPasv()
{
	std::string reply;
	if (SendCmd("PASV", &reply) != 227)
		return -1;
	size_t open = reply.find('(');
	size_t close = reply.find(')', open);
	if (open == std::string::npos || close == std::string::npos)
		return -1;
	int h[6] = {0, 0, 0, 0, 0, 0};
	if (std::sscanf(reply.c_str() + open + 1, "%d,%d,%d,%d,%d,%d",
			&h[0], &h[1], &h[2], &h[3], &h[4], &h[5]) != 6)
		return -1;
	char dataHost[32];
	std::snprintf(dataHost, sizeof(dataHost), "%d.%d.%d.%d", h[0], h[1], h[2], h[3]);
	int dataPort = h[4] * 256 + h[5];
	return TcpConnect(dataHost, dataPort);
}

std::vector<Entry> FtpClient::List(const std::string& path, bool* okOut)
{
	*okOut = false;
	if (fControl < 0)
		return {};
	std::string reply;
	if (!path.empty() && SendCmd("CWD " + path, &reply) / 100 != 2) {
		fError = B_TRANSLATE("Cartella non accessibile.");
		return {};
	}
	int data = OpenPasv();
	if (data < 0) { fError = B_TRANSLATE("Modalita' passiva non riuscita."); return {}; }

	int c = SendCmd("LIST", &reply);
	if (c != 150 && c != 125) { close(data); fError = B_TRANSLATE("LIST rifiutato."); return {}; }

	std::string raw;
	char buf[4096];
	ssize_t n;
	while ((n = read(data, buf, sizeof(buf))) > 0)
		raw.append(buf, n);
	close(data);
	ReadReply(&reply); // 226 transfer complete

	*okOut = true;
	return ParseListing(raw);
}

bool FtpClient::Retrieve(const std::string& remoteFile, const std::string& localPath)
{
	if (fControl < 0)
		return false;
	int data = OpenPasv();
	if (data < 0) { fError = B_TRANSLATE("Modalita' passiva non riuscita."); return false; }

	std::string reply;
	int c = SendCmd("RETR " + remoteFile, &reply);
	if (c != 150 && c != 125) { close(data); fError = B_TRANSLATE("Download rifiutato."); return false; }

	FILE* f = std::fopen(localPath.c_str(), "wb");
	if (f == nullptr) { close(data); fError = B_TRANSLATE("Impossibile scrivere il file locale."); return false; }
	char buf[8192];
	ssize_t n;
	while ((n = read(data, buf, sizeof(buf))) > 0)
		std::fwrite(buf, 1, n, f);
	std::fclose(f);
	close(data);
	ReadReply(&reply); // 226
	return true;
}

} // namespace ftp
} // namespace campiello
