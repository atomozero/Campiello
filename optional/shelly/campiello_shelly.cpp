// campiello_shelly.cpp
//
// The Campiello Shelly add-on: a control panel for a Shelly smart relay/plug discovered on the
// network, over its LOCAL HTTP API (no cloud), for both device generations (gen1 REST and gen2 RPC,
// auto-detected). It lists the device's channels with an on/off button and a live power readout, and
// opens the device web UI. Network I/O runs on worker threads so the UI never blocks.
//
// Launched from the WON neighborhood on a double-click of a Shelly (the shelly.handler manifest),
// which passes CAMPIELLO:host/name/port and the mDNS TXT as CAMPIELLO:txt.<key>; also runnable from
// the command line with host=<ip> [name=<label>] [port=80] [gen=2]. OPTIONAL, Haiku: links only the
// network kit (no third-party dependency), so it stays MIT-clean. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_shelly.cpp ShellyClient.cpp -lbe -lnetwork -llocalestub

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
#include <Roster.h>
#include <StringView.h>
#include <String.h>
#include <Window.h>

#include <fs_attr.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ShellyClient.h"

using namespace campiello::shelly;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Shelly"

static const char* const kSignature = "application/x-vnd.Campiello-shelly";

static const uint32 kMsgRefresh   = 'sref';
static const uint32 kMsgReady     = 'srdy';
static const uint32 kMsgToggle    = 'stog';
static const uint32 kMsgCmdDone   = 'scmd';
static const uint32 kMsgOpenWeb   = 'sweb';

