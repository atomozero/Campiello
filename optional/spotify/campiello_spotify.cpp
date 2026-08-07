// campiello_spotify.cpp
//
// The Campiello Spotify add-on: for a Spotify Connect receiver discovered via _spotify-connect._tcp it
// shows the speaker's public info via the unauthenticated zeroconf getInfo endpoint. It does NOT
// control playback: that needs a Spotify Premium account and the OAuth Web API (or the closed Connect
// protocol), a documented follow-up (docs/addons/spotify.md).
//
// Launched from the WON neighborhood on a double-click of a Spotify Connect device (the
// spotify.handler manifest), which passes the device via CAMPIELLO:host/name, the SRV port as
// CAMPIELLO:port, and the mDNS TXT as CAMPIELLO:txt.<key> (CPath is the getInfo path). No third-party
// dependency: links only libbe + the network kit. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_spotify.cpp SpotifyProbe.cpp -lbe -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
#include <StringView.h>
#include <String.h>
#include <TextView.h>
#include <TypeConstants.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>

#include "SpotifyProbe.h"

using namespace campiello::spotify;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Spotify"

static const char* const kSignature = "application/x-vnd.Campiello-spotify";

static const uint32 kMsgProbe      = 'sprb';
static const uint32 kMsgInfoReady  = 'sinf';

// --------------------------------------------------------------------------- worker
struct ProbeJob { std::string host; int port; std::string cpath; BMessenger reply; };
static int32 ProbeThread(void* arg)
{
	ProbeJob* job = static_cast<ProbeJob*>(arg);
	SpotifyProbe p(job->host, job->port, job->cpath);
	bool ok = false;
	Info info = p.GetInfo(&ok);
	BMessage m(kMsgInfoReady);
	m.AddBool("ok", ok);
	m.AddString("remoteName", info.remoteName.c_str());
	m.AddString("brand", info.brandDisplayName.c_str());
	m.AddString("model", info.modelDisplayName.c_str());
	m.AddString("deviceType", info.deviceType.c_str());
	m.AddString("version", info.version.c_str());
	m.AddString("activeUser", info.activeUser.c_str());
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class SpotifyWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	SpotifyWindow(const std::string& host, int port, const std::string& cpath,
		const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	void StartProbe();

	std::string  fHost, fCPath, fName;
	int          fPort;
	BTextView*   fInfo = nullptr;
	BStringView* fStatus = nullptr;
};

SpotifyWindow::SpotifyWindow(const std::string& host, int port, const std::string& cpath,
	const std::string& name)
	: BWindow(BRect(100, 100, 460, 380), "Spotify Connect", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fCPath(cpath), fName(name), fPort(port)
{
	BStringView* title = new BStringView("t", name.empty() ? host.c_str() : name.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	fInfo = new BTextView("i");
	fInfo->MakeEditable(false);
	fInfo->SetExplicitMinSize(BSize(320, 120));

	BTextView* note = new BTextView("n");
	note->MakeEditable(false);
	note->SetExplicitMinSize(BSize(320, 70));
	note->SetText(
		B_TRANSLATE("Campiello mostra le informazioni pubbliche dell'altoparlante. Avviare e "
			"controllare la riproduzione richiede un account Spotify Premium e l'autenticazione "
			"(Web API OAuth), non ancora disponibile. Per ora usa l'app Spotify e scegli questo "
			"dispositivo con Spotify Connect."));

	fStatus = new BStringView("st", fHost.c_str());
	BButton* refresh = new BButton("r", B_TRANSLATE("Aggiorna"), new BMessage(kMsgProbe));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(fInfo)
		.Add(note)
		.AddGroup(B_HORIZONTAL).Add(fStatus).AddGlue().Add(refresh).End()
	.End();

	CenterOnScreen();
	StartProbe();
}

void SpotifyWindow::StartProbe()
{
	if (fPort == 0) {
		fInfo->SetText("");
		fStatus->SetText(B_TRANSLATE("Porta del dispositivo non disponibile."));
		return;
	}
	fStatus->SetText(B_TRANSLATE("Interrogo il dispositivo..."));
	ProbeJob* job = new ProbeJob{fHost, fPort, fCPath, BMessenger(this)};
	thread_id t = spawn_thread(ProbeThread, "spotify_probe", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void SpotifyWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgProbe:
			StartProbe();
			return;
		case kMsgInfoReady: {
			bool ok = false; msg->FindBool("ok", &ok);
			if (!ok) {
				fInfo->SetText("");
				fStatus->SetText(B_TRANSLATE("Dispositivo non raggiungibile o non Spotify Connect."));
				return;
			}
			const char* remoteName = ""; msg->FindString("remoteName", &remoteName);
			const char* brand = ""; msg->FindString("brand", &brand);
			const char* model = ""; msg->FindString("model", &model);
			const char* deviceType = ""; msg->FindString("deviceType", &deviceType);
			const char* version = ""; msg->FindString("version", &version);
			const char* activeUser = ""; msg->FindString("activeUser", &activeUser);
			BString s;
			if (remoteName[0]) s << B_TRANSLATE("Nome: ") << remoteName << "\n";
			if (brand[0] || model[0]) s << B_TRANSLATE("Dispositivo: ") << brand << " " << model << "\n";
			if (deviceType[0]) s << B_TRANSLATE("Tipo: ") << deviceType << "\n";
			if (version[0]) s << B_TRANSLATE("Versione: ") << version << "\n";
			s << B_TRANSLATE("Account attivo: ") << (activeUser[0] ? activeUser : B_TRANSLATE("(nessuno)")) << "\n";
			s << B_TRANSLATE("Indirizzo: ") << fHost.c_str() << ":" << fPort << "\n";
			fInfo->SetText(s.String());
			fStatus->SetText(B_TRANSLATE("Pronto."));
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class SpotifyApp : public BApplication {
public:
	SpotifyApp(const std::string& host, int port, const std::string& name)
		: BApplication(kSignature), fHost(host), fName(name), fPort(port) {}

	void RefsReceived(BMessage* msg) override
	{
		entry_ref ref;
		for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; ++i) {
			BNode node(&ref);
			if (node.InitCheck() != B_OK)
				continue;
			BString host, name, cpath;
			ReadAttr(node, "CAMPIELLO:host", host);
			ReadAttr(node, "CAMPIELLO:name", name);
			ReadAttr(node, "CAMPIELLO:txt.CPath", cpath);
			if (cpath.Length() == 0)
				ReadAttr(node, "CAMPIELLO:txt.cpath", cpath);
			int32 port = 0;
			node.ReadAttr("CAMPIELLO:port", B_INT32_TYPE, 0, &port, sizeof(port));
			if (host.Length() == 0)
				continue;
			std::string cp = cpath.Length() ? std::string(cpath.String()) : "/";
			(new SpotifyWindow(host.String(), port, cp, name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Spotify Connect",
				B_TRANSLATE("Nessun dispositivo. Aprilo dal vicinato WON, o passa host=<ip> port=<n>."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new SpotifyWindow(fHost, fPort, "/", fName))->Show();
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
	int fPort;
	bool fShown = false;
};

#ifndef SPOTIFY_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	int port = 0;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
		else if (a.compare(0, 5, "port=") == 0) port = std::atoi(a.c_str() + 5);
	}
	SpotifyApp app(host, port, name);
	app.Run();
	return 0;
}
#endif
