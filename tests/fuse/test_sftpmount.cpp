// test_sftpmount.cpp
//
// Tests the SFTP mount-argument parser: key=value options into SftpConfig, unrecognized tokens
// passed through to the FUSE arguments, environment fallbacks, and the host/user requirement.
// Pure standard C++, no framework; non-zero exit on failure.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../src/fondamenta/fuse/SftpMount.h"

using namespace campiello::fondamenta;

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                            \
	do {                                                                       \
		++gChecks;                                                             \
		if (!(cond)) {                                                         \
			++gFailures;                                                       \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
		}                                                                      \
	} while (0)

static SftpMount Parse(std::vector<const char*> args)
{
	std::vector<char*> argv;
	for (const char* a : args)
		argv.push_back(const_cast<char*>(a));
	return ParseSftpMount(static_cast<int>(argv.size()), argv.data());
}

static void ClearEnv()
{
	unsetenv("CAMPIELLO_SFTP_HOST");
	unsetenv("CAMPIELLO_SFTP_PORT");
	unsetenv("CAMPIELLO_SFTP_USER");
	unsetenv("CAMPIELLO_SFTP_PASS");
	unsetenv("CAMPIELLO_SFTP_KEY");
	unsetenv("CAMPIELLO_SFTP_PATH");
}

static void TestArgsOnly()
{
	ClearEnv();
	SftpMount m = Parse({"campiello_sftp", "host=nas.local", "user=me", "password=secret",
		"path=/srv/share", "/mnt/point"});
	CHECK(m.ok);
	CHECK(m.config.host == "nas.local");
	CHECK(m.config.user == "me");
	CHECK(m.config.password == "secret");
	CHECK(m.config.basePath == "/srv/share");
	// Unrecognized tokens (program name + mount point) are forwarded to FUSE.
	CHECK(m.fuseArgs.size() == 2);
	CHECK(m.fuseArgs[0] == "campiello_sftp");
	CHECK(m.fuseArgs[1] == "/mnt/point");
}

static void TestPortAndPassthrough()
{
	ClearEnv();
	SftpMount m = Parse({"prog", "host=h", "user=u", "port=2222", "-o", "ro", "/mnt"});
	CHECK(m.ok);
	CHECK(m.config.port == 2222);
	// -o, ro, and the mount point pass through in order.
	CHECK(m.fuseArgs.size() == 4);
	CHECK(m.fuseArgs[1] == "-o" && m.fuseArgs[2] == "ro" && m.fuseArgs[3] == "/mnt");
}

static void TestMissingUser()
{
	ClearEnv();
	SftpMount m = Parse({"prog", "host=h", "/mnt"});
	CHECK(!m.ok);
	CHECK(!m.error.empty());
}

static void TestMissingHost()
{
	ClearEnv();
	SftpMount m = Parse({"prog", "/mnt"});
	CHECK(!m.ok);
}

static void TestEnvFallback()
{
	ClearEnv();
	setenv("CAMPIELLO_SFTP_HOST", "envhost", 1);
	setenv("CAMPIELLO_SFTP_USER", "envuser", 1);
	setenv("CAMPIELLO_SFTP_PORT", "2200", 1);
	SftpMount m = Parse({"prog", "/mnt"});
	CHECK(m.ok);
	CHECK(m.config.host == "envhost");
	CHECK(m.config.user == "envuser");
	CHECK(m.config.port == 2200);
	CHECK(m.fuseArgs.size() == 2 && m.fuseArgs[1] == "/mnt");

	// A command-line value overrides the environment.
	SftpMount m2 = Parse({"prog", "host=arghost", "/mnt"});
	CHECK(m2.config.host == "arghost");   // arg wins
	CHECK(m2.config.user == "envuser");   // env fills the rest
	ClearEnv();
}

// BuildMountParameters (the helper's output) must parse back to the same config.
static void TestBuildRoundTrip()
{
	ClearEnv();
	SftpConfig c;
	c.host = "nas.local";
	c.port = 2222;
	c.user = "me";
	c.password = "secret";
	c.basePath = "/srv/share";

	std::string params = BuildMountParameters(c);
	// Split on spaces the way userlandfs hands tokens to the add-on.
	std::vector<std::string> toks;
	size_t start = 0;
	while (start <= params.size()) {
		size_t sp = params.find(' ', start);
		if (sp == std::string::npos) { toks.push_back(params.substr(start)); break; }
		toks.push_back(params.substr(start, sp - start));
		start = sp + 1;
	}
	CHECK(!toks.empty() && toks[0] == "campiello_sftp");

	std::vector<char*> argv;
	for (std::string& t : toks)
		argv.push_back(&t[0]);
	SftpMount m = ParseSftpMount(static_cast<int>(argv.size()), argv.data());
	CHECK(m.ok);
	CHECK(m.config.host == "nas.local");
	CHECK(m.config.port == 2222);
	CHECK(m.config.user == "me");
	CHECK(m.config.password == "secret");
	CHECK(m.config.basePath == "/srv/share");
}

int main()
{
	TestArgsOnly();
	TestPortAndPassthrough();
	TestMissingUser();
	TestMissingHost();
	TestEnvFallback();
	TestBuildRoundTrip();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
