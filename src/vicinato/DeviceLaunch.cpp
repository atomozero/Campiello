// DeviceLaunch.cpp
//
// See DeviceLaunch.h.

#include "DeviceLaunch.h"

#include <cstdlib>

namespace campiello {
namespace vicinato {

std::string EncodeArg(const std::string& value)
{
	static const char* kHex = "0123456789ABCDEF";
	std::string out;
	out.reserve(value.size());
	for (unsigned char c : value) {
		if (c <= 0x20 || c == '%' || c == 0x7f) {
			out.push_back('%');
			out.push_back(kHex[c >> 4]);
			out.push_back(kHex[c & 0x0f]);
		} else {
			out.push_back(static_cast<char>(c));
		}
	}
	return out;
}

std::string DecodeArg(const std::string& value)
{
	auto nyb = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	std::string out;
	out.reserve(value.size());
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '%' && i + 2 < value.size()) {
			int hi = nyb(value[i + 1]);
			int lo = nyb(value[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out.push_back(static_cast<char>((hi << 4) | lo));
				i += 2;
				continue;
			}
		}
		out.push_back(value[i]);
	}
	return out;
}

std::vector<std::string> BuildLaunchArgs(const NetworkService& service, const std::string& action)
{
	std::vector<std::string> args;
	args.push_back("host=" + EncodeArg(service.host));
	args.push_back("port=" + std::to_string(static_cast<unsigned>(service.port)));
	if (!service.serviceType.empty())
		args.push_back("type=" + EncodeArg(service.serviceType));
	if (!service.label.empty())
		args.push_back("name=" + EncodeArg(service.label));
	if (!action.empty())
		args.push_back("action=" + EncodeArg(action));
	for (const auto& kv : service.txt)
		args.push_back("txt." + EncodeArg(kv.first) + "=" + EncodeArg(kv.second));
	return args;
}

DeviceInfo ParseDevice(int argc, const char* const* argv)
{
	DeviceInfo out;
	for (int i = 1; i < argc; ++i) {
		std::string token = argv[i];
		size_t eq = token.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = token.substr(0, eq);
		std::string value = token.substr(eq + 1);
		if (key == "host")
			out.host = DecodeArg(value);
		else if (key == "port")
			out.port = static_cast<uint16_t>(std::strtoul(value.c_str(), nullptr, 10));
		else if (key == "type")
			out.type = DecodeArg(value);
		else if (key == "name")
			out.name = DecodeArg(value);
		else if (key == "action")
			out.action = DecodeArg(value);
		else if (key.compare(0, 4, "txt.") == 0)
			out.txt.emplace_back(DecodeArg(key.substr(4)), DecodeArg(value));
	}
	return out;
}

} // namespace vicinato
} // namespace campiello
