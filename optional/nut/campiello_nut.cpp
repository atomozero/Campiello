// campiello_nut.cpp
//
// The Campiello NUT add-on: a monitor for a UPS exposed by a Network UPS Tools server, discovered
// via _nut._tcp (TCP 3493). It reads the real UPS state - status (on line / on battery / low
// battery), battery charge, estimated runtime, load, input/battery voltage - and lists every
// variable the server publishes. It is strictly read-only: it never sends an INSTCMD or SET, so it
// cannot shut down or change the UPS. Network I/O runs on a worker thread so the UI never blocks.
//
// Launched from the WON neighborhood on a double-click of a UPS device (the nut.handler manifest),
// which passes CAMPIELLO:host/name and CAMPIELLO:port, or from the command line with host=<ip>
// [port=3493] [name=<label>]. No third-party dependency: links libbe + the network kit. End-user
// strings are Italian.
//
//   g++ -std=c++17 campiello_nut.cpp NutClient.cpp -lbe -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
#include <ScrollView.h>
#include <StringView.h>
#include <String.h>
#include <TextView.h>
#include <Window.h>

#include <fs_attr.h>

#include <cstdlib>
#include <map>
#include <string>

#include "NutClient.h"

using namespace campiello::nut;

static const char* const kSignature = "application/x-vnd.Campiello-nut";

static const uint32 kMsgRefresh   = 'nrfr';
static const uint32 kMsgInfoReady = 'ninr';

