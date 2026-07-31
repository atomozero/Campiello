// campiello_radar.cpp
//
// The Campiello network radar: a debug window that listens for mDNS/DNS-SD traffic and shows,
// live, every host transmitting and every service advertised on the LAN, translated into
// human-readable terms so you can tell WHAT was detected (a Hue bridge, a Matter dimmable light,
// a printer...). It exists to make the multicast-delivery question visible (docs/RADAR.md,
// docs/VERIFIED.md): switch the interface menu and watch where traffic appears, or does not. Not
// a product surface.
//
// The service catalog and TXT decoding live in the portable RadarLabels module (unit-tested);
// this file is the Haiku GUI (links libbe), driving the MdnsRadar engine and polling its
// snapshot on a one-second timer. End-user strings are Italian (working agreement rule 4).

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <FindDirectory.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <OutlineListView.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <View.h>
#include <Window.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../bricola/mdns/MdnsSocket.h"
#include "../bricola/mdns/MdnsRadar.h"
#include "../bricola/mdns/RadarLabels.h"

using namespace campiello::bricola::mdns;

static const char* const kSignature = "application/x-vnd.Campiello-radar";

static const uint32 kMsgTick     = 'tick';
static const uint32 kMsgIface    = 'ifac';
static const uint32 kMsgQueryNow = 'qnow';
static const uint32 kMsgExport   = 'expt';

namespace {

// Seconds since a monotonic-ms timestamp, floored at 0.
int64_t AgoSeconds(int64_t nowMs, int64_t thenMs)
{
	int64_t d = (nowMs - thenMs) / 1000;
	return d < 0 ? 0 : d;
}

// Strip the trailing ".<serviceType>" from an instance FQDN for a shorter label.
std::string FriendlyInstance(const std::string& name, const std::string& type)
{
	std::string suffix = "." + type;
	if (!type.empty() && name.size() > suffix.size()
		&& name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
		return name.substr(0, name.size() - suffix.size());
	}
	return name;
}

std::string Plural(size_t n, const char* one, const char* many)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%zu %s", n, n == 1 ? one : many);
	return buf;
}

// A stable identity carried by the collapsible items, so their expand/collapse state can be
// preserved across list rebuilds (the counts in the rows change every second; the tree structure
// usually does not). A cross-cast target, hence polymorphic.
class KeyedItem {
public:
	virtual ~KeyedItem() {}
	std::string key;
};

// A list item drawn bold, for the section headers.
class BoldStringItem : public BStringItem, public KeyedItem {
public:
	explicit BoldStringItem(const char* text) : BStringItem(text) {}

	void Update(BView* owner, const BFont*) override
	{
		BFont bold(be_bold_font);
		BStringItem::Update(owner, &bold);
	}

	void DrawItem(BView* owner, BRect frame, bool complete) override
	{
		BFont previous;
		owner->GetFont(&previous);
		owner->SetFont(be_bold_font);
		BStringItem::DrawItem(owner, frame, complete);
		owner->SetFont(&previous);
	}
};

// A plain list item that carries a key (for services and instances).
class KeyedStringItem : public BStringItem, public KeyedItem {
public:
	explicit KeyedStringItem(const char* text) : BStringItem(text) {}
};

} // namespace

class RadarWindow : public BWindow {
public:
	RadarWindow();
	~RadarWindow() override;
	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void Refresh();
	void SwitchInterface(const char* ipv4); // null/empty = auto
	void ExportLog();
	BPopUpMenu* BuildInterfaceMenu();
	void CaptureExpansion();  // read current expand/collapse state into fExpandState
	void ApplyExpansion();    // write fExpandState back onto the rebuilt items

	MdnsRadar         fRadar;
	BOutlineListView* fList;
	BStringView*      fStats;
	BMessageRunner*   fRunner = nullptr;

	// Preserved across rebuilds so a manual collapse is not undone by the next refresh.
	std::map<std::string, bool> fExpandState; // item key -> expanded
	std::string                 fLastSignature; // structure last rendered
};

