// ShellyPowerLog.cpp - see ShellyPowerLog.h.

#include "ShellyPowerLog.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <sys/types.h>

namespace campiello {
namespace shelly {

namespace {

// Create every component of a directory path (like "mkdir -p"). Best-effort.
void MakeDirs(const std::string& path)
{
	std::string acc;
	for (size_t i = 0; i < path.size(); ++i) {
		acc.push_back(path[i]);
		if (path[i] == '/' && acc.size() > 1)
			mkdir(acc.c_str(), 0755);
	}
	mkdir(path.c_str(), 0755);
}

std::string HomeDir()
{
	const char* home = std::getenv("HOME");
	if (home != nullptr && home[0] != '\0')
		return home;
	return "/boot/home";
}

} // namespace

std::string ShellyPowerLog::BaseDir()
{
	const char* override = std::getenv("CAMPIELLO_SHELLY_DIR");
	if (override != nullptr && override[0] != '\0')
		return override;
	return HomeDir() + "/config/settings/Campiello/shelly";
}

std::string ShellyPowerLog::SanitizeHost(const std::string& host)
{
	std::string out;
	out.reserve(host.size());
	for (char c : host) {
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '.' || c == '-' || c == '_';
		out.push_back(ok ? c : '_');
	}
	if (out.empty())
		out = "device";
	return out;
}

std::string ShellyPowerLog::LogPathForHost(const std::string& host)
{
	return BaseDir() + "/" + SanitizeHost(host) + ".log";
}

std::string ShellyPowerLog::WatchPath()
{
	return BaseDir() + "/watch.list";
}

std::vector<PowerSample> ShellyPowerLog::ParseLog(const std::string& text, long since)
{
	std::vector<PowerSample> out;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty())
			continue;
		std::istringstream ls(line);
		long t = 0;
		double w = 0.0;
		if (!(ls >> t >> w))
			continue;
		if (since > 0 && t < since)
			continue;
		PowerSample s;
		s.t = t;
		s.w = (float)w;
		out.push_back(s);
	}
	return out;
}

std::vector<PowerSample> ShellyPowerLog::Load(const std::string& host, long since)
{
	std::ifstream in(LogPathForHost(host));
	if (!in)
		return {};
	std::stringstream buf;
	buf << in.rdbuf();
	return ParseLog(buf.str(), since);
}

bool ShellyPowerLog::Append(const std::string& host, long epoch, float watts)
{
	if (!std::isfinite(watts))
		return false;
	MakeDirs(BaseDir());
	std::ofstream out(LogPathForHost(host), std::ios::app);
	if (!out)
		return false;
	out << epoch << ' ' << watts << '\n';
	return out.good();
}

void ShellyPowerLog::Trim(const std::string& host, long now)
{
	long cutoff = now - kWindowSeconds;
	std::vector<PowerSample> kept = Load(host, cutoff);
	// Only rewrite if something would actually be dropped: compare against the full count.
	std::vector<PowerSample> all = Load(host, 0);
	if (kept.size() == all.size())
		return;
	std::ofstream out(LogPathForHost(host), std::ios::trunc);
	if (!out)
		return;
	for (const PowerSample& s : kept)
		out << s.t << ' ' << s.w << '\n';
}

std::vector<WatchEntry> ShellyPowerLog::ParseWatch(const std::string& text)
{
	std::vector<WatchEntry> out;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		// host \t port \t gen \t name (name may contain spaces; it is the last field).
		std::string fields[4];
		size_t start = 0;
		int fi = 0;
		for (; fi < 4; ++fi) {
			size_t tab = (fi < 3) ? line.find('\t', start) : std::string::npos;
			fields[fi] = line.substr(start, tab == std::string::npos ? std::string::npos : tab - start);
			if (tab == std::string::npos)
				break;
			start = tab + 1;
		}
		if (fields[0].empty())
			continue;
		WatchEntry e;
		e.host = fields[0];
		e.port = fields[1].empty() ? 80 : std::atoi(fields[1].c_str());
		e.gen = fields[2].empty() ? 0 : std::atoi(fields[2].c_str());
		e.name = fields[3];
		out.push_back(e);
	}
	return out;
}

std::vector<WatchEntry> ShellyPowerLog::LoadWatch()
{
	std::ifstream in(WatchPath());
	if (!in)
		return {};
	std::stringstream buf;
	buf << in.rdbuf();
	return ParseWatch(buf.str());
}

void ShellyPowerLog::AddWatch(const WatchEntry& entry)
{
	if (entry.host.empty())
		return;
	std::vector<WatchEntry> entries = LoadWatch();
	bool changed = false;
	bool found = false;
	for (WatchEntry& e : entries) {
		if (e.host == entry.host) {
			found = true;
			// Refresh port/gen/name if they carry more information.
			if (entry.port > 0 && e.port != entry.port) { e.port = entry.port; changed = true; }
			if (entry.gen > 0 && e.gen != entry.gen) { e.gen = entry.gen; changed = true; }
			if (!entry.name.empty() && e.name != entry.name) { e.name = entry.name; changed = true; }
			break;
		}
	}
	if (!found) {
		entries.push_back(entry);
		changed = true;
	}
	if (!changed)
		return;

	MakeDirs(BaseDir());
	std::ofstream out(WatchPath(), std::ios::trunc);
	if (!out)
		return;
	for (const WatchEntry& e : entries) {
		out << e.host << '\t' << (e.port > 0 ? e.port : 80) << '\t' << e.gen << '\t' << e.name << '\n';
	}
}

} // namespace shelly
} // namespace campiello
