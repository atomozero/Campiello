// IppClient.h
//
// Minimal Internet Printing Protocol (IPP) client for a printer discovered on the network. IPP is an
// HTTP POST (Content-Type application/ipp) carrying a binary IPP message; this implements just enough
// to query a printer and submit a document:
//   Get-Printer-Attributes (0x000B): read the printer's name, state, model, supported formats.
//   Print-Job              (0x0002): submit a document (PDF/JPEG/PWG-raster) the printer supports.
//
// The encoding is done by hand (no external library), so this module needs no third-party
// dependency: MIT-clean, links only libbe + the network kit. Transport is plain HTTP on port 631
// (_ipp._tcp). TLS printing (_ipps._tcp) is a documented follow-up (would add OpenSSL, optional).
//
// References: PWG "How to Use the Internet Printing Protocol" (istopwg.github.io/ipp/ippguide.html);
// the binary layout cross-checked against github.com/williamkapke/ipp and github.com/watson/ipp-encoder.

#ifndef CAMPIELLO_IPP_IPPCLIENT_H
#define CAMPIELLO_IPP_IPPCLIENT_H

#include <string>
#include <utility>
#include <vector>

namespace campiello {
namespace ipp {

class IppClient {
public:
	// `rp` is the resource path from the printer's mDNS TXT (default "ipp/print" for AirPrint).
	explicit IppClient(const std::string& host, int port = 631,
		const std::string& rp = "ipp/print")
		: fHost(host), fPort(port), fRp(rp) {}

	// Query the printer. Returns decoded (name, value) attribute pairs (empty on failure); `okOut`
	// reports whether the request reached the printer and returned a successful IPP status.
	std::vector<std::pair<std::string, std::string>> GetPrinterAttributes(bool* okOut);

	// Submit a document file. `docFormat` is a MIME type the printer supports (e.g. "application/pdf",
	// "image/jpeg", "application/octet-stream" to let the printer auto-detect). Returns true on a
	// successful IPP status.
	bool PrintFile(const std::string& path, const std::string& docFormat,
		const std::string& jobName);

	std::string PrinterUri() const;

private:
	std::string PostIpp(const std::string& ippMessage, const std::string& document, int* httpStatus);

	std::string fHost;
	int         fPort;
	std::string fRp;
};

// IPP message builders/parsers (exposed for testing; dependency-free).
namespace wire {
	void PutU8(std::string& b, uint8_t v);
	void PutU16(std::string& b, uint16_t v);
	void PutU32(std::string& b, uint32_t v);
	// Append one attribute: value tag, name, value (as raw bytes).
	void PutAttr(std::string& b, uint8_t valueTag, const std::string& name, const std::string& value);
	// Parse the printer-attributes group of a response into (name,value) text pairs.
	std::vector<std::pair<std::string, std::string>> ParseAttributes(const std::string& resp);
	// The 2-byte IPP status-code at the start of a response (0 if the buffer is too short).
	uint16_t StatusCode(const std::string& resp);
}

} // namespace ipp
} // namespace campiello

#endif // CAMPIELLO_IPP_IPPCLIENT_H
