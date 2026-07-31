// FireTVRemote.cpp
//
// See FireTVRemote.h. Ported from the validated prototype; protocol unchanged.

#include "FireTVRemote.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace campiello {
namespace firetv {

namespace {

int ParseStatus(const std::string& resp)
{
	// "HTTP/1.1 200 OK" -> 200
	if (resp.size() < 12)
		return 0;
	size_t sp = resp.find(' ');
	if (sp == std::string::npos)
		return 0;
	return std::atoi(resp.c_str() + sp + 1);
}

std::string SplitBody(const std::string& resp)
{
	size_t p = resp.find("\r\n\r\n");
	return p == std::string::npos ? std::string() : resp.substr(p + 4);
}

} // namespace

std::string JsonEscape(const std::string& s)
{
	std::string o;
	for (char c : s) {
		if (c == '"' || c == '\\') {
			o += '\\';
			o += c;
		} else {
			o += c;
		}
	}
	return o;
}

std::string ExtractJsonString(const std::string& json, const std::string& key)
{
	std::string pat = "\"" + key + "\"";
	size_t k = json.find(pat);
	if (k == std::string::npos)
		return "";
	size_t colon = json.find(':', k + pat.size());
	if (colon == std::string::npos)
		return "";
	size_t q1 = json.find('"', colon);
	if (q1 == std::string::npos)
		return "";
	size_t q2 = json.find('"', q1 + 1);
	if (q2 == std::string::npos)
		return "";
	return json.substr(q1 + 1, q2 - q1 - 1);
}

int FireTVRemote::TcpConnect(int port)
{
	char portStr[8];
	std::snprintf(portStr, sizeof(portStr), "%d", port);
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo(fIp.c_str(), portStr, &hints, &res) != 0 || res == nullptr)
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

std::string FireTVRemote::PlainHttp(int port, const std::string& request, int* statusOut)
{
	*statusOut = 0;
	int fd = TcpConnect(port);
	if (fd < 0)
		return "";
	if (write(fd, request.data(), request.size()) < 0) {
		close(fd);
		return "";
	}
	std::string resp;
	char buf[4096];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		resp.append(buf, n);
	close(fd);
	*statusOut = ParseStatus(resp);
	return SplitBody(resp);
}

std::string FireTVRemote::HttpsPost(const std::string& path, const std::string& jsonBody,
	bool withToken, int* statusOut)
{
	*statusOut = 0;
	int fd = TcpConnect(8080);
	if (fd < 0)
		return "";

	SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
	if (ctx == nullptr) {
		close(fd);
		return "";
	}
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr); // Fire TV uses a self-signed cert
	SSL* ssl = SSL_new(ctx);
	SSL_set_fd(ssl, fd);
	SSL_set_tlsext_host_name(ssl, fIp.c_str());
	if (SSL_connect(ssl) != 1) {
		SSL_free(ssl);
		SSL_CTX_free(ctx);
		close(fd);
		return "";
	}

	std::string req = "POST " + path + " HTTP/1.1\r\n";
	req += "Host: " + fIp + ":8080\r\n";
	req += "X-Api-Key: 0987654321\r\n";
	req += "User-Agent: okhttp/4.10.0\r\n";
	req += "Content-Type: application/json\r\n";
	if (withToken && !fToken.empty())
		req += "X-Client-Token: " + fToken + "\r\n";
	req += "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n";
	req += "Connection: close\r\n\r\n";
	req += jsonBody;

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

bool FireTVRemote::Wake()
{
	int status = 0;
	std::string req =
		"POST /apps/FireTVRemote HTTP/1.1\r\n"
		"Host: " + fIp + ":8009\r\n"
		"User-Agent: okhttp/4.10.0\r\n"
		"Content-Type: text/plain; charset=utf-8\r\n"
		"Content-Length: 0\r\n"
		"Connection: close\r\n\r\n";
	PlainHttp(8009, req, &status);
	return status == 200 || status == 201 || status == 204;
}

bool FireTVRemote::RequestPin(const std::string& friendlyName)
{
	int st = 0;
	HttpsPost("/v1/FireTV/pin/display", "{\"friendlyName\":\"" + JsonEscape(friendlyName) + "\"}",
		false, &st);
	return st == 200;
}

std::string FireTVRemote::VerifyPin(const std::string& pin)
{
	int st = 0;
	std::string body = HttpsPost("/v1/FireTV/pin/verify", "{\"pin\":\"" + JsonEscape(pin) + "\"}",
		false, &st);
	if (st != 200)
		return "";
	std::string tok = ExtractJsonString(body, "description");
	if (!tok.empty())
		fToken = tok;
	return tok;
}

bool FireTVRemote::Nav(const std::string& action)
{
	int st = 0;
	HttpsPost("/v1/FireTV?action=" + action, "{\"keyActionType\":\"keyDownUp\"}", true, &st);
	return st == 200 || st == 500;
}

bool FireTVRemote::Media(const std::string& action, const std::string& direction)
{
	int st = 0;
	std::string body;
	if (action == "scan" && !direction.empty())
		body = "{\"direction\":\"" + direction + "\",\"keyAction\":{\"keyActionType\":\"keyDownUp\"}}";
	HttpsPost("/v1/media?action=" + action, body, true, &st);
	return st == 200 || st == 500;
}

bool FireTVRemote::Launch(const std::string& pkg)
{
	int st = 0;
	HttpsPost("/v1/FireTV/app/" + pkg, std::string(), true, &st);
	return st == 200 || st == 500;
}

bool FireTVRemote::Text(const std::string& text)
{
	int st = 0;
	HttpsPost("/v1/FireTV/text", "{\"text\":\"" + JsonEscape(text) + "\"}", true, &st);
	return st == 200 || st == 500;
}

} // namespace firetv
} // namespace campiello
