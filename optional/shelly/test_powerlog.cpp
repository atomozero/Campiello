// test_powerlog.cpp - offline tests for ShellyPowerLog (parsing, filtering, watch list, round-trip).
//
//   g++ -std=c++17 -o test_powerlog test_powerlog.cpp ShellyPowerLog.cpp && ./test_powerlog

#include "ShellyPowerLog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

using namespace campiello::shelly;

static int gChecks = 0;
static int gFails = 0;

static void Check(bool ok, const char* what)
{
	++gChecks;
	if (!ok) {
		++gFails;
		std::printf("  FAIL: %s\n", what);
	}
}

int main()
{
	// --- ParseLog: valid lines, filtering by 'since', junk skipped.
	{
		std::string log =
			"100 5\n"
			"160 7.5\n"
			"\n"
			"garbage line\n"
			"220 0\n"
			"280 12.25\n";
		std::vector<PowerSample> all = ShellyPowerLog::ParseLog(log, 0);
		Check(all.size() == 4, "ParseLog keeps 4 valid samples");
		Check(all[0].t == 100 && all[0].w == 5.0f, "ParseLog first sample");
		Check(all[3].t == 280 && all[3].w == 12.25f, "ParseLog last sample");

		std::vector<PowerSample> recent = ShellyPowerLog::ParseLog(log, 200);
		Check(recent.size() == 2, "ParseLog since=200 keeps 2");
		Check(recent[0].t == 220, "ParseLog since drops older");
	}

	// --- SanitizeHost: IPs keep dots, odd chars become underscores.
	{
		Check(ShellyPowerLog::SanitizeHost("192.168.1.5") == "192.168.1.5", "sanitize keeps IP");
		Check(ShellyPowerLog::SanitizeHost("shelly plug/x") == "shelly_plug_x", "sanitize replaces");
		Check(ShellyPowerLog::SanitizeHost("") == "device", "sanitize empty -> device");
	}

	// --- ParseWatch: fields, defaults, name with spaces, comments.
	{
		std::string w =
			"# comment\n"
			"192.168.1.5\t80\t2\tPresa Plus Plug S\n"
			"10.0.0.9\t\t\t\n"
			"host-only\n";
		std::vector<WatchEntry> es = ShellyPowerLog::ParseWatch(w);
		Check(es.size() == 3, "ParseWatch keeps 3 (comment skipped)");
		Check(es[0].host == "192.168.1.5" && es[0].port == 80 && es[0].gen == 2, "ParseWatch fields");
		Check(es[0].name == "Presa Plus Plug S", "ParseWatch name with spaces");
		Check(es[1].host == "10.0.0.9" && es[1].port == 80 && es[1].gen == 0, "ParseWatch defaults");
		Check(es[2].host == "host-only" && es[2].port == 80, "ParseWatch host-only line");
	}

	// --- Filesystem round-trip in a scratch dir: append, load-since, trim to 48h, watch add/update.
	{
		char tmpl[] = "/tmp/shellylog_XXXXXX";
		char* dir = mkdtemp(tmpl);
		Check(dir != nullptr, "mkdtemp scratch dir");
		if (dir != nullptr) {
			setenv("CAMPIELLO_SHELLY_DIR", dir, 1);
			long now = (long)std::time(nullptr);

			ShellyPowerLog::Append("1.2.3.4", now - 3 * 3600, 10.0f); // 3h ago (inside 48h)
			ShellyPowerLog::Append("1.2.3.4", now - 60, 20.0f);       // recent
			ShellyPowerLog::Append("1.2.3.4", now, 30.0f);            // now
			std::vector<PowerSample> got = ShellyPowerLog::Load("1.2.3.4", 0);
			Check(got.size() == 3, "Append+Load round-trip 3 samples");

			// A stale sample older than 48h is dropped by Trim.
			ShellyPowerLog::Append("1.2.3.4", now - (49L * 3600), 99.0f);
			Check(ShellyPowerLog::Load("1.2.3.4", 0).size() == 4, "stale sample present before trim");
			ShellyPowerLog::Trim("1.2.3.4", now);
			std::vector<PowerSample> trimmed = ShellyPowerLog::Load("1.2.3.4", 0);
			Check(trimmed.size() == 3, "Trim drops the >48h sample");
			bool staleGone = true;
			for (const PowerSample& s : trimmed)
				if (s.w == 99.0f) staleGone = false;
			Check(staleGone, "Trim removed the stale value");

			// NaN is refused.
			Check(!ShellyPowerLog::Append("1.2.3.4", now, 0.0f / 0.0f), "Append rejects NaN");

			// Watch list: add, then update the same host (no duplicate).
			ShellyPowerLog::AddWatch({"1.2.3.4", 80, 0, "Presa"});
			ShellyPowerLog::AddWatch({"5.6.7.8", 80, 1, "EM"});
			ShellyPowerLog::AddWatch({"1.2.3.4", 80, 2, "Presa Plus"}); // update gen+name
			std::vector<WatchEntry> watch = ShellyPowerLog::LoadWatch();
			Check(watch.size() == 2, "AddWatch dedupes by host");
			bool updated = false;
			for (const WatchEntry& e : watch)
				if (e.host == "1.2.3.4") updated = (e.gen == 2 && e.name == "Presa Plus");
			Check(updated, "AddWatch updates gen/name in place");
		}
	}

	std::printf("%s: %d checks, %d failures\n", gFails == 0 ? "OK" : "FAILED", gChecks, gFails);
	return gFails == 0 ? 0 : 1;
}
