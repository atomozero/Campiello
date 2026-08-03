// NutClient.cpp
//
// See NutClient.h. Hand-rolled read-only NUT client and line parsers.

#include "NutClient.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace nut {

std::string Unescape(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); ++i) {
		if (in[i] == '\\' && i + 1 < in.size()) {
			out += in[i + 1];
			++i;
		} else {
			out += in[i];
		}
	}
	return out;
}

namespace {

// Split into lines, stripping a trailing CR.
std::vector<std::string> Lines(const std::string& text)
{
	std::vector<std::string> out;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		out.push_back(line);
	}
	return out;
}

// Split a line into whitespace-separated tokens, but keep a trailing double-quoted string as one
// token (with its quotes) so the caller can unescape it. Good enough for NUT VAR/UPS lines.
std::vector<std::string> Tokens(const std::string& line)
{
	std::vector<std::string> out;
	size_t i = 0;
	while (i < line.size()) {
		while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
		if (i >= line.size())
			break;
		if (line[i] == '"') {
			size_t j = i + 1;
			std::string val;
			while (j < line.size()) {
				if (line[j] == '\\' && j + 1 < line.size()) { val += line[j]; val += line[j + 1]; j += 2; continue; }
				if (line[j] == '"') break;
				val += line[j++];
			}
			out.push_back(Unescape(val));
			i = (j < line.size()) ? j + 1 : j;
		} else {
			size_t j = i;
			while (j < line.size() && !std::isspace((unsigned char)line[j])) ++j;
			out.push_back(line.substr(i, j - i));
			i = j;
		}
	}
	return out;
}

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

std::vector<std::pair<std::string, std::string>> ParseListUps(const std::string& reply)
{
	std::vector<std::pair<std::string, std::string>> out;
	for (const std::string& line : Lines(reply)) {
		std::vector<std::string> t = Tokens(line);
		// UPS <name> "<description>"
		if (t.size() >= 2 && t[0] == "UPS")
			out.push_back({t[1], t.size() >= 3 ? t[2] : std::string()});
	}
	return out;
}

std::map<std::string, std::string> ParseListVar(const std::string& reply, const std::string& ups)
{
	std::map<std::string, std::string> out;
	for (const std::string& line : Lines(reply)) {
		std::vector<std::string> t = Tokens(line);
		// VAR <ups> <key> "<value>"
		if (t.size() >= 4 && t[0] == "VAR" && t[1] == ups)
			out[t[2]] = t[3];
	}
	return out;
}

std::string StatusText(const std::string& status)
{
	struct { const char* flag; const char* text; } kMap[] = {
		{"OL", "in linea"}, {"OB", "a batteria"}, {"LB", "batteria scarica"},
		{"HB", "batteria alta"}, {"RB", "sostituire la batteria"}, {"CHRG", "in carica"},
		{"DISCHRG", "in scarica"}, {"BYPASS", "bypass"}, {"CAL", "in calibrazione"},
		{"OFF", "spento"}, {"OVER", "sovraccarico"}, {"TRIM", "riduce tensione"},
		{"BOOST", "aumenta tensione"}, {"FSD", "arresto forzato"},
	};
	std::string out;
	size_t pos = 0;
	while (pos < status.size()) {
		while (pos < status.size() && std::isspace((unsigned char)status[pos])) ++pos;
		size_t end = pos;
		while (end < status.size() && !std::isspace((unsigned char)status[end])) ++end;
		if (end == pos)
			break;
		std::string flag = status.substr(pos, end - pos);
		pos = end;
		const char* text = flag.c_str();
		for (const auto& m : kMap)
			if (flag == m.flag) { text = m.text; break; }
		if (!out.empty())
			out += ", ";
		out += text;
	}
	return out;
}

std::string RuntimeText(const std::string& seconds)
{
	if (seconds.empty())
		return seconds;
	char* end = nullptr;
	long s = std::strtol(seconds.c_str(), &end, 10);
	if (end == seconds.c_str() || (end != nullptr && *end != '\0') || s < 0)
		return seconds;
	long h = s / 3600;
	long m = (s % 3600) / 60;
	char buf[32];
	if (h > 0)
		std::snprintf(buf, sizeof(buf), "%ld h %ld min", h, m);
	else
		std::snprintf(buf, sizeof(buf), "%ld min", m);
	return buf;
}

std::string NutClient::Exchange(const std::string& command)
{
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return "";
	// Send the command then LOGOUT so the server flushes the reply and closes; we read to EOF.
	std::string req = command + "\nLOGOUT\n";
	if (write(fd, req.data(), req.size()) < 0) { close(fd); return ""; }

	std::string resp;
	char buf[2048];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		resp.append(buf, n);
	close(fd);
	return resp;
}

std::vector<std::pair<std::string, std::string>> NutClient::ListUps()
{
	return ParseListUps(Exchange("LIST UPS"));
}

std::map<std::string, std::string> NutClient::ListVars(const std::string& ups)
{
	return ParseListVar(Exchange("LIST VAR " + ups), ups);
}

} // namespace nut
} // namespace campiello
