// campiello_afp.cpp
//
// The Campiello AFP add-on: for an AFP server discovered via _afpovertcp._tcp it shows the server's
// public info (name, machine type, AFP versions, authentication methods) via the unauthenticated DSI
// GetStatus probe. It does NOT implement an AFP filesystem client (heavy; a documented follow-up in
// docs/addons/afp.md) and notes that AFP is legacy: modern clients should prefer SMB (Campiello has
// an SMB add-on).
//
// Launched from the WON neighborhood on a double-click of an AFP server (the afp.handler manifest),
// which passes the device via CAMPIELLO:host/name, or from the command line with host=<ip>
// [name=<label>]. No third-party dependency (DSI by hand): links only libbe + the network kit.
// End-user strings are Italian.
//
//   g++ -std=c++17 campiello_afp.cpp AfpProbe.cpp -lbe -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
#include <StringView.h>
#include <String.h>
#include <TextView.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>

#include "AfpProbe.h"

using namespace campiello::afp;

static const char* const kSignature = "application/x-vnd.Campiello-afp";

static const uint32 kMsgProbe      = 'aprb';
static const uint32 kMsgStatusReady = 'asta';

// --------------------------------------------------------------------------- worker
struct StatusJob { std::string host; BMessenger reply; };
static int32 StatusThread(void* arg)
{
	StatusJob* job = static_cast<StatusJob*>(arg);
	AfpProbe p(job->host);
	bool ok = false;
	Status s = p.GetStatus(&ok);
	BMessage m(kMsgStatusReady);
	m.AddBool("ok", ok);
	m.AddString("server", s.serverName.c_str());
	m.AddString("machine", s.machineType.c_str());
	for (const std::string& v : s.versions) m.AddString("ver", v.c_str());
	for (const std::string& u : s.uams) m.AddString("uam", u.c_str());
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class AfpWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	AfpWindow(const std::string& host, const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	void StartProbe();

	std::string  fHost;
	BTextView*   fInfo = nullptr;
	BStringView* fStatus = nullptr;
};

AfpWindow::AfpWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(100, 100, 460, 400), "Server AFP", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host)
{
	BStringView* title = new BStringView("t", name.empty() ? host.c_str() : name.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	fInfo = new BTextView("info");
	fInfo->MakeEditable(false);
	fInfo->SetExplicitMinSize(BSize(320, 140));

	BStringView* note = new BStringView("n",
		"AFP e' un protocollo datato (macOS moderno usa SMB).\n"
		"Per condividere file, preferisci il modulo SMB di Campiello.");
	fStatus = new BStringView("st", fHost.c_str());
	BButton* refresh = new BButton("r", "Aggiorna", new BMessage(kMsgProbe));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(fInfo)
		.Add(note)
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(refresh)
		.End()
	.End();

	CenterOnScreen();
	StartProbe();
}

void AfpWindow::StartProbe()
{
	fStatus->SetText("Interrogo il server...");
	StatusJob* job = new StatusJob{fHost, BMessenger(this)};
	thread_id t = spawn_thread(StatusThread, "afp_probe", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void AfpWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgProbe:
			StartProbe();
			return;
		case kMsgStatusReady: {
			bool ok = false; msg->FindBool("ok", &ok);
			if (!ok) {
				fInfo->SetText("");
				fStatus->SetText("Server non raggiungibile o non AFP.");
				return;
			}
			const char* server = ""; msg->FindString("server", &server);
			const char* machine = ""; msg->FindString("machine", &machine);
			BString info;
			info << "Nome server: " << server << "\n";
			info << "Tipo macchina: " << machine << "\n";
			BString vers;
			const char* v;
			for (int32 i = 0; msg->FindString("ver", i, &v) == B_OK; ++i)
				vers << (vers.Length() ? ", " : "") << v;
			info << "Versioni AFP: " << (vers.Length() ? vers : BString("-")) << "\n";
			BString uams;
			const char* u;
			for (int32 i = 0; msg->FindString("uam", i, &u) == B_OK; ++i)
				uams << (uams.Length() ? ", " : "") << u;
			info << "Autenticazione: " << (uams.Length() ? uams : BString("-")) << "\n";
			fInfo->SetText(info.String());
			fStatus->SetText("Pronto.");
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class AfpApp : public BApplication {
public:
	AfpApp(const std::string& host, const std::string& name)
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
			(new AfpWindow(host.String(), name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Server AFP",
				"Nessun server. Apri un server AFP dal vicinato WON, o passa host=<ip>.",
				"Chiudi"))->Go();
			Quit();
			return;
		}
		(new AfpWindow(fHost, fName))->Show();
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

#ifndef AFP_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	AfpApp app(host, name);
	app.Run();
	return 0;
}
#endif
