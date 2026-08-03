// DaikinClient.cpp
//
// See DaikinClient.h. Hand-rolled Daikin BRP069 "aircon" HTTP client and response parser.

#include "DaikinClient.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace daikin {

std::string UrlDecode(const std::string& in)
{
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size(); ++i) {
		if (in[i] == '%' && i + 2 < in.size()
			&& std::isxdigit((unsigned char)in[i + 1])
			&& std::isxdigit((unsigned char)in[i + 2])) {
			char hex[3] = {in[i + 1], in[i + 2], '\0'};
			out += static_cast<char>(std::strtol(hex, nullptr, 16));
			i += 2;
		} else {
			out += in[i];
		}
	}
	return out;
}

Fields ParseResponse(const std::string& body)
{
	Fields f;
	size_t pos = 0;
	while (pos <= body.size()) {
		size_t comma = body.find(',', pos);
		std::string token = body.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
		pos = (comma == std::string::npos) ? body.size() + 1 : comma + 1;

		// Trim surrounding whitespace (responses can carry a trailing CR/LF).
		size_t a = 0, b = token.size();
		while (a < b && std::isspace((unsigned char)token[a])) ++a;
		while (b > a && std::isspace((unsigned char)token[b - 1])) --b;
		token = token.substr(a, b - a);
		if (token.empty())
			continue;

		size_t eq = token.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = token.substr(0, eq);
		std::string value = UrlDecode(token.substr(eq + 1));
		if (key == "ret")
			f.ok = (value == "OK");
		f.kv[key] = value;
	}
	return f;
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

DaikinClient::Response DaikinClient::HttpGet(const std::string& path)
{
	Response r;
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return r;

	std::string req = "GET " + path + " HTTP/1.1\r\n";
	req += "Host: " + fHost + "\r\n";
	req += "Accept: text/plain\r\n";
	req += "Connection: close\r\n\r\n";
	if (write(fd, req.data(), req.size()) < 0) { close(fd); return r; }

	std::string resp;
	char buf[2048];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		resp.append(buf, n);
	close(fd);

	size_t sp = resp.find(' ');
	if (sp != std::string::npos)
		r.status = std::atoi(resp.c_str() + sp + 1);
	size_t hdrEnd = resp.find("\r\n\r\n");
	r.body = (hdrEnd == std::string::npos) ? std::string() : resp.substr(hdrEnd + 4);
	return r;
}

BasicInfo DaikinClient::GetBasicInfo()
{
	BasicInfo bi;
	Response r = HttpGet("/common/basic_info");
	if (r.status != 200)
		return bi;
	Fields f = ParseResponse(r.body);
	bi.ok = f.ok;
	bi.name = f.Get("name");
	bi.ver = f.Get("ver");
	bi.type = f.Get("type");
	bi.reg = f.Get("reg");
	bi.pow = std::atoi(f.Get("pow", "0").c_str());
	bi.err = std::atoi(f.Get("err", "0").c_str());
	return bi;
}

ControlInfo DaikinClient::GetControlInfo()
{
	ControlInfo ci;
	Response r = HttpGet("/aircon/get_control_info");
	if (r.status != 200)
		return ci;
	Fields f = ParseResponse(r.body);
	ci.ok = f.ok;
	ci.pow = std::atoi(f.Get("pow", "0").c_str());
	ci.mode = std::atoi(f.Get("mode", "0").c_str());
	ci.stemp = f.Get("stemp");
	ci.shum = std::atoi(f.Get("shum", "0").c_str());
	ci.fRate = f.Get("f_rate", "A");
	ci.fDir = std::atoi(f.Get("f_dir", "0").c_str());
	return ci;
}

SensorInfo DaikinClient::GetSensorInfo()
{
	SensorInfo si;
	Response r = HttpGet("/aircon/get_sensor_info");
	if (r.status != 200)
		return si;
	Fields f = ParseResponse(r.body);
	si.ok = f.ok;
	si.htemp = f.Get("htemp");
	si.otemp = f.Get("otemp");
	si.hhum = f.Get("hhum");
	si.cmpfreq = std::atoi(f.Get("cmpfreq", "0").c_str());
	return si;
}

bool DaikinClient::SetControlInfo(int pow, int mode, const std::string& stemp, int shum,
	const std::string& fRate, int fDir)
{
	char path[256];
	std::snprintf(path, sizeof(path),
		"/aircon/set_control_info?pow=%d&mode=%d&stemp=%s&shum=%d&f_rate=%s&f_dir=%d",
		pow, mode, stemp.c_str(), shum, fRate.c_str(), fDir);
	Response r = HttpGet(path);
	if (r.status != 200)
		return false;
	return ParseResponse(r.body).ok;
}

std::string ModeName(int mode)
{
	switch (mode) {
		case 0:
		case 1:
		case 7: return "Automatico";
		case 2: return "Deumidificazione";
		case 3: return "Raffrescamento";
		case 4: return "Riscaldamento";
		case 6: return "Ventilazione";
		default: return "Modo " + std::to_string(mode);
	}
}

std::string FanRateName(const std::string& rate)
{
	if (rate == "A") return "Automatica";
	if (rate == "B") return "Silenziosa";
	if (rate.size() == 1 && rate[0] >= '3' && rate[0] <= '7')
		return "Livello " + std::to_string(rate[0] - '2'); // 3->1 ... 7->5
	return rate.empty() ? "-" : rate;
}

std::string FanDirName(int dir)
{
	switch (dir) {
		case 0: return "Ferma";
		case 1: return "Verticale";
		case 2: return "Orizzontale";
		case 3: return "Verticale e orizzontale";
		default: return "-";
	}
}

} // namespace daikin
} // namespace campiello
