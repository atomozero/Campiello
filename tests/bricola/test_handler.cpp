// test_handler.cpp
//
// Unit test for the device add-on framework: manifest parsing and service->handler matching
// (HandlerRegistry). Pure std, so it runs as part of the offline build.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/vicinato/DeviceLaunch.h"
#include "../../src/vicinato/HandlerRegistry.h"

using namespace campiello::vicinato;

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

static NetworkService Service(const std::string& type, ServiceKind kind)
{
	NetworkService s;
	s.serviceType = type;
	s.kind = kind;
	return s;
}

int main()
{
	// A well-formed manifest parses into all its fields; comments and blank lines are ignored.
	{
		const char* manifest =
			"# Philips Hue handler\n"
			"signature = application/x-vnd.Campiello-hue\n"
			"name      = Philips Hue\n"
			"\n"
			"match.type = _hue._tcp   # the bridge\n"
			"match.type = _hap._tcp\n"
			"match.kind = home\n"
			"action.open   = Controlla le luci\n"
			"action.toggle = Accendi/spegni\n";
		DeviceHandler h;
		CHECK(ParseHandlerManifest(manifest, h));
		CHECK(h.signature == "application/x-vnd.Campiello-hue");
		CHECK(h.name == "Philips Hue");
		CHECK(h.matchTypes.size() == 2);
		CHECK(h.matchTypes[0] == "_hue._tcp");
		CHECK(h.matchTypes[1] == "_hap._tcp");
		CHECK(h.matchKinds.size() == 1 && h.matchKinds[0] == ServiceKind::Home);
		CHECK(h.actions.size() == 2);
		CHECK(h.actions[0].id == "open" && h.actions[0].label == "Controlla le luci");
		CHECK(h.actions[1].id == "toggle");
	}

	// A manifest with no signature is rejected.
	{
		DeviceHandler h;
		CHECK(!ParseHandlerManifest("name = Nope\nmatch.type = _x._tcp\n", h));
	}

	// Kind names round-trip.
	{
		ServiceKind k;
		CHECK(KindFromName("media", k) && k == ServiceKind::Media);
		CHECK(KindFromName("printer", k) && k == ServiceKind::Printer);
		CHECK(!KindFromName("bogus", k));
		CHECK(std::string(KindName(ServiceKind::Home)) == "home");
	}

	// Matching: by service type and by kind, in registration order; DefaultHandler is the first.
	{
		HandlerRegistry reg;
		DeviceHandler hue;
		hue.signature = "app.hue";
		hue.matchTypes = {"_hue._tcp"};
		reg.Add(hue);
		DeviceHandler media;
		media.signature = "app.cast";
		media.matchKinds = {ServiceKind::Media};
		reg.Add(media);
		CHECK(reg.Count() == 2);

		// A Hue bridge matches the type handler only.
		auto m1 = reg.Match(Service("_hue._tcp", ServiceKind::Home));
		CHECK(m1.size() == 1 && m1[0]->signature == "app.hue");

		// The same type as advertised by mDNS (with a trailing ".local") still matches the manifest
		// type, which has no ".local" suffix.
		auto m1local = reg.Match(Service("_hue._tcp.local", ServiceKind::Home));
		CHECK(m1local.size() == 1 && m1local[0]->signature == "app.hue");

		// A Fire TV (media kind, different type) matches the kind handler only.
		auto m2 = reg.Match(Service("_amzn-wplay._tcp", ServiceKind::Media));
		CHECK(m2.size() == 1 && m2[0]->signature == "app.cast");

		// An unmatched service has no handler.
		CHECK(reg.Match(Service("_ipp._tcp", ServiceKind::Printer)).empty());
		CHECK(reg.DefaultHandler(Service("_ipp._tcp", ServiceKind::Printer)) == nullptr);

		// The default handler is the first match.
		const DeviceHandler* def = reg.DefaultHandler(Service("_hue._tcp", ServiceKind::Home));
		CHECK(def != nullptr && def->signature == "app.hue");
	}

	// The launch protocol round-trips a device (including a name and TXT value with spaces) through
	// the argument list a handler would receive.
	{
		NetworkService svc;
		svc.host = "192.168.2.101";
		svc.port = 443;
		svc.serviceType = "_hue._tcp";
		svc.label = "Philips Hue (soggiorno)";
		svc.txt = {{"bridgeid", "001788FFFE"}, {"modelid", "BSB002 v2"}};

		std::vector<std::string> args = BuildLaunchArgs(svc, "toggle");
		std::vector<const char*> argv;
		argv.push_back("campiello_hue"); // argv[0]
		for (const std::string& a : args)
			argv.push_back(a.c_str());
		DeviceInfo d = ParseDevice(static_cast<int>(argv.size()), argv.data());

		CHECK(d.host == "192.168.2.101");
		CHECK(d.port == 443);
		CHECK(d.type == "_hue._tcp");
		CHECK(d.name == "Philips Hue (soggiorno)"); // spaces survived
		CHECK(d.action == "toggle");
		CHECK(d.txt.size() == 2);
		CHECK(d.txt[0].first == "bridgeid" && d.txt[0].second == "001788FFFE");
		CHECK(d.txt[1].second == "BSB002 v2"); // space in a TXT value survived
	}

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
