// test_spotifyapi.cpp
//
// Unit tests for the pure pieces of SpotifyWebApi: the PKCE code-challenge (against the canonical
// RFC 7636 test vector), base64url, the redirect query-param parser, and the /devices JSON parser.
// The live OAuth/Web-API paths need a real Premium account and are exercised by hand (docs/addons/spotify.md).

#include "SpotifyWebApi.h"

#include <cstdio>
#include <string>

using namespace campiello::spotify;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
	if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_fail; } \
	else std::printf("ok  : %s\n", msg); \
} while (0)

int main()
{
	// RFC 7636 appendix B: verifier -> code_challenge (base64url(SHA256)).
	{
		std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
		std::string expect   = "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM";
		CHECK(SpotifyWebApi::CodeChallenge(verifier) == expect, "PKCE S256 matches RFC 7636 vector");
	}

	// base64url: no +/ or padding.
	{
		unsigned char raw[] = { 0xFB, 0xFF, 0xFE };   // std base64 "+//+", url-safe "-__-"
		std::string b = SpotifyWebApi::Base64Url(raw, sizeof(raw));
		CHECK(b == "-__-", "base64url maps +// and trims padding");
	}

	// A fresh verifier is within the PKCE length range and url-safe.
	{
		std::string v = SpotifyWebApi::RandomVerifier();
		bool okChars = true;
		for (char c : v)
			if (!(isalnum((unsigned char)c) || c == '-' || c == '_')) okChars = false;
		CHECK(v.size() >= 43 && v.size() <= 128 && okChars, "RandomVerifier length + charset");
	}

	// Redirect query parsing.
	{
		std::string line = "GET /callback?code=AQD_abc-123&state=Xy_9 HTTP/1.1";
		CHECK(SpotifyWebApi::QueryParam(line, "code") == "AQD_abc-123", "QueryParam extracts code");
		CHECK(SpotifyWebApi::QueryParam(line, "state") == "Xy_9", "QueryParam extracts state");
		CHECK(SpotifyWebApi::QueryParam(line, "error").empty(), "QueryParam absent key -> empty");
		std::string errLine = "GET /callback?error=access_denied&state=Z HTTP/1.1";
		CHECK(SpotifyWebApi::QueryParam(errLine, "error") == "access_denied", "QueryParam extracts error");
		CHECK(SpotifyWebApi::QueryParam(errLine, "code").empty(), "no code on error");
	}

	// /devices parsing: two devices, one active, with volume.
	{
		std::string json =
			"{\"devices\":[{\"id\":\"abc123\",\"is_active\":true,\"name\":\"Salotto\","
			"\"type\":\"Speaker\",\"volume_percent\":42},"
			"{\"id\":\"def456\",\"is_active\":false,\"name\":\"PC\",\"type\":\"Computer\","
			"\"volume_percent\":100}]}";
		auto ds = ParseDevices(json);
		CHECK(ds.size() == 2, "ParseDevices count");
		if (ds.size() == 2) {
			CHECK(ds[0].id == "abc123" && ds[0].name == "Salotto", "device 0 id/name");
			CHECK(ds[0].active == true && ds[0].volume == 42, "device 0 active/volume");
			CHECK(ds[0].type == "Speaker", "device 0 type");
			CHECK(ds[1].active == false && ds[1].volume == 100, "device 1 active/volume");
		}
		auto empty = ParseDevices("{\"devices\":[]}");
		CHECK(empty.empty(), "ParseDevices empty array");
	}

	// AuthorizeUrl carries the PKCE + scope params.
	{
		std::string u = SpotifyWebApi::AuthorizeUrl("CID", "CHAL", "STATE");
		CHECK(u.find("client_id=CID") != std::string::npos, "AuthorizeUrl client_id");
		CHECK(u.find("code_challenge=CHAL") != std::string::npos, "AuthorizeUrl challenge");
		CHECK(u.find("code_challenge_method=S256") != std::string::npos, "AuthorizeUrl S256");
		CHECK(u.find("response_type=code") != std::string::npos, "AuthorizeUrl code flow");
	}

	std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
	return g_fail ? 1 : 0;
}