RadarWindow::RadarWindow()
	:
	BWindow(BRect(100, 100, 660, 640), "Campiello Radar",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	BStringView* title = new BStringView("title", "Radar di rete (mDNS/DNS-SD)");
	BFont titleFont(be_bold_font);
	titleFont.SetSize(titleFont.Size() * 1.4f);
	title->SetFont(&titleFont);

	fStats = new BStringView("stats", "Avvio...");

	BMenuField* iface = new BMenuField("iface", "Interfaccia:", BuildInterfaceMenu());
	BButton* query = new BButton("query", "Interroga ora", new BMessage(kMsgQueryNow));
	BButton* exportButton = new BButton("export", "Esporta log", new BMessage(kMsgExport));

	fList = new BOutlineListView("list", B_SINGLE_SELECTION_LIST);
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(fStats)
		.AddGroup(B_HORIZONTAL)
			.Add(iface)
			.AddGlue()
			.Add(exportButton)
			.Add(query)
		.End()
		.Add(scroll);

	CenterOnScreen();

	// Start the radar (auto interface) and poll it once a second.
	if (!fRadar.Start(nullptr)) {
		fStats->SetText("Impossibile aprire il socket multicast.");
	} else {
		BMessage tick(kMsgTick);
		fRunner = new BMessageRunner(BMessenger(this), &tick, 1000000LL);
		Refresh();
	}
}

RadarWindow::~RadarWindow()
{
	delete fRunner;
}

BPopUpMenu* RadarWindow::BuildInterfaceMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("Automatica");

	BMessage* autoMsg = new BMessage(kMsgIface);
	autoMsg->SetString("iface", ""); // empty = auto-resolve
	BMenuItem* autoItem = new BMenuItem("Automatica", autoMsg);
	autoItem->SetMarked(true);
	menu->AddItem(autoItem);

	// Every local IPv4, loopback included (127.0.0.1 is the same-host debug case).
	std::vector<std::string> addrs = LocalIPv4Addresses(true);
	for (const std::string& a : addrs) {
		BMessage* m = new BMessage(kMsgIface);
		m->SetString("iface", a.c_str());
		menu->AddItem(new BMenuItem(a.c_str(), m));
	}
	return menu;
}

void RadarWindow::SwitchInterface(const char* ipv4)
{
	fRadar.Stop();
	if (!fRadar.Start((ipv4 != nullptr && ipv4[0] != '\0') ? ipv4 : nullptr))
		fStats->SetText("Impossibile aprire il socket su questa interfaccia.");
	else
		Refresh();
}

void RadarWindow::ExportLog()
{
	std::string report = BuildRadarReport(fRadar.Snapshot());

	char home[1024];
	std::string path;
	if (find_directory(B_USER_DIRECTORY, -1, false, home, sizeof(home)) == B_OK)
		path = std::string(home) + "/campiello_radar.txt";
	else
		path = "campiello_radar.txt";

	FILE* f = std::fopen(path.c_str(), "w");
	if (f == nullptr) {
		BAlert* alert = new BAlert("Campiello Radar",
			"Impossibile scrivere il file di log.", "OK");
		alert->Go();
		return;
	}
	std::fwrite(report.data(), 1, report.size(), f);
	std::fclose(f);

	std::string msg = "Log salvato in:\n" + path;
	BAlert* alert = new BAlert("Campiello Radar", msg.c_str(), "OK");
	alert->Go();
}

void RadarWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTick:
			Refresh();
			return;
		case kMsgIface: {
			const char* iface = nullptr;
			message->FindString("iface", &iface);
			SwitchInterface(iface);
			return;
		}
		case kMsgQueryNow:
			fRadar.QueryNow();
			return;
		case kMsgExport:
			ExportLog();
			return;
	}
	BWindow::MessageReceived(message);
}

