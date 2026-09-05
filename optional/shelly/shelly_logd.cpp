// shelly_logd.cpp
//
// campiello_shelly_logd: a tiny headless background logger. Once a minute it reads the watch list
// (the plugs the user has opened in the control panel), polls each one's total active power over the
// local HTTP API, and appends a sample to that device's power log, trimmed to the last 48 hours. The
// control panel then draws a real 48h history even when no window is open.
//
// Started at login by its own launch_daemon job (data/user_launch/campiello_shelly), the same
// mechanism the core daemon uses. Deliberately NOT part of campiello_daemon: the core stays device
// agnostic and MIT-clean, so the Shelly-specific polling lives here in the add-on. No libbe; it links
// only the socket-based ShellyClient and the plain-text ShellyPowerLog, so the package stays
// MIT-clean.

#include <csignal>
#include <ctime>
#include <vector>

#include <unistd.h> // sleep

#include "ShellyClient.h"
#include "ShellyPowerLog.h"

using namespace campiello::shelly;

namespace {

volatile sig_atomic_t gStop = 0;
void OnSignal(int) { gStop = 1; }

const int kPollSeconds = 60; // one sample per minute -> 2880 samples over 48h

// Poll one device and record its total active power. Returns false only on transport failure (a
// device that reports no metered channel is not an error - just nothing to log this tick).
bool RecordOne(const WatchEntry& e, long now)
{
	ShellyClient client(e.host, e.port > 0 ? e.port : 80, e.gen);
	std::vector<ShellyChannel> chans;
	if (!client.FetchChannels(chans))
		return false;

	double total = 0.0;
	bool any = false;
	for (const ShellyChannel& ch : chans) {
		if (ch.hasPower) { total += ch.power; any = true; }
	}
	if (any)
		ShellyPowerLog::Append(e.host, now, (float)total);
	ShellyPowerLog::Trim(e.host, now);
	return true;
}

} // namespace

int main()
{
	std::signal(SIGTERM, OnSignal);
	std::signal(SIGINT, OnSignal);

	while (!gStop) {
		long now = (long)std::time(nullptr);
		std::vector<WatchEntry> watch = ShellyPowerLog::LoadWatch();
		for (const WatchEntry& e : watch) {
			if (gStop)
				break;
			RecordOne(e, now);
		}

		// Sleep in one-second steps so a shutdown signal is honoured promptly.
		for (int i = 0; i < kPollSeconds && !gStop; ++i)
			sleep(1);
	}
	return 0;
}
