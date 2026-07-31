// HandlerRegistry.cpp
//
// See HandlerRegistry.h.

#include "HandlerRegistry.h"

#include <dirent.h>

#include <cstdio>

namespace campiello {
namespace vicinato {

namespace {

std::string Trim(const std::string& s)
{
	size_t a = 0;
	size_t b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n'))
		++a;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n'))
		--b;
	return s.substr(a, b - a);
}

bool StartsWith(const std::string& s, const char* prefix)
{
	std::string p(prefix);
	return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

} // namespace

const char* KindName(ServiceKind k)
{
	switch (k) {
		case ServiceKind::Campiello: return "campiello";
		case ServiceKind::Smb:       return "smb";
		case ServiceKind::Sftp:      return "sftp";
		case ServiceKind::Home:      return "home";
		case ServiceKind::Web:       return "web";
		case ServiceKind::Printer:   return "printer";
		case ServiceKind::Media:     return "media";
		default:                     return "other";
	}
}

bool KindFromName(const std::string& name, ServiceKind& out)
{
	for (ServiceKind k : {ServiceKind::Campiello, ServiceKind::Smb, ServiceKind::Sftp,
			ServiceKind::Home, ServiceKind::Web, ServiceKind::Printer, ServiceKind::Media,
			ServiceKind::Other}) {
		if (name == KindName(k)) {
			out = k;
			return true;
		}
	}
	return false;
}

bool ParseHandlerManifest(const std::string& text, DeviceHandler& out)
{
	out = DeviceHandler();
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

		size_t hash = line.find('#');
		if (hash != std::string::npos)
			line = line.substr(0, hash);
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = Trim(line.substr(0, eq));
		std::string value = Trim(line.substr(eq + 1));
		if (key.empty() || value.empty())
			continue;

		if (key == "signature")
			out.signature = value;
		else if (key == "name")
			out.name = value;
		else if (key == "match.type")
			out.matchTypes.push_back(value);
		else if (key == "match.kind") {
			ServiceKind k;
			if (KindFromName(value, k))
				out.matchKinds.push_back(k);
		} else if (StartsWith(key, "action.")) {
			HandlerAction a;
			a.id = key.substr(std::string("action.").size());
			a.label = value;
			if (!a.id.empty())
				out.actions.push_back(a);
		}
	}
	return !out.signature.empty();
}

void HandlerRegistry::LoadFromDir(const std::string& dir)
{
	DIR* d = ::opendir(dir.c_str());
	if (d == nullptr)
		return;
	struct dirent* ent;
	while ((ent = ::readdir(d)) != nullptr) {
		std::string name = ent->d_name;
		if (name.size() < 8 || name.compare(name.size() - 8, 8, ".handler") != 0)
			continue;
		std::string path = dir + "/" + name;
		FILE* f = std::fopen(path.c_str(), "r");
		if (f == nullptr)
			continue;
		std::string text;
		char buf[1024];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
			text.append(buf, n);
		std::fclose(f);
		DeviceHandler handler;
		if (ParseHandlerManifest(text, handler))
			fHandlers.push_back(handler);
	}
	::closedir(d);
}

// Compare two DNS-SD service types ignoring a trailing ".local" (mDNS instances carry it, manifests
// usually do not), e.g. "_amzn-wplay._tcp.local" matches "_amzn-wplay._tcp".
static bool SameServiceType(const std::string& a, const std::string& b)
{
	auto strip = [](const std::string& s) -> std::string {
		const std::string suffix = ".local";
		if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
			return s.substr(0, s.size() - suffix.size());
		return s;
	};
	return strip(a) == strip(b);
}

std::vector<const DeviceHandler*> HandlerRegistry::Match(const NetworkService& service) const
{
	std::vector<const DeviceHandler*> out;
	for (const DeviceHandler& h : fHandlers) {
		bool matched = false;
		for (const std::string& t : h.matchTypes) {
			if (SameServiceType(t, service.serviceType)) {
				matched = true;
				break;
			}
		}
		if (!matched) {
			for (ServiceKind k : h.matchKinds) {
				if (k == service.kind) {
					matched = true;
					break;
				}
			}
		}
		if (matched)
			out.push_back(&h);
	}
	return out;
}

const DeviceHandler* HandlerRegistry::DefaultHandler(const NetworkService& service) const
{
	std::vector<const DeviceHandler*> matches = Match(service);
	return matches.empty() ? nullptr : matches.front();
}

} // namespace vicinato
} // namespace campiello
