// test_neighborhood.cpp
//
// Unit test for NetworkDirectory: service classification (kind/auth/backend/browsable) and
// building the neighborhood from a hand-built radar snapshot. Pure standard C++, runs anywhere.

#include <cstdio>
#include <string>

#include "../../src/vicinato/NetworkDirectory.h"

using namespace campiello::vicinato;
using campiello::bricola::mdns::RadarInstance;
using campiello::bricola::mdns::RadarSnapshot;

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

int main()
{
	// Classification of the browsable file services.
	{
		NetworkService s = ClassifyServiceType("_smb._tcp.local");
		CHECK(s.kind == ServiceKind::Smb);
		CHECK(s.browsable);
		CHECK(s.auth == AuthKind::Password);
		CHECK(s.backend == BackendKind::Smb);
	}
	{
		NetworkService s = ClassifyServiceType("_campiello._tcp.local");
		CHECK(s.kind == ServiceKind::Campiello);
		CHECK(s.browsable);
		CHECK(s.auth == AuthKind::Pairing);
		CHECK(s.backend == BackendKind::Cnp);
	}
	{
		NetworkService s = ClassifyServiceType("_sftp-ssh._tcp.local");
		CHECK(s.kind == ServiceKind::Sftp);
		CHECK(s.backend == BackendKind::Sftp);
	}
	// Non-file services are not browsable and have no backend.
	{
		NetworkService s = ClassifyServiceType("_ipp._tcp.local");
		CHECK(s.kind == ServiceKind::Printer);
		CHECK(!s.browsable);
		CHECK(s.backend == BackendKind::None);
		CHECK(s.auth == AuthKind::None);
	}
	{
		NetworkService s = ClassifyServiceType("_hue._tcp.local");
		CHECK(s.kind == ServiceKind::Home);
		CHECK(!s.browsable);
	}

	// Build a neighborhood from a snapshot with a Windows share and a Fire TV media service.
	{
		RadarSnapshot snap;

		RadarInstance smb;
		smb.name = "Ufficio._smb._tcp.local";
		smb.type = "_smb._tcp.local";
		smb.host = "vepro.local";
		smb.port = 445;
		smb.addrs.push_back("192.168.2.104");
		snap.instances.push_back(smb);

		RadarInstance media;
		media.name = "FireTV._amzn-wplay._tcp.local";
		media.type = "_amzn-wplay._tcp.local";
		media.port = 43317;
		media.addrs.push_back("192.168.2.102");
		media.txt = {{"n", "FireTVStick di Andrea"}};
		snap.instances.push_back(media);

		std::vector<NetworkService> hood = BuildNeighborhood(snap);
		CHECK(hood.size() == 2);

		// Find the SMB one.
		const NetworkService* smbSvc = nullptr;
		const NetworkService* mediaSvc = nullptr;
		for (const NetworkService& s : hood) {
			if (s.kind == ServiceKind::Smb) smbSvc = &s;
			if (s.kind == ServiceKind::Media) mediaSvc = &s;
		}
		CHECK(smbSvc != nullptr);
		CHECK(mediaSvc != nullptr);
		if (smbSvc != nullptr) {
			CHECK(smbSvc->host == "192.168.2.104");
			CHECK(smbSvc->port == 445);
			CHECK(smbSvc->browsable);
			CHECK(smbSvc->backend == BackendKind::Smb);
		}
		if (mediaSvc != nullptr) {
			// The Amazon name is surfaced as the label via RadarLabels.
			CHECK(mediaSvc->label == "FireTVStick di Andrea");
			CHECK(!mediaSvc->browsable);
		}
		// Sorted by kind: Smb (kind 1) before Media (kind 5).
		CHECK(hood[0].kind == ServiceKind::Smb);
	}

	// A _workstation host classifies as a Computer (not browsable, no backend).
	{
		NetworkService s = ClassifyServiceType("_workstation._tcp.local");
		CHECK(s.kind == ServiceKind::Computer);
		CHECK(!s.browsable);
		CHECK(s.backend == BackendKind::None);
	}

	// Dedup: a machine advertising both _workstation and a real service (SMB) on the same host is
	// shown once, by its service; a lone _workstation host keeps its Computer entry.
	{
		RadarSnapshot snap;
		RadarInstance ws1; // same host as the share below -> deduped away
		ws1.name = "vepro._workstation._tcp.local";
		ws1.type = "_workstation._tcp.local";
		ws1.addrs.push_back("192.168.2.104");
		snap.instances.push_back(ws1);

		RadarInstance smb;
		smb.name = "Ufficio._smb._tcp.local";
		smb.type = "_smb._tcp.local";
		smb.addrs.push_back("192.168.2.104");
		snap.instances.push_back(smb);

		RadarInstance ws2; // a bare computer, nothing else on its host -> kept
		ws2.name = "linbox._workstation._tcp.local";
		ws2.type = "_workstation._tcp.local";
		ws2.addrs.push_back("192.168.2.150");
		snap.instances.push_back(ws2);

		std::vector<NetworkService> hood = BuildNeighborhood(snap);
		int computers = 0, smbs = 0;
		for (const NetworkService& s : hood) {
			if (s.kind == ServiceKind::Computer) ++computers;
			if (s.kind == ServiceKind::Smb) ++smbs;
		}
		CHECK(computers == 1);              // only the lone workstation survives
		CHECK(smbs == 1);
		CHECK(hood.size() == 2);
		for (const NetworkService& s : hood)
			if (s.kind == ServiceKind::Computer)
				CHECK(s.host == "192.168.2.150");
	}

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
