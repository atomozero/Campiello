// HueBridge.cpp
//
// See HueBridge.h. TLS/socket handling mirrors optional/firetv/FireTVRemote.cpp (self-signed LAN
// device); the wire protocol is the Hue local REST API.

#include "HueBridge.h"

#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>

namespace campiello {
namespace hue {

std::string JsonEscape(const std::string& s)
{
	std::string out;
	for (char c : s) {
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:   out += c;      break;
		}
	}
	return out;
}

// Extract the string value of "key":"value" (first occurrence). Empty if absent.
std::string ExtractJsonString(const std::string& json, const std::string& key)
{
	std::string needle = "\"" + key + "\"";
	size_t k = json.find(needle);
	if (k == std::string::npos)
		return "";
	size_t colon = json.find(':', k + needle.size());
	if (colon == std::string::npos)
		return "";
	size_t q1 = json.find('"', colon + 1);
	if (q1 == std::string::npos)
		return "";
	// handle escaped quotes inside the value
	std::string out;
	for (size_t i = q1 + 1; i < json.size(); ++i) {
		char c = json[i];
		if (c == '\\' && i + 1 < json.size()) { out += json[i + 1]; ++i; continue; }
		if (c == '"') break;
		out += c;
	}
	return out;
}

// Split the top-level objects of the "data":[ ... ] array, tracking brace depth and skipping strings.
std::vector<std::string> SplitJsonDataObjects(const std::string& json)
{
	std::vector<std::string> out;
	size_t d = json.find("\"data\"");
	if (d == std::string::npos)
		return out;
	size_t lb = json.find('[', d);
	if (lb == std::string::npos)
		return out;
	int depth = 0;
	bool inStr = false;
	size_t objStart = std::string::npos;
	for (size_t i = lb + 1; i < json.size(); ++i) {
		char c = json[i];
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
				out.push_back(json.substr(objStart, i - objStart + 1));
				objStart = std::string::npos;
			}
			continue;
		}
		if (c == ']' && depth == 0)
			break;
	}
	return out;
}

namespace {

int ParseStatus(const std::string& resp)
{
	// "HTTP/1.1 200 OK"
	size_t sp = resp.find(' ');
	if (sp == std::string::npos)
		return 0;
	return std::atoi(resp.c_str() + sp + 1);
}

std::string SplitBody(const std::string& resp)
{
	size_t p = resp.find("\r\n\r\n");
	if (p == std::string::npos)
		return "";
	std::string body = resp.substr(p + 4);
	// The bridge replies with chunked transfer-encoding; strip a leading chunk-size line and any
	// trailing chunk markers heuristically (the payload is JSON starting with '{' or '[').
	size_t j = body.find_first_of("{[");
	size_t e = body.find_last_of("}]");
	if (j != std::string::npos && e != std::string::npos && e >= j)
		return body.substr(j, e - j + 1);
	return body;
}

int TcpConnect(const std::string& ip, int port)
{
	char portStr[8];
	std::snprintf(portStr, sizeof(portStr), "%d", port);
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo(ip.c_str(), portStr, &hints, &res) != 0 || res == nullptr)
		return -1;
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		freeaddrinfo(res);
		return -1;
	}
	if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		close(fd);
		freeaddrinfo(res);
		return -1;
	}
	freeaddrinfo(res);
	return fd;
}

} // namespace

std::string HueBridge::HttpsRequest(const std::string& method, const std::string& path,
	const std::string& body, bool withKey, int* statusOut)
{
	*statusOut = 0;
	int fd = TcpConnect(fIp, 443);
	if (fd < 0)
		return "";

	SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
	if (ctx == nullptr) {
		close(fd);
		return "";
	}
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr); // bridge uses a self-signed cert
	SSL* ssl = SSL_new(ctx);
	SSL_set_fd(ssl, fd);
	SSL_set_tlsext_host_name(ssl, fIp.c_str());
	if (SSL_connect(ssl) != 1) {
		SSL_free(ssl);
		SSL_CTX_free(ctx);
		close(fd);
		return "";
	}

	std::string req = method + " " + path + " HTTP/1.1\r\n";
	req += "Host: " + fIp + "\r\n";
	if (withKey && !fAppKey.empty())
		req += "hue-application-key: " + fAppKey + "\r\n";
	req += "Content-Type: application/json\r\n";
	req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	req += "Connection: close\r\n\r\n";
	req += body;

	SSL_write(ssl, req.data(), static_cast<int>(req.size()));

	std::string resp;
	char buf[4096];
	int n;
	while ((n = SSL_read(ssl, buf, sizeof(buf))) > 0)
		resp.append(buf, n);

	SSL_shutdown(ssl);
	SSL_free(ssl);
	SSL_CTX_free(ctx);
	close(fd);

	*statusOut = ParseStatus(resp);
	return SplitBody(resp);
}

std::string HueBridge::Pair(const std::string& appName)
{
	int st = 0;
	std::string body = "{\"devicetype\":\"" + JsonEscape(appName)
		+ "\",\"generateclientkey\":true}";
	std::string resp = HttpsRequest("POST", "/api", body, false, &st);
	std::string key = ExtractJsonString(resp, "username");
	if (!key.empty())
		fAppKey = key;
	return key;
}

std::vector<Light> HueBridge::ListLights()
{
	std::vector<Light> lights;
	int st = 0;
	std::string resp = HttpsRequest("GET", "/clip/v2/resource/light", "", true, &st);
	if (st != 200)
		return lights;
	for (const std::string& obj : SplitJsonDataObjects(resp)) {
		Light l;
		l.id = ExtractJsonString(obj, "id"); // the light's own v2 resource id (not owner.rid/id_v1)
		l.name = ExtractJsonString(obj, "name");
		// on.on
		size_t onPos = obj.find("\"on\"");
		if (onPos != std::string::npos)
			l.on = obj.find("true", onPos) != std::string::npos
				&& obj.find("true", onPos) < obj.find('}', onPos);
		// dimming.brightness
		size_t br = obj.find("\"brightness\"");
		if (br != std::string::npos) {
			size_t colon = obj.find(':', br);
			if (colon != std::string::npos) {
				l.dimmable = true;
				l.brightness = std::atoi(obj.c_str() + colon + 1);
			}
		}
		if (!l.id.empty())
			lights.push_back(l);
	}
	return lights;
}

bool HueBridge::SetOn(const std::string& id, bool on)
{
	int st = 0;
	std::string body = std::string("{\"on\":{\"on\":") + (on ? "true" : "false") + "}}";
	HttpsRequest("PUT", "/clip/v2/resource/light/" + id, body, true, &st);
	return st == 200;
}

bool HueBridge::SetBrightness(const std::string& id, int percent)
{
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	int st = 0;
	std::string body = "{\"dimming\":{\"brightness\":" + std::to_string(percent) + "}}";
	HttpsRequest("PUT", "/clip/v2/resource/light/" + id, body, true, &st);
	return st == 200;
}

} // namespace hue
} // namespace campiello