bool RadarWindow::QuitRequested()
{
	delete fRunner;
	fRunner = nullptr;
	fRadar.Stop();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

void RadarWindow::Refresh()
{
	RadarSnapshot snap = fRadar.Snapshot();

	// Correlate each host IP with the devices it advertises, so a host row can say what it IS.
	std::map<std::string, std::set<std::string>> hostDevices; // ip -> service labels
	for (const RadarInstance& inst : snap.instances) {
		ServiceInfo si = LookupService(inst.type);
		for (const std::string& addr : inst.addrs)
			hostDevices[addr].insert(si.label);
	}

	// Header: interface and totals, with a loud line when nothing is arriving.
	char stats[256];
	std::string iface = snap.interfaceIp.empty() ? "automatica" : snap.interfaceIp;
	if (snap.totalPackets == 0) {
		std::snprintf(stats, sizeof(stats),
			"Interfaccia %s: nessun traffico multicast ricevuto.", iface.c_str());
	} else {
		std::snprintf(stats, sizeof(stats),
			"Interfaccia %s - %llu pacchetti da %zu host, %llu record (%llu scartati)",
			iface.c_str(), (unsigned long long)snap.totalPackets, snap.sources.size(),
			(unsigned long long)snap.totalRecords, (unsigned long long)snap.droppedPackets);
	}
	fStats->SetText(stats);

	// Rebuild the tree ONLY when its structure changes. The row texts (packet counts, "visto Ns
	// fa") tick every second, but rebuilding every second would blow away the user's manual
	// collapses and the scroll position. So we compute a signature of the structure (which hosts,
	// services, instances, subtypes exist) and skip the rebuild when it is unchanged.
	std::string sig;
	for (const RadarSource& src : snap.sources)
		sig += "H|" + src.ip + ";";
	for (const RadarService& svc : snap.services) {
		sig += "S|" + svc.type + "|" + std::to_string(svc.instances);
		for (const std::string& s : svc.subtypes)
			sig += "/" + s;
		sig += ";";
	}
	for (const RadarInstance& inst : snap.instances) {
		std::string addr = inst.addrs.empty() ? inst.host : inst.addrs[0];
		sig += "I|" + inst.name + "|" + inst.type + "|" + std::to_string(inst.port) + "|" + addr
			+ ";";
	}
	if (sig == fLastSignature && fList->FullListCountItems() > 0)
		return; // structure unchanged: leave the tree (and its expand state) alone

	CaptureExpansion();
	fList->MakeEmpty();

	char line[640];

	// Section 1: hosts that are transmitting, each annotated with what it appears to be.
	std::snprintf(line, sizeof(line), "Host che trasmettono (%zu)", snap.sources.size());
	BoldStringItem* hostsHeader = new BoldStringItem(line);
	hostsHeader->key = "sec:hosts";
	fList->AddItem(hostsHeader);
	for (const RadarSource& src : snap.sources) {
		std::string devices;
		auto it = hostDevices.find(src.ip);
		if (it != hostDevices.end()) {
			for (const std::string& d : it->second) {
				if (!devices.empty())
					devices += ", ";
				devices += d;
			}
		}
		if (devices.empty()) {
			std::snprintf(line, sizeof(line), "%s - %llu pacchetti, visto %llds fa",
				src.ip.c_str(), (unsigned long long)src.packets,
				(long long)AgoSeconds(snap.nowMs, src.lastMs));
		} else {
			std::snprintf(line, sizeof(line), "%s - %s - %llu pacchetti, visto %llds fa",
				src.ip.c_str(), devices.c_str(), (unsigned long long)src.packets,
				(long long)AgoSeconds(snap.nowMs, src.lastMs));
		}
		BStringItem* item = new BStringItem(line);
		item->SetOutlineLevel(1);
		fList->AddItem(item);
	}

	// Section 2: services, each expanding to its instances, each of those to its TXT attributes.
	std::snprintf(line, sizeof(line), "Servizi (%zu)", snap.services.size());
	BoldStringItem* servicesHeader = new BoldStringItem(line);
	servicesHeader->key = "sec:services";
	fList->AddItem(servicesHeader);
	for (const RadarService& svc : snap.services) {
		ServiceInfo si = LookupService(svc.type);
		std::string header = (si.category == "Altro" || si.category == si.label)
			? si.label : (si.category + " - " + si.label);
		std::snprintf(line, sizeof(line), "%s   [%s]   (%s)", header.c_str(), svc.type.c_str(),
			Plural(svc.instances, "istanza", "istanze").c_str());
		KeyedStringItem* svcItem = new KeyedStringItem(line);
		svcItem->key = "svc:" + svc.type;
		svcItem->SetOutlineLevel(1);
		fList->AddItem(svcItem);

		if (!svc.subtypes.empty()) {
			std::string subs;
			for (const std::string& s : svc.subtypes) {
				if (!subs.empty())
					subs += ", ";
				subs += s;
			}
			std::snprintf(line, sizeof(line), "sottotipi: %s", subs.c_str());
			BStringItem* subItem = new BStringItem(line);
			subItem->SetOutlineLevel(2);
			fList->AddItem(subItem);
		}

		for (const RadarInstance& inst : snap.instances) {
			if (inst.type != svc.type)
				continue;
			std::string label = FriendlyInstance(inst.name, inst.type);
			std::string addr = inst.addrs.empty() ? inst.host : inst.addrs[0];
			std::string summary = InstanceSummary(inst.type, inst.txt);
			if (summary.empty())
				std::snprintf(line, sizeof(line), "%s   @ %s:%u", label.c_str(), addr.c_str(),
					(unsigned)inst.port);
			else
				std::snprintf(line, sizeof(line), "%s   @ %s:%u   -   %s", label.c_str(),
					addr.c_str(), (unsigned)inst.port, summary.c_str());
			KeyedStringItem* instItem = new KeyedStringItem(line);
			instItem->key = "inst:" + inst.name;
			instItem->SetOutlineLevel(2);
			fList->AddItem(instItem);

			// TXT attributes as children, decoded and collapsed by default so the tree stays
			// scannable but the specs are one click away.
			bool hasTxt = false;
			for (const auto& kv : inst.txt) {
				std::string key = TxtKeyLabel(inst.type, kv.first);
				std::string value = DecodeTxtValue(inst.type, kv.first, kv.second);
				if (value.empty())
					std::snprintf(line, sizeof(line), "%s", key.c_str());
				else
					std::snprintf(line, sizeof(line), "%s = %s", key.c_str(), value.c_str());
				BStringItem* txtItem = new BStringItem(line);
				txtItem->SetOutlineLevel(3);
				fList->AddItem(txtItem);
				hasTxt = true;
			}
			if (hasTxt)
				fList->Collapse(instItem); // TXT collapsed by default
		}
	}

	// Restore any expand/collapse the user had set before this rebuild, then remember the new
	// structure.
	ApplyExpansion();
	fLastSignature = sig;
}

void RadarWindow::CaptureExpansion()
{
	int32 n = fList->FullListCountItems();
	for (int32 i = 0; i < n; ++i) {
		BListItem* item = fList->FullListItemAt(i);
		KeyedItem* keyed = dynamic_cast<KeyedItem*>(item);
		if (keyed != nullptr && !keyed->key.empty())
			fExpandState[keyed->key] = item->IsExpanded();
	}
}

void RadarWindow::ApplyExpansion()
{
	int32 n = fList->FullListCountItems();
	for (int32 i = 0; i < n; ++i) {
		BListItem* item = fList->FullListItemAt(i);
		KeyedItem* keyed = dynamic_cast<KeyedItem*>(item);
		if (keyed == nullptr || keyed->key.empty())
			continue;
		auto it = fExpandState.find(keyed->key);
		if (it != fExpandState.end()) {
			if (it->second)
				fList->Expand(item);
			else
				fList->Collapse(item);
		}
	}
}

int main()
{
	BApplication app(kSignature);
	RadarWindow* window = new RadarWindow();
	window->Show();
	app.Run();
	return 0;
}
