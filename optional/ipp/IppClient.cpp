// IppClient.cpp
//
// See IppClient.h. Hand-rolled IPP encode/decode over plain HTTP.

#include "IppClient.h"

#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace ipp {

// IPP protocol constants (RFC 8010/8011).
namespace {
const uint8_t  kVerMajor = 1, kVerMinor = 1;       // IPP/1.1: widely accepted
const uint16_t kOpGetPrinterAttributes = 0x000B;
const uint16_t kOpPrintJob             = 0x0002;

const uint8_t kTagOperationAttrs = 0x01;
const uint8_t kTagJobAttrs       = 0x02;
const uint8_t kTagEndOfAttrs     = 0x03;
const uint8_t kTagPrinterAttrs   = 0x04;

// value tags
const uint8_t kValInteger   = 0x21;
const uint8_t kValBoolean   = 0x22;
const uint8_t kValEnum      = 0x23;
const uint8_t kValText      = 0x41; // textWithoutLanguage
const uint8_t kValName      = 0x42; // nameWithoutLanguage
const uint8_t kValKeyword   = 0x44;
const uint8_t kValUri       = 0x45;
const uint8_t kValCharset   = 0x47;
const uint8_t kValLanguage  = 0x48;
const uint8_t kValMimeType  = 0x49;
} // namespace

namespace wire {

void PutU8(std::string& b, uint8_t v) { b.push_back((char)v); }
void PutU16(std::string& b, uint16_t v) { b.push_back((char)(v >> 8)); b.push_back((char)(v & 0xff)); }
void PutU32(std::string& b, uint32_t v)
{
	b.push_back((char)(v >> 24)); b.push_back((char)(v >> 16));
	b.push_back((char)(v >> 8));  b.push_back((char)(v & 0xff));
}

void PutAttr(std::string& b, uint8_t valueTag, const std::string& name, const std::string& value)
{
	PutU8(b, valueTag);
	PutU16(b, (uint16_t)name.size());
	b += name;
	PutU16(b, (uint16_t)value.size());
	b += value;
}

uint16_t StatusCode(const std::string& resp)
{
	if (resp.size() < 4)
		return 0;
	return ((uint8_t)resp[2] << 8) | (uint8_t)resp[3];
}

// Decode the attribute groups after the 8-byte header into (name, value) text pairs. Multi-value
// attributes (additional-value tags with an empty name) are appended to the previous name.
std::vector<std::pair<std::string, std::string>> ParseAttributes(const std::string& resp)
{
	std::vector<std::pair<std::string, std::string>> out;
	size_t i = 8; // skip version(2) + status(2) + request-id(4)
	std::string lastName;
	while (i < resp.size()) {
		uint8_t tag = (uint8_t)resp[i++];
		if (tag == kTagEndOfAttrs)
			break;
		if (tag <= 0x0f) // a delimiter (begin-attribute-group) tag: continue into its attributes
			continue;
		if (i + 2 > resp.size())
			break;
		uint16_t nameLen = ((uint8_t)resp[i] << 8) | (uint8_t)resp[i + 1];
		i += 2;
		if (i + nameLen > resp.size())
			break;
		std::string name = resp.substr(i, nameLen);
		i += nameLen;
		if (i + 2 > resp.size())
			break;
		uint16_t valLen = ((uint8_t)resp[i] << 8) | (uint8_t)resp[i + 1];
		i += 2;
		if (i + valLen > resp.size())
			break;
		std::string raw = resp.substr(i, valLen);
		i += valLen;

		std::string value;
		switch (tag) {
			case kValInteger:
			case kValEnum: {
				int32_t n = 0;
				for (unsigned char c : raw) n = (n << 8) | c;
				value = std::to_string(n);
				break;
			}
			case kValBoolean:
				value = (!raw.empty() && raw[0]) ? "si" : "no";
				break;
			default: // text/name/keyword/uri/charset/mime: printable strings
				value = raw;
				break;
		}
		if (nameLen == 0 && !out.empty())
			out.back().second += ", " + value; // additional value of the previous attribute
		else
			out.push_back({name, value});
	}
	return out;
}

} // namespace wire