// --------------------------------------------------------------------------- worker
struct InfoJob { std::string host; int port; BMessenger reply; };
static int32 InfoThread(void* arg)
{
	InfoJob* job = static_cast<InfoJob*>(arg);
	NutClient c(job->host, job->port);

	BMessage m(kMsgInfoReady);
	auto units = c.ListUps();
	if (units.empty()) {
		m.AddBool("ok", false);
		job->reply.SendMessage(&m);
		delete job;
		return 0;
	}
	m.AddBool("ok", true);
	m.AddString("ups", units.front().first.c_str());
	m.AddString("desc", units.front().second.c_str());

	std::map<std::string, std::string> vars = c.ListVars(units.front().first);
	for (const auto& kv : vars) {
		m.AddString("k", kv.first.c_str());
		m.AddString("v", kv.second.c_str());
	}
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class NutWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	NutWindow(const std::string& host, int port, const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	void StartInfo();

	std::string  fHost;
	int          fPort;
	std::string  fName;
	BStringView* fTitle = nullptr;
	BStringView* fStatus = nullptr;
	BStringView* fCharge = nullptr;
	BStringView* fRuntime = nullptr;
	BStringView* fLoad = nullptr;
	BTextView*   fAll = nullptr;
	BStringView* fFoot = nullptr;
};

NutWindow::NutWindow(const std::string& host, int port, const std::string& name)
	: BWindow(BRect(100, 100, 480, 500), "Gruppo di continuita (UPS)", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fPort(port), fName(name)
{
	fTitle = new BStringView("t", fName.empty() ? host.c_str() : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	fTitle->SetFont(&f);

	fStatus = new BStringView("st", "Stato: interrogo il server...");
	fCharge = new BStringView("ch", "Carica batteria: -");
	fRuntime = new BStringView("rt", "Autonomia stimata: -");
	fLoad = new BStringView("ld", "Carico: -");

	fAll = new BTextView("all");
	fAll->MakeEditable(false);
	fAll->SetExplicitMinSize(BSize(340, 200));
	BScrollView* scroll = new BScrollView("scroll", fAll, 0, false, true);

	BButton* refresh = new BButton("rf", "Aggiorna", new BMessage(kMsgRefresh));
	fFoot = new BStringView("foot", host.c_str());

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fTitle)
		.Add(fStatus)
		.Add(fCharge)
		.Add(fRuntime)
		.Add(fLoad)
		.Add(new BStringView("hdr", "Tutte le variabili:"))
		.Add(scroll)
		.AddGroup(B_HORIZONTAL)
			.Add(fFoot)
			.AddGlue()
			.Add(refresh)
		.End()
	.End();

	CenterOnScreen();
	StartInfo();
}

void NutWindow::StartInfo()
{
	fFoot->SetText("Aggiorno...");
	InfoJob* job = new InfoJob{fHost, fPort, BMessenger(this)};
	thread_id t = spawn_thread(InfoThread, "nut_info", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void NutWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgRefresh:
			StartInfo();
			return;
		case kMsgInfoReady: {
			bool ok = false; msg->FindBool("ok", &ok);
			if (!ok) {
				fStatus->SetText("Stato: nessun UPS o server non raggiungibile.");
				fFoot->SetText("Non raggiungibile.");
				return;
			}
			std::map<std::string, std::string> vars;
			const char* k = nullptr;
			const char* v = nullptr;
			for (int32 i = 0; msg->FindString("k", i, &k) == B_OK
					&& msg->FindString("v", i, &v) == B_OK; ++i)
				vars[k] = v;

			auto get = [&](const char* key) -> std::string {
				auto it = vars.find(key);
				return it == vars.end() ? std::string() : it->second;
			};

			const char* ups = ""; msg->FindString("ups", &ups);
			std::string model = get("ups.model");
			if (model.empty()) model = get("device.model");
			std::string mfr = get("ups.mfr");
			if (mfr.empty()) mfr = get("device.mfr");
			BString title;
			if (!fName.empty()) title = fName.c_str();
			else if (!model.empty()) title = (mfr.empty() ? model : (mfr + " " + model)).c_str();
			else title = ups;
			if (title.Length() > 0)
				fTitle->SetText(title.String());

			BString st("Stato: ");
			std::string status = get("ups.status");
			st << (status.empty() ? "-" : StatusText(status).c_str());
			fStatus->SetText(st.String());

			BString ch("Carica batteria: ");
			std::string charge = get("battery.charge");
			ch << (charge.empty() ? "-" : (charge + " %").c_str());
			fCharge->SetText(ch.String());

			BString rt("Autonomia stimata: ");
			std::string runtime = get("battery.runtime");
			rt << (runtime.empty() ? "-" : RuntimeText(runtime).c_str());
			fRuntime->SetText(rt.String());

			BString ld("Carico: ");
			std::string load = get("ups.load");
			ld << (load.empty() ? "-" : (load + " %").c_str());
			std::string inv = get("input.voltage");
			if (!inv.empty())
				ld << "   Ingresso: " << inv.c_str() << " V";
			fLoad->SetText(ld.String());

			BString all;
			for (const auto& kv : vars)
				all << kv.first.c_str() << " = " << kv.second.c_str() << "\n";
			fAll->SetText(all.String());

			fFoot->SetText("Pronto.");
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class NutApp : public BApplication {
public:
	NutApp(const std::string& host, int port, const std::string& name)
		: BApplication(kSignature), fHost(host), fPort(port), fName(name) {}

	void RefsReceived(BMessage* msg) override
	{
		entry_ref ref;
		for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; ++i) {
			BNode node(&ref);
			if (node.InitCheck() != B_OK)
				continue;
			BString host, name, port;
			ReadAttr(node, "CAMPIELLO:host", host);
			ReadAttr(node, "CAMPIELLO:name", name);
			ReadAttr(node, "CAMPIELLO:port", port);
			if (host.Length() == 0)
				continue;
			int p = (port.Length() > 0) ? std::atoi(port.String()) : 3493;
			if (p <= 0) p = 3493;
			(new NutWindow(host.String(), p, name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Gruppo di continuita (UPS)",
				"Nessun dispositivo. Apri un UPS dal vicinato WON, o passa host=<ip>.",
				"Chiudi"))->Go();
			Quit();
			return;
		}
		(new NutWindow(fHost, fPort, fName))->Show();
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
	int fPort;
	std::string fName;
	bool fShown = false;
};

#ifndef NUT_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	int port = 3493;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "port=") == 0) port = std::atoi(a.substr(5).c_str());
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	if (port <= 0) port = 3493;
	NutApp app(host, port, name);
	app.Run();
	return 0;
}
#endif
