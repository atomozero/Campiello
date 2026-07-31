// test_smbmount.cpp
//
// Unit test for the SMB mount-argument parser. Pure standard C++ (no libsmb2, no Haiku), so it
// runs anywhere as part of the offline build. Covers: key=value recognition, residual FUSE args
// pass-through, the server/share requirement, and the BuildSmbMountParameters round trip.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "SmbMount.h"

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

// Build an argv from string literals for ParseSmbMount (which takes char**).
static SmbMount Parse(const std::vector<std::string>& args)
{
	std::vector<char*> argv;
	std::vector<std::string> storage = args;
	for (std::string& a : storage)
		argv.push_back(&a[0]);
	return ParseSmbMount(static_cast<int>(argv.size()), argv.data());
}

int main()
{
	// Full config, with a mount point and a FUSE option mixed in.
	{
		SmbMount m = Parse({"campiello_smb", "server=192.168.1.10", "share=Public",
			"user=alice", "password=secret", "domain=WORKGROUP", "path=docs",
			"/Public", "-o", "ro"});
		CHECK(m.ok);
		CHECK(m.error.empty());
		CHECK(m.config.server == "192.168.1.10");
		CHECK(m.config.share == "Public");
		CHECK(m.config.user == "alice");
		CHECK(m.config.password == "secret");
		CHECK(m.config.domain == "WORKGROUP");
		CHECK(m.config.basePath == "docs");
		// argv[0] plus the three unrecognized tokens pass through to FUSE.
		CHECK(m.fuseArgs.size() == 4);
		CHECK(m.fuseArgs[0] == "campiello_smb");
		CHECK(m.fuseArgs[1] == "/Public");
		CHECK(m.fuseArgs[2] == "-o");
		CHECK(m.fuseArgs[3] == "ro");
	}

	// Missing server -> not ok.
	{
		SmbMount m = Parse({"campiello_smb", "share=Public"});
		CHECK(!m.ok);
		CHECK(!m.error.empty());
	}

	// Missing share is OK: it selects the server-level ("\\server" style) mount whose root lists
	// the shares. Only the server is required.
	{
		SmbMount m = Parse({"campiello_smb", "server=host"});
		CHECK(m.ok);
		CHECK(m.config.share.empty());
	}

	// A share with no user is allowed (some shares permit anonymous/guest access).
	{
		SmbMount m = Parse({"campiello_smb", "server=host", "share=Public"});
		CHECK(m.ok);
		CHECK(m.config.user.empty());
	}

	// BuildSmbMountParameters emits the fs name first and only the set fields, and its output
	// re-parses to the same config.
	{
		SmbConfig c;
		c.server = "host";
		c.share = "Share";
		c.user = "bob";
		c.basePath = "sub";
		std::string params = BuildSmbMountParameters(c);
		CHECK(params.rfind("campiello_smb", 0) == 0); // starts with the fs name
		CHECK(params.find("server=host") != std::string::npos);
		CHECK(params.find("share=Share") != std::string::npos);
		CHECK(params.find("user=bob") != std::string::npos);
		CHECK(params.find("path=sub") != std::string::npos);
		CHECK(params.find("password=") == std::string::npos); // unset, omitted
		CHECK(params.find("domain=") == std::string::npos);   // unset, omitted
	}

	// A share name (or password) with spaces must survive userlandfs's whitespace tokenization:
	// BuildSmbMountParameters percent-encodes it, and re-tokenizing on whitespace then parsing
	// decodes it back. This is the "din esp8266 mini" case that broke the real mount.
	{
		SmbConfig c;
		c.server = "host";
		c.share = "din esp8266 mini";
		c.password = "p a%s";
		std::string params = BuildSmbMountParameters(c);
		CHECK(params.find(' ' + std::string("share=din%20esp8266%20mini")) != std::string::npos);
		CHECK(params.find("password=p%20a%25s") != std::string::npos);
		// Split on whitespace the way userlandfs does, then parse.
		std::vector<std::string> toks;
		std::string cur;
		for (char ch : params) {
			if (ch == ' ') { if (!cur.empty()) { toks.push_back(cur); cur.clear(); } }
			else cur.push_back(ch);
		}
		if (!cur.empty()) toks.push_back(cur);
		std::vector<char*> argv;
		for (std::string& t : toks) argv.push_back(&t[0]);
		SmbMount m = ParseSmbMount(static_cast<int>(argv.size()), argv.data());
		CHECK(m.ok);
		CHECK(m.config.share == "din esp8266 mini");
		CHECK(m.config.password == "p a%s");
	}

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
