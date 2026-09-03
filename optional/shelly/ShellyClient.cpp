// ShellyClient.cpp
//
// See ShellyClient.h. Plain HTTP/1.0 over a POSIX socket (Shelly speaks cleartext HTTP on port 80),
// plus a small JSON scanner. No TLS, no third-party dependency.

#include "ShellyClient.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace campiello {
namespace shelly {

// --------------------------------------------------------------------------- JSON scanners

// Locate the position just after the ':' that follows the first occurrence of "key".
static size_t FindValuePos(const std::string& json, const std::string& key)
{
	std::string needle = "\"" + key + "\"";
	size_t k = json.find(needle);
	if (k == std::string::npos)
		return std::string::npos;
	size_t colon = json.find(':', k + needle.size());
	if (colon == std::string::npos)
		return std::string::npos;
	size_t p = colon + 1;
	while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r'))
		++p;
	return p;
}

std::string JsonString(const std::string& json, const std::string& key)
{
	size_t p = FindValuePos(json, key);
	if (p == std::string::npos || p >= json.size() || json[p] != '"')
		return "";
	std::string out;
	for (size_t i = p + 1; i < json.size(); ++i) {
		char c = json[i];
		if (c == '\\' && i + 1 < json.size()) { out += json[i + 1]; ++i; continue; }
		if (c == '"') break;
		out += c;
	}
	return out;
}

bool JsonBool(const std::string& json, const std::string& key, bool& out)
{
	size_t p = FindValuePos(json, key);
	if (p == std::string::npos)
		return false;
	if (json.compare(p, 4, "true") == 0) { out = true; return true; }
	if (json.compare(p, 5, "false") == 0) { out = false; return true; }
	return false;
}

bool JsonNumber(const std::string& json, const std::string& key, double& out)
{
	size_t p = FindValuePos(json, key);
	if (p == std::string::npos || p >= json.size())
		return false;
	char c = json[p];
	if (!(c == '-' || c == '+' || c == '.' || std::isdigit((unsigned char)c)))
		return false; // null, string, object, etc.
	out = std::atof(json.c_str() + p);
	return true;
}

std::string JsonValueBlock(const std::string& json, const std::string& key)
{
	size_t p = FindValuePos(json, key);
	if (p == std::string::npos || p >= json.size())
		return "";
	char open = json[p];
	char close;
	if (open == '{') close = '}';
	else if (open == '[') close = ']';
	else return "";
	int depth = 0;
	bool inStr = false;
	for (size_t i = p; i < json.size(); ++i) {
		char c = json[i];
		if (inStr) {
			if (c == '\\') { ++i; continue; }
			if (c == '"') inStr = false;
			continue;
		}
		if (c == '"') { inStr = true; continue; }
		if (c == open) ++depth;
		else if (c == close) {
			--depth;
			if (depth == 0)
				return json.substr(p, i - p + 1);
		}
	}
	return "";
}

std::vector<std::string> JsonArrayObjects(const std::string& arrayBody)
{
	std::vector<std::string> out;
	int depth = 0;
	bool inStr = false;
	size_t objStart = std::string::npos;
	for (size_t i = 0; i < arrayBody.size(); ++i) {
		char c = arrayBody[i];
		if (inStr) {
			if (c == '\\') { ++i; continue; }
			if (c == '"') inStr = false;
			continue;
		}
		if (c == '"') { inStr = true; continue; }
		if (c == '{') { if (depth == 0) objStart = i; ++depth; continue; }
		if (c == '}') {
			--depth;
			if (depth == 0 && objStart != std::string::npos) {
				out.push_back(arrayBody.substr(objStart, i - objStart + 1));
				objStart = std::string::npos;
			}
		}
	}
	return out;
}

int GenerationFromShelly(const std::string& shellyJson)
{
	double g = 0;
	if (JsonNumber(shellyJson, "gen", g) && g >= 1)
		return (int)g;
	return 1; // gen1 devices omit the field
}

// --------------------------------------------------------------------------- status -> channels

std::vector<ShellyChannel> ParseChannelsGen2(const std::string& statusJson)
{
	std::vector<ShellyChannel> out;
	for (int n = 0; n < 8; ++n) {
		std::string block = JsonValueBlock(statusJson, "switch:" + std::to_string(n));
		if (block.empty())
			continue;
		ShellyChannel ch;
		ch.id = n;
		ch.name = "Canale " + std::to_string(n + 1);
		ch.controllable = true;
		JsonBool(block, "output", ch.on);
		double v = 0;
		if (JsonNumber(block, "apower", v)) { ch.hasPower = true; ch.power = v; }
		if (JsonNumber(block, "voltage", v)) ch.voltage = v;
		std::string aen = JsonValueBlock(block, "aenergy");
		if (JsonNumber(aen, "total", v)) ch.total = v; // Wh
		out.push_back(ch);
	}
	return out;
}

std::vector<ShellyChannel> ParseChannelsGen1(const std::string& statusJson)
{
	std::vector<ShellyChannel> out;
	std::vector<std::string> relays = JsonArrayObjects(JsonValueBlock(statusJson, "relays"));
	std::vector<std::string> meters = JsonArrayObjects(JsonValueBlock(statusJson, "meters"));
	for (size_t i = 0; i < relays.size(); ++i) {
		ShellyChannel ch;
		ch.id = (int)i;
		ch.name = "Canale " + std::to_string(i + 1);
		ch.controllable = true;
		JsonBool(relays[i], "ison", ch.on);
		if (i < meters.size()) {
			double v = 0;
			if (JsonNumber(meters[i], "power", v)) { ch.hasPower = true; ch.power = v; }
			if (JsonNumber(meters[i], "total", v)) ch.total = v / 60.0; // gen1 meter total is Wmin
		}
		out.push_back(ch);
	}
	// A Shelly EM exposes clamp channels as read-only power meters (emeters, energy already in Wh).
	std::vector<std::string> emeters = JsonArrayObjects(JsonValueBlock(statusJson, "emeters"));
	for (size_t i = 0; i < emeters.size(); ++i) {
		ShellyChannel ch;
		ch.id = (int)i;
		ch.name = "Misura " + std::to_string(i + 1);
		ch.controllable = false;
		ch.hasPower = true;
		double v = 0;
		if (JsonNumber(emeters[i], "power", v)) ch.power = v;
		if (JsonNumber(emeters[i], "voltage", v)) ch.voltage = v;
		if (JsonNumber(emeters[i], "total", v)) ch.total = v; // Wh
		out.push_back(ch);
	}
	return out;
}

// --------------------------------------------------------------------------- HTTP transport

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
	struct timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		close(fd);
		freeaddrinfo(res);
		return -1;
	}
	freeaddrinfo(res);
	return fd;
}

