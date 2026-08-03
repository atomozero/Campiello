// DaikinClient.h
//
// A tiny client for the Daikin BRP069/BRP072 Wi-Fi adapter local HTTP API (the classic "aircon"
// API advertised as _dkapi._tcp, adp_kind=3). It is plain HTTP on port 80 with no authentication:
// text responses of the form "ret=OK,key=value,key=value,..." (some values percent-encoded). This
// is the documented, open, license-clean local control path for many Daikin split units - no cloud,
// no heavy crypto - so Campiello can read the real state and actually command the unit.
//
// Endpoints used:
//   GET /common/basic_info        - adapter identity (name, firmware, region, power, error)
//   GET /aircon/get_control_info  - power, mode, target temp, humidity, fan rate/direction
//   GET /aircon/get_sensor_info   - indoor/outdoor temperature, compressor frequency
//   GET /aircon/set_control_info?pow=..&mode=..&stemp=..&shum=..&f_rate=..&f_dir=..  - command
//
// The set endpoint is stateful: the unit expects the full pow/mode/stemp/shum set on every call, so
// the caller reads the current control info, changes what it wants, and sends the whole set back.
//
// Portable: standard C++ + POSIX sockets, no BeAPI and no third-party dependency, so ParseResponse
// is unit-testable off Haiku. References: pydaikin, ael-code/daikin-control (behavioural, not code).

#ifndef CAMPIELLO_DAIKIN_DAIKINCLIENT_H
#define CAMPIELLO_DAIKIN_DAIKINCLIENT_H

#include <map>
#include <string>

namespace campiello {
namespace daikin {

// A parsed "ret=OK,key=value,..." response.
struct Fields {
	bool ok = false; // ret == OK
	std::map<std::string, std::string> kv;

	bool Has(const std::string& key) const { return kv.find(key) != kv.end(); }
	std::string Get(const std::string& key, const std::string& def = "") const
	{
		auto it = kv.find(key);
		return it == kv.end() ? def : it->second;
	}
};

// Parse a Daikin adapter response body. Splits on ',', each token on the first '=', percent-decodes
// values, and sets ok from the "ret" field. Robust to leading/trailing whitespace and empty tokens.
Fields ParseResponse(const std::string& body);

// Percent-decode a Daikin-encoded value ("%53%6f" -> "So"). '+' is left as-is (Daikin does not use
// form encoding). Invalid escapes are copied through verbatim.
std::string UrlDecode(const std::string& in);

struct BasicInfo {
	bool ok = false;
	std::string name;   // human name (percent-decoded), may be empty
	std::string ver;    // firmware version
	std::string type;   // "aircon"
	std::string reg;    // region ("eu"...)
	int pow = 0;        // 1 if powered on
	int err = 0;        // error code, 0 = none
};

struct ControlInfo {
	bool ok = false;
	int pow = 0;           // 0 off / 1 on
	int mode = 0;          // 0/1/7 auto, 2 dry, 3 cool, 4 heat, 6 fan
	std::string stemp;     // target temp as advertised ("25.0", or "M"/"--" in fan/dry)
	int shum = 0;          // target humidity
	std::string fRate;     // "A" auto, "B" silent, "3".."7" levels
	int fDir = 0;          // 0 stop, 1 vertical, 2 horizontal, 3 both
};

struct SensorInfo {
	bool ok = false;
	std::string htemp; // indoor temperature ("-" if unavailable)
	std::string otemp; // outdoor temperature
	std::string hhum;  // indoor humidity ("-" if unavailable)
	int cmpfreq = 0;   // compressor frequency (rough load indicator)
};

class DaikinClient {
public:
	explicit DaikinClient(const std::string& host, int port = 80) : fHost(host), fPort(port) {}

	BasicInfo GetBasicInfo();
	ControlInfo GetControlInfo();
	SensorInfo GetSensorInfo();

	// Send the full required control set. Returns true only on ret=OK.
	bool SetControlInfo(int pow, int mode, const std::string& stemp, int shum,
		const std::string& fRate, int fDir);

	struct Response { int status = 0; std::string body; };

private:
	Response HttpGet(const std::string& path);

	std::string fHost;
	int fPort;
};

// UI helpers (Italian), pure functions on the protocol codes.
std::string ModeName(int mode);
std::string FanRateName(const std::string& rate);
std::string FanDirName(int dir);

} // namespace daikin
} // namespace campiello

#endif // CAMPIELLO_DAIKIN_DAIKINCLIENT_H
