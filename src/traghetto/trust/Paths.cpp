// Paths.cpp
//
// Implementation of the per-node file locations. See Paths.h.

#include "Paths.h"

#include <cstdlib>

#include <sys/stat.h>

#ifdef __HAIKU__
#include <FindDirectory.h>
#endif

namespace campiello {
namespace net {

namespace {

// The base settings directory (before the Campiello subdir).
bool SettingsBase(std::string& out)
{
#ifdef __HAIKU__
	char buffer[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, buffer, sizeof(buffer)) != B_OK)
		return false;
	out = buffer;
	return true;
#else
	const char* home = ::getenv("HOME");
	if (home == nullptr)
		return false;
	out = std::string(home) + "/.config";
	return true;
#endif
}

// The user's home directory.
bool HomeBase(std::string& out)
{
#ifdef __HAIKU__
	char buffer[1024];
	if (find_directory(B_USER_DIRECTORY, -1, false, buffer, sizeof(buffer)) != B_OK)
		return false;
	out = buffer;
	return true;
#else
	const char* home = ::getenv("HOME");
	if (home == nullptr)
		return false;
	out = home;
	return true;
#endif
}

} // namespace

bool SettingsDir(std::string& out)
{
	std::string base;
	if (!SettingsBase(base))
		return false;
	std::string dir = base + "/Campiello";
	::mkdir(dir.c_str(), 0700); // ignore EEXIST
	out = std::move(dir);
	return true;
}

bool IdentityPath(std::string& out)
{
	std::string dir;
	if (!SettingsDir(dir))
		return false;
	out = dir + "/identity.pem";
	return true;
}

bool TrustStorePath(std::string& out)
{
	std::string dir;
	if (!SettingsDir(dir))
		return false;
	out = dir + "/trusted";
	return true;
}

bool SharedRootPath(std::string& out)
{
	std::string home;
	if (!HomeBase(home))
		return false;
	std::string desktop = home + "/Desktop";
	::mkdir(desktop.c_str(), 0755); // ignore EEXIST; Desktop normally already exists
	std::string share = desktop + "/Condivisa";
	::mkdir(share.c_str(), 0755);   // ignore EEXIST
	out = std::move(share);
	return true;
}

} // namespace net
} // namespace campiello