int ParseStatus(const std::string& resp)
{
	size_t sp = resp.find(' ');
	if (sp == std::string::npos)
		return 0;
	return std::atoi(resp.c_str() + sp + 1);
}

std::string SplitBody(const std::string& resp)
{
	size_t p = resp.find("\r\n\r\n");
	return (p == std::string::npos) ? std::string() : resp.substr(p + 4);
}

} // namespace

std::string ShellyClient::HttpGet(const std::string& path, int* statusOut)
{
	*statusOut = 0;
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return "";
	std::string req = "GET " + path + " HTTP/1.0\r\n";
	req += "Host: " + fHost + "\r\n";
	req += "User-Agent: Campiello\r\n";
	req += "Connection: close\r\n\r\n";
	ssize_t off = 0;
	while (off < (ssize_t)req.size()) {
		ssize_t w = send(fd, req.data() + off, req.size() - off, 0);
		if (w <= 0) { close(fd); return ""; }
		off += w;
	}
	std::string resp;
	char buf[4096];
	ssize_t n;
	while ((n = recv(fd, buf, sizeof(buf), 0)) > 0)
		resp.append(buf, n);
	close(fd);
	*statusOut = ParseStatus(resp);
	return SplitBody(resp);
}

// --------------------------------------------------------------------------- device operations

int ShellyClient::EnsureGen()
{
	if (fGen >= 1)
		return fGen;
	int st = 0;
	std::string body = HttpGet("/shelly", &st);
	fGen = (st == 200) ? GenerationFromShelly(body) : 1;
	return fGen;
}

bool ShellyClient::FetchInfo(ShellyInfo& out)
{
	int st = 0;
	std::string body = HttpGet("/shelly", &st);
	if (st != 200)
		return false;
	out = ShellyInfo();
	out.gen = GenerationFromShelly(body);
	fGen = out.gen;
	out.mac = JsonString(body, "mac");
	if (out.gen >= 2) {
		out.id = JsonString(body, "id");
		out.model = JsonString(body, "model");
		out.app = JsonString(body, "app");
		out.fw = JsonString(body, "ver");
		JsonBool(body, "auth_en", out.authEnabled);
	} else {
		out.model = JsonString(body, "type");
		out.app = out.model;
		out.fw = JsonString(body, "fw");
		JsonBool(body, "auth", out.authEnabled);
		out.id = out.mac.empty() ? std::string("shelly") : ("shelly-" + out.mac);
	}
	return true;
}

bool ShellyClient::FetchChannels(std::vector<ShellyChannel>& out)
{
	out.clear();
	int st = 0;
	if (EnsureGen() >= 2) {
		std::string body = HttpGet("/rpc/Shelly.GetStatus", &st);
		if (st != 200)
			return false;
		out = ParseChannelsGen2(body);
		return true;
	}
	std::string body = HttpGet("/status", &st);
	if (st != 200)
		return false;
	out = ParseChannelsGen1(body);
	return true;
}

bool ShellyClient::SetOn(int id, bool on)
{
	int st = 0;
	if (EnsureGen() >= 2) {
		HttpGet("/rpc/Switch.Set?id=" + std::to_string(id) + "&on=" + (on ? "true" : "false"), &st);
	} else {
		HttpGet("/relay/" + std::to_string(id) + "?turn=" + (on ? "on" : "off"), &st);
	}
	return st == 200;
}

bool ShellyClient::Toggle(int id)
{
	int st = 0;
	if (EnsureGen() >= 2)
		HttpGet("/rpc/Switch.Toggle?id=" + std::to_string(id), &st);
	else
		HttpGet("/relay/" + std::to_string(id) + "?turn=toggle", &st);
	return st == 200;
}

} // namespace shelly
} // namespace campiello
