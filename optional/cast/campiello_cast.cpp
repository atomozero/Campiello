// campiello_cast.cpp
//
// The Campiello Google Cast add-on: a small panel for a Chromecast / Google Cast device discovered
// via _googlecast._tcp. Over DIAL (plain HTTP, port 8008) it shows the device name and which
// receiver app is running, and launches or stops apps (YouTube, Netflix). Network I/O runs on worker
// threads so the UI never blocks.
//
// Launching a specific media URL with transport controls needs the CASTv2 protobuf channel (TLS
// 8009), a documented follow-up (docs/addons/cast.md); this add-on covers launch/stop/status.
//
// Launched from the WON neighborhood on a double-click of a Cast device (the cast.handler manifest),
// which passes the device via CAMPIELLO:host/name, or from the command line with host=<ip>
// [name=<label>]. No third-party dependency (DIAL is plain HTTP): links only libbe + the network kit.
// End-user strings are Italian.
//
//   g++ -std=c++17 campiello_cast.cpp DialClient.cpp -lbe -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
#include <StringView.h>
#include <String.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>

#include "DialClient.h"

using namespace campiello::cast;

static const char* const kSignature = "application/x-vnd.Campiello-cast";

static const uint32 kMsgInfo      = 'cinf';
static const uint32 kMsgInfoReady = 'cinr';
static const uint32 kMsgLaunch    = 'clau'; // "app" = receiver app name
static const uint32 kMsgStop      = 'csto';
static const uint32 kMsgActionDone= 'cact';

// The receiver apps this panel can launch by DIAL name.
static const char* const kApps[] = {"YouTube", "Netflix"};
static const int kNumApps = 2;

// --------------------------------------------------------------------------- workers
struct InfoJob { std::string host; BMessenger reply; };
static int32 InfoThread(void* arg)
{
	InfoJob* job = static_cast<InfoJob*>(arg);
	DialClient d(job->host);
	BMessage m(kMsgInfoReady);
	m.AddString("name", d.FriendlyName().c_str());
	std::string running;
	for (int i = 0; i < kNumApps; ++i) {
		bool ok = false;
		std::string state = d.AppState(kApps[i], &ok);
		if (ok && state == "running")
			running = kApps[i];
	}
	m.AddString("running", running.c_str());
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct ActionJob { std::string host, app, action; BMessenger reply; };
static int32 ActionThread(void* arg)
{
	ActionJob* job = static_cast<ActionJob*>(arg);
	DialClient d(job->host);
	bool ok = (job->action == "launch") ? d.Launch(job->app) : d.Stop(job->app);
	BMessage m(kMsgActionDone);
	m.AddBool("ok", ok);
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class CastWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	CastWindow(const std::string& host, const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	void StartInfo();
	void RunAction(const std::string& app, const std::string& action);

	std::string  fHost;
	std::string  fName;
	std::string  fRunning; // the receiver app currently running, if known
	BStringView* fSummary = nullptr;
	BStringView* fStatus = nullptr;
};

CastWindow::CastWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(100, 100, 400, 320), "Google Cast", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fName(name)
{
	BStringView* title = new BStringView("t", fName.empty() ? "Dispositivo Cast" : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	fSummary = new BStringView("sum", "Interrogo il dispositivo...");
	fStatus = new BStringView("st", fHost.c_str());

	BMessage* yt = new BMessage(kMsgLaunch); yt->AddString("app", "YouTube");
	BMessage* nf = new BMessage(kMsgLaunch); nf->AddString("app", "Netflix");
	BButton* bYt = new BButton("yt", "Avvia YouTube", yt);
	BButton* bNf = new BButton("nf", "Avvia Netflix", nf);
	BButton* bStop = new BButton("stop", "Ferma app", new BMessage(kMsgStop));
	BButton* bRefresh = new BButton("refresh", "Aggiorna", new BMessage(kMsgInfo));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(fSummary)
		.AddGroup(B_HORIZONTAL)
			.Add(bYt)
			.Add(bNf)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(bStop)
			.AddGlue()
			.Add(bRefresh)
		.End()
		.Add(fStatus)
	.End();

	CenterOnScreen();
	StartInfo();
}

void CastWindow::StartInfo()
{
	fStatus->SetText("Aggiorno...");
	InfoJob* job = new InfoJob{fHost, BMessenger(this)};
	thread_id t = spawn_thread(InfoThread, "cast_info", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void CastWindow::RunAction(const std::string& app, const std::string& action)
{
	fStatus->SetText(action == "launch" ? "Avvio in corso..." : "Arresto in corso...");
	ActionJob* job = new ActionJob{fHost, app, action, BMessenger(this)};
	thread_id t = spawn_thread(ActionThread, "cast_act", B_NORMAL_PRIORITY, job);
	if (t < 0) delete job; else resume_thread(t);
}

void CastWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgInfo:
			StartInfo();
			return;
		case kMsgInfoReady: {
			const char* name = ""; msg->FindString("name", &name);
			const char* running = ""; msg->FindString("running", &running);
			fRunning = running;
			BString sum(name[0] ? name : fHost.c_str());
			if (fRunning.empty())
				sum << "\nNessuna app in esecuzione.";
			else
				sum << "\nIn esecuzione: " << fRunning.c_str();
			fSummary->SetText(sum.String());
			fStatus->SetText("Pronto.");
			return;
		}
		case kMsgLaunch: {
			const char* app = ""; msg->FindString("app", &app);
			fRunning = app;
			RunAction(app, "launch");
			return;
		}
		case kMsgStop:
			RunAction(fRunning.empty() ? "YouTube" : fRunning, "stop");
			return;
		case kMsgActionDone: {
			bool ok = false; msg->FindBool("ok", &ok);
			fStatus->SetText(ok ? "Fatto." : "Comando non riuscito.");
			if (ok) StartInfo(); // refresh what is running
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class CastApp : public BApplication {
public:
	CastApp(const std::string& host, const std::string& name)
		: BApplication(kSignature), fHost(host), fName(name) {}

	void RefsReceived(BMessage* msg) override
	{
		entry_ref ref;
		for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; ++i) {
			BNode node(&ref);
			if (node.InitCheck() != B_OK)
				continue;
			BString host, name;
			ReadAttr(node, "CAMPIELLO:host", host);
			ReadAttr(node, "CAMPIELLO:name", name);
			if (host.Length() == 0)
				continue;
			(new CastWindow(host.String(), name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Google Cast",
				"Nessun dispositivo. Apri un dispositivo Cast dal vicinato WON, o passa host=<ip>.",
				"Chiudi"))->Go();
			Quit();
			return;
		}
		(new CastWindow(fHost, fName))->Show();
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
	bool fShown = false;
};

#ifndef CAST_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	CastApp app(host, name);
	app.Run();
	return 0;
}
#endif