// --------------------------------------------------------------------------- workers
struct RefreshJob { std::string host; int port; int gen; BMessenger reply; };
static int32 RefreshThread(void* arg)
{
	RefreshJob* job = static_cast<RefreshJob*>(arg);
	ShellyClient c(job->host, job->port, job->gen);
	ShellyInfo info;
	bool ok = c.FetchInfo(info);
	std::vector<ShellyChannel> chans;
	if (ok)
		ok = c.FetchChannels(chans);

	BMessage m(kMsgReady);
	m.AddBool("ok", ok);
	m.AddInt32("gen", info.gen);
	m.AddString("model", info.model.c_str());
	m.AddString("app", info.app.c_str());
	m.AddString("fw", info.fw.c_str());
	m.AddBool("auth", info.authEnabled);
	for (const ShellyChannel& ch : chans) {
		m.AddInt32("ch.id", ch.id);
		m.AddString("ch.name", ch.name.c_str());
		m.AddBool("ch.ctl", ch.controllable);
		m.AddBool("ch.on", ch.on);
		m.AddBool("ch.hp", ch.hasPower);
		m.AddDouble("ch.pw", ch.power);
		m.AddDouble("ch.v", ch.voltage);
		m.AddDouble("ch.wh", ch.total);
	}
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct SetJob { std::string host; int port; int gen; int id; bool on; BMessenger reply; };
static int32 SetThread(void* arg)
{
	SetJob* job = static_cast<SetJob*>(arg);
	ShellyClient c(job->host, job->port, job->gen);
	bool ok = c.SetOn(job->id, job->on);
	BMessage m(kMsgCmdDone);
	m.AddBool("ok", ok);
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class ShellyWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	ShellyWindow(const std::string& host, const std::string& name, int port, int gen);
	void MessageReceived(BMessage* msg) override;

private:
	void Build(BMessage* ready); // ready == nullptr -> "loading"
	void StartRefresh();

	std::string  fHost;
	std::string  fName;
	int          fPort;
	int          fGen;
	BStringView* fStatus = nullptr;
};

ShellyWindow::ShellyWindow(const std::string& host, const std::string& name, int port, int gen)
	: BWindow(BRect(100, 100, 460, 400), "Shelly",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fName(name), fPort(port <= 0 ? 80 : port), fGen(gen)
{
	Build(nullptr);
	StartRefresh();
	CenterOnScreen();
}

void ShellyWindow::StartRefresh()
{
	RefreshJob* job = new RefreshJob{fHost, fPort, fGen, BMessenger(this)};
	thread_id t = spawn_thread(RefreshThread, "shelly_refresh", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void ShellyWindow::Build(BMessage* ready)
{
	while (BView* c = ChildAt(0)) { RemoveChild(c); delete c; }

	BStringView* title = new BStringView("t",
		fName.empty() ? B_TRANSLATE("Shelly") : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	BButton* refresh = new BButton("refresh", B_TRANSLATE("Aggiorna"), new BMessage(kMsgRefresh));
	BButton* web = new BButton("web", B_TRANSLATE("Apri web"), new BMessage(kMsgOpenWeb));

	const char* statusText = B_TRANSLATE("Carico lo stato...");
	std::string detail;
	bool ok = false;
	if (ready != nullptr) {
		ready->FindBool("ok", &ok);
		if (!ok) {
			statusText = B_TRANSLATE("Dispositivo irraggiungibile.");
		} else {
			int32 gen = 1; ready->FindInt32("gen", &gen);
			const char* model = ""; ready->FindString("model", &model);
			const char* fw = ""; ready->FindString("fw", &fw);
			char buf[256];
			std::snprintf(buf, sizeof(buf), "gen%d  %s  %s  %s",
				(int)gen, model, fw, fHost.c_str());
			detail = buf;
			statusText = detail.c_str();
		}
	}
	fStatus = new BStringView("st", statusText);

	BLayoutBuilder::Group<> root = BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(title)
			.AddGlue()
			.Add(web)
			.Add(refresh)
		.End()
		.Add(fStatus);

	if (ready != nullptr && ok) {
		bool auth = false; ready->FindBool("auth", &auth);
		int32 id;
		int count = 0;
		for (int32 i = 0; ready->FindInt32("ch.id", i, &id) == B_OK; ++i) {
			const char* nm = ""; ready->FindString("ch.name", i, &nm);
			bool ctl = false; ready->FindBool("ch.ctl", i, &ctl);
			bool on = false; ready->FindBool("ch.on", i, &on);
			bool hp = false; ready->FindBool("ch.hp", i, &hp);
			double pw = 0; ready->FindDouble("ch.pw", i, &pw);
			double v = 0; ready->FindDouble("ch.v", i, &v);
			double wh = 0; ready->FindDouble("ch.wh", i, &wh);

			BStringView* label = new BStringView("", nm);

			std::string readout;
			if (hp) {
				char b[128];
				std::snprintf(b, sizeof(b), "%.1f W", pw);
				readout = b;
				if (v > 0) { std::snprintf(b, sizeof(b), "  %.0f V", v); readout += b; }
				if (wh > 0) {
					std::snprintf(b, sizeof(b), "  %.2f kWh", wh / 1000.0);
					readout += b;
				}
			}
			BStringView* meter = new BStringView("", readout.c_str());

			auto row = root.AddGroup(B_HORIZONTAL);
			row.Add(label);
			row.AddGlue();
			row.Add(meter);
			if (ctl) {
				BMessage* tg = new BMessage(kMsgToggle);
				tg->AddInt32("id", id);
				tg->AddBool("on", !on); // pressing flips it
				BButton* btn = new BButton("", on ? B_TRANSLATE("Acceso") : B_TRANSLATE("Spento"), tg);
				row.Add(btn);
			}
			row.End();
			++count;
		}
		if (count == 0)
			fStatus->SetText(B_TRANSLATE("Nessun canale rilevato."));
		if (auth)
			root.Add(new BStringView("auth",
				B_TRANSLATE("Autenticazione attiva sul dispositivo: il controllo potrebbe fallire.")));
	}
	root.AddGlue().End();
}

void ShellyWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgRefresh:
			if (fStatus != nullptr) fStatus->SetText(B_TRANSLATE("Aggiorno..."));
			StartRefresh();
			return;
		case kMsgReady: {
			int32 gen = 0;
			if (msg->FindInt32("gen", &gen) == B_OK && gen >= 1)
				fGen = gen; // cache for the control calls, saves a probe
			Build(msg);
			return;
		}
		case kMsgToggle: {
			int32 id = 0; msg->FindInt32("id", &id);
			bool on = false; msg->FindBool("on", &on);
			if (fStatus != nullptr) fStatus->SetText(B_TRANSLATE("Invio comando..."));
			SetJob* job = new SetJob{fHost, fPort, fGen, (int)id, on, BMessenger(this)};
			thread_id t = spawn_thread(SetThread, "shelly_set", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgCmdDone: {
			bool ok = false; msg->FindBool("ok", &ok);
			if (!ok && fStatus != nullptr)
				fStatus->SetText(B_TRANSLATE("Comando non riuscito."));
			else
				StartRefresh(); // reflect the new state
			return;
		}
		case kMsgOpenWeb: {
			std::string url = "http://" + fHost + "/";
			char* argv[1] = { const_cast<char*>(url.c_str()) };
			if (be_roster->Launch("application/x-vnd.Be.URL.http", 1, argv) != B_OK)
				be_roster->Launch("text/html", 1, argv);
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class ShellyApp : public BApplication {
public:
	ShellyApp(const std::string& host, const std::string& name, int port, int gen)
		: BApplication(kSignature), fHost(host), fName(name), fPort(port), fGen(gen) {}

	void RefsReceived(BMessage* msg) override
	{
		entry_ref ref;
		for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; ++i) {
			BNode node(&ref);
			if (node.InitCheck() != B_OK)
				continue;
			BString host, name, port, gen;
			ReadAttr(node, "CAMPIELLO:host", host);
			ReadAttr(node, "CAMPIELLO:name", name);
			ReadAttr(node, "CAMPIELLO:port", port);
			ReadAttr(node, "CAMPIELLO:txt.gen", gen);
			if (host.Length() == 0)
				continue;
			int p = (port.Length() > 0) ? std::atoi(port.String()) : 80;
			int g = (gen.Length() > 0) ? std::atoi(gen.String()) : 0;
			(new ShellyWindow(host.String(), name.String(), p, g))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Shelly",
				B_TRANSLATE("Nessun dispositivo. Apri uno Shelly dal vicinato WON, o passa host=<ip>."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new ShellyWindow(fHost, fName, fPort, fGen))->Show();
		fShown = true;
	}

private:
	static void ReadAttr(BNode& node, const char* attr, BString& out)
	{
		attr_info info;
		if (node.GetAttrInfo(attr, &info) != B_OK || info.size <= 0)
			return;
		char* buf = out.LockBuffer(info.size + 1);
		ssize_t n = node.ReadAttr(attr, B_STRING_TYPE, 0, buf, info.size);
		buf[(n > 0) ? n : 0] = '\0';
		out.UnlockBuffer();
	}

	std::string fHost;
	std::string fName;
	int         fPort;
	int         fGen;
	bool        fShown = false;
};

int main(int argc, char** argv)
{
	std::string host, name;
	int port = 80, gen = 0;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
		else if (a.compare(0, 5, "port=") == 0) port = std::atoi(a.c_str() + 5);
		else if (a.compare(0, 4, "gen=") == 0) gen = std::atoi(a.c_str() + 4);
	}
	ShellyApp app(host, name, port, gen);
	app.Run();
	return 0;
}
