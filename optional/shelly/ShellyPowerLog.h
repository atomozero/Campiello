// ShellyPowerLog.h
//
// Persistent, dependency-free power history for Shelly devices. The background logger
// (campiello_shelly_logd) appends one total-active-power sample per minute per watched device; the
// control panel reads the last 48 hours to draw a history graph. Two small on-disk artifacts, both
// plain text under the Campiello settings directory:
//
//   <base>/watch.list           one "host\tport\tgen\tname" line per device to record (the panel
//                               registers a device here when the user opens it)
//   <base>/<host>.log           append-only "epoch_seconds watts" lines, trimmed to the last 48h
//
// <base> is $CAMPIELLO_SHELLY_DIR when set (used by the tests), else $HOME/config/settings/Campiello
// /shelly. No libbe and no third-party library, so it is unit-testable off Haiku and stays MIT-clean.

#ifndef CAMPIELLO_SHELLY_SHELLYPOWERLOG_H
#define CAMPIELLO_SHELLY_SHELLYPOWERLOG_H

#include <string>
#include <vector>

namespace campiello {
namespace shelly {

// One recorded reading: epoch seconds and total active power in watts.
struct PowerSample {
	long  t = 0;
	float w = 0.0f;
};

// A device the logger should poll.
struct WatchEntry {
	std::string host;
	int         port = 80;
	int         gen = 0;   // 0 = auto-detect
	std::string name;
};

class ShellyPowerLog {
public:
	static const long kWindowSeconds = 48 * 3600; // 48 hours

	// Directory holding watch.list and the per-host logs. Created on demand by the writers.
	static std::string BaseDir();
	static std::string LogPathForHost(const std::string& host);
	static std::string WatchPath();

	// Append one sample (best-effort, creates the directory). Non-finite watts are dropped.
	static bool Append(const std::string& host, long epoch, float watts);
	// Load samples with t >= since (since <= 0 means all), in stored (chronological) order.
	static std::vector<PowerSample> Load(const std::string& host, long since = 0);
	// Rewrite the log keeping only samples newer than now - kWindowSeconds.
	static void Trim(const std::string& host, long now);

	// Watch list: read all entries; add one keyed by host (updates port/gen/name if already present).
	static std::vector<WatchEntry> LoadWatch();
	static void AddWatch(const WatchEntry& entry);

	// Pure helpers, testable without a filesystem.
	static std::vector<PowerSample> ParseLog(const std::string& text, long since = 0);
	static std::vector<WatchEntry>  ParseWatch(const std::string& text);
	static std::string              SanitizeHost(const std::string& host);
};

} // namespace shelly
} // namespace campiello

#endif // CAMPIELLO_SHELLY_SHELLYPOWERLOG_H