std::string IppClient::PrinterUri() const
{
	char buf[512];
	std::snprintf(buf, sizeof(buf), "ipp://%s:%d/%s", fHost.c_str(), fPort, fRp.c_str());
	return buf;
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

std::string IppClient::PostIpp(const std::string& ippMessage, const std::string& document,
	int* httpStatus)
{
	*httpStatus = 0;
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return "";

	std::string body = ippMessage + document;
	std::string req = "POST /" + fRp + " HTTP/1.1\r\n";
	req += "Host: " + fHost + ":" + std::to_string(fPort) + "\r\n";
	req += "Content-Type: application/ipp\r\n";
	req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	req += "Connection: close\r\n\r\n";
	req += body;

	if (write(fd, req.data(), req.size()) < 0) { close(fd); return ""; }

	std::string resp;
	char buf[4096];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		resp.append(buf, n);
	close(fd);

	size_t sp = resp.find(' ');
	if (sp != std::string::npos)
		*httpStatus = std::atoi(resp.c_str() + sp + 1);
	size_t hdrEnd = resp.find("\r\n\r\n");
	if (hdrEnd == std::string::npos)
		return "";
	return resp.substr(hdrEnd + 4); // the IPP response message
}

std::vector<std::pair<std::string, std::string>> IppClient::GetPrinterAttributes(bool* okOut)
{
	*okOut = false;
	std::string msg;
	wire::PutU8(msg, kVerMajor);
	wire::PutU8(msg, kVerMinor);
	wire::PutU16(msg, kOpGetPrinterAttributes);
	wire::PutU32(msg, 1); // request-id
	wire::PutU8(msg, kTagOperationAttrs);
	wire::PutAttr(msg, kValCharset,  "attributes-charset",          "utf-8");
	wire::PutAttr(msg, kValLanguage, "attributes-natural-language", "en");
	wire::PutAttr(msg, kValUri,      "printer-uri",                 PrinterUri());
	wire::PutU8(msg, kTagEndOfAttrs);

	int http = 0;
	std::string resp = PostIpp(msg, "", &http);
	if (http != 200 || resp.size() < 8)
		return {};
	uint16_t status = wire::StatusCode(resp);
	*okOut = (status < 0x0100); // successful-ok range
	return wire::ParseAttributes(resp);
}

bool IppClient::PrintFile(const std::string& path, const std::string& docFormat,
	const std::string& jobName)
{
	FILE* f = std::fopen(path.c_str(), "rb");
	if (f == nullptr)
		return false;
	std::string doc;
	char buf[8192];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
		doc.append(buf, n);
	std::fclose(f);
	if (doc.empty())
		return false;

	std::string msg;
	wire::PutU8(msg, kVerMajor);
	wire::PutU8(msg, kVerMinor);
	wire::PutU16(msg, kOpPrintJob);
	wire::PutU32(msg, 2); // request-id
	wire::PutU8(msg, kTagOperationAttrs);
	wire::PutAttr(msg, kValCharset,  "attributes-charset",          "utf-8");
	wire::PutAttr(msg, kValLanguage, "attributes-natural-language", "en");
	wire::PutAttr(msg, kValUri,      "printer-uri",                 PrinterUri());
	wire::PutAttr(msg, kValName,     "requesting-user-name",        "campiello");
	wire::PutAttr(msg, kValName,     "job-name",                    jobName);
	if (!docFormat.empty())
		wire::PutAttr(msg, kValMimeType, "document-format",         docFormat);
	wire::PutU8(msg, kTagEndOfAttrs);

	int http = 0;
	std::string resp = PostIpp(msg, doc, &http);
	if (http != 200 || resp.size() < 8)
		return false;
	return wire::StatusCode(resp) < 0x0100;
}

} // namespace ipp
} // namespace campiello
