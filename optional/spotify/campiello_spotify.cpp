// campiello_spotify.cpp
//
// The Campiello Spotify add-on. For a Spotify Connect receiver discovered via _spotify-connect._tcp it
// shows the speaker's public info (the unauthenticated zeroconf getInfo, SpotifyProbe) AND, once the
// user links a Spotify account, actively controls playback through the Spotify Web API
// (SpotifyWebApi): transfer playback to this speaker, play/pause/next/previous and volume.
//
// The active control needs a Spotify Premium account and a (free) Spotify developer app; the user
// pastes its client_id once and authorizes in a browser (OAuth Authorization Code + PKCE, loopback
// redirect). See docs/addons/spotify.md. Info-only still works with no account.
//
// Launched from the WON neighborhood on a double-click of a Spotify Connect device (the
// spotify.handler manifest) with CAMPIELLO:host/name, the SRV port as CAMPIELLO:port and the mDNS TXT
// as CAMPIELLO:txt.<key> (CPath is the getInfo path). End-user strings are Italian (Locale Kit).
//
//   g++ -std=c++17 campiello_spotify.cpp SpotifyProbe.cpp SpotifyWebApi.cpp -lbe -lcurl -lcrypto -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
#include <Slider.h>
#include <StringView.h>
#include <String.h>
#include <TextControl.h>
#include <TextView.h>
#include <TypeConstants.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>
#include <vector>

#include "SpotifyProbe.h"
#include "SpotifyWebApi.h"

using namespace campiello::spotify;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE. Source strings are Italian (the
// default when no catalog matches), catalogs under data/locale/catalogs/<signature>/ translate them.
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Spotify"

static const char* const kSignature = "application/x-vnd.Campiello-spotify";

static const uint32 kMsgProbe        = 'sprb';
static const uint32 kMsgInfoReady    = 'sinf';
static const uint32 kMsgConnect      = 'cnnt';
static const uint32 kMsgDisconnect   = 'disc';
static const uint32 kMsgTransfer     = 'xfer';
static const uint32 kMsgPlayPause    = 'plpz';
static const uint32 kMsgNext         = 'next';
static const uint32 kMsgPrev         = 'prev';
static const uint32 kMsgVolume       = 'volu';
static const uint32 kMsgRefresh      = 'rdev';
static const uint32 kMsgControlDone  = 'cdon';

// The control operations the worker thread can run.
enum Op { OpAuthorize, OpDevices, OpTransfer, OpPlayPause, OpNext, OpPrev, OpVolume, OpNowPlaying };

// --------------------------------------------------------------------------- getInfo worker
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

// --------------------------------------------------------------------------- control worker
struct ControlJob {
	Op          op;
	AuthState   auth;          // a warm copy (tokens) so ops avoid a refresh each time
	std::string clientId;      // for OpAuthorize
	std::string deviceId;      // for OpTransfer
	std::string matchName;     // for OpDevices: the speaker name to match
	bool        wantPlay = true;   // for OpPlayPause: true=play, false=pause
	int         volume = -1;   // for OpVolume
	BMessenger  reply;
};

static int32 ControlThread(void* arg)
{
	ControlJob* job = static_cast<ControlJob*>(arg);
	BMessage m(kMsgControlDone);
	m.AddInt32("op", (int32)job->op);
	bool ok = false;
	std::string err;

	if (job->op == OpAuthorize) {
		AuthState auth;
		ok = SpotifyWebApi::Authorize(job->clientId, auth, &err);
		if (ok) {
			m.AddString("accessToken", auth.accessToken.c_str());
			m.AddString("refreshToken", auth.refreshToken.c_str());
			m.AddString("clientId", auth.clientId.c_str());
			m.AddInt64("expiresAt", (int64)auth.expiresAt);
		}
	} else {
		SpotifyWebApi api(job->auth);
		switch (job->op) {
			case OpDevices: {
				std::vector<Device> devs;
				ok = api.GetDevices(devs, &err);
				if (ok) {
					// Match the discovered speaker by name (case-insensitive).
					std::string want = job->matchName;
					for (auto& d : devs) {
						BString a(d.name.c_str()), b(want.c_str());
						if (a.ICompare(b) == 0) {
							m.AddString("matchedId", d.id.c_str());
							m.AddInt32("matchedVolume", d.volume);
							m.AddBool("matchedActive", d.active);
							break;
						}
					}
					m.AddInt32("deviceCount", (int32)devs.size());
				}
				break;
			}
			case OpTransfer:   ok = api.Transfer(job->deviceId, true, &err); break;
			case OpPlayPause:  ok = job->wantPlay ? api.Play(&err) : api.Pause(&err); break;
			case OpNext:       ok = api.Next(&err); break;
			case OpPrev:       ok = api.Previous(&err); break;
			case OpVolume:     ok = api.SetVolume(job->volume, &err); break;
			case OpNowPlaying: {
				NowPlaying np;
				ok = api.GetNowPlaying(np, &err);
				if (ok) {
					m.AddBool("playing", np.playing);
					m.AddString("track", np.track.c_str());
					m.AddString("artist", np.artist.c_str());
					m.AddString("activeDeviceId", np.deviceId.c_str());
				}
				break;
			}
			default: break;
		}
		// Return the (possibly refreshed) tokens so the window keeps a warm session.
		m.AddString("accessToken", api.Auth().accessToken.c_str());
		m.AddString("refreshToken", api.Auth().refreshToken.c_str());
		m.AddInt64("expiresAt", (int64)api.Auth().expiresAt);
	}

	m.AddBool("ok", ok);
	m.AddString("err", err.c_str());
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
	void RunControl(ControlJob* job);
	void UpdateAuthUi();
	void SetBusy(const char* what);

	std::string  fHost, fCPath, fName, fRemoteName;
	int          fPort;
	AuthState    fAuth;
	std::string  fMatchedDeviceId;
	bool         fPlaying = false;

	BTextView*   fInfo = nullptr;
	BStringView* fStatus = nullptr;
	BStringView* fNowPlaying = nullptr;
	BTextControl* fClientId = nullptr;
	BButton*     fConnectBtn = nullptr;
	BButton*     fDisconnectBtn = nullptr;
	BButton*     fTransferBtn = nullptr;
	BButton*     fPrevBtn = nullptr;
	BButton*     fPlayBtn = nullptr;
	BButton*     fNextBtn = nullptr;
	BSlider*     fVolume = nullptr;
	BView*       fSetupGroup = nullptr;
	BView*       fControlGroup = nullptr;
};

SpotifyWindow::SpotifyWindow(const std::string& host, int port, const std::string& cpath,
	const std::string& name)
	: BWindow(BRect(100, 100, 520, 480), "Spotify Connect", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fCPath(cpath), fName(name), fPort(port)
{
	SpotifyWebApi::LoadAuth(fAuth);

	BStringView* title = new BStringView("t", name.empty() ? host.c_str() : name.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	fInfo = new BTextView("i");
	fInfo->MakeEditable(false);
	fInfo->SetExplicitMinSize(BSize(360, 110));

	// --- setup group (client_id + connect) -----------------------------------
	fClientId = new BTextControl("cid", B_TRANSLATE("Client ID:"),
		fAuth.clientId.c_str(), nullptr);
	fConnectBtn = new BButton("cn", B_TRANSLATE("Collega account"), new BMessage(kMsgConnect));
	BStringView* redirect = new BStringView("rd",
		B_TRANSLATE("Crea un'app gratuita su developer.spotify.com e imposta il Redirect URI:"));
	BStringView* redirect2 = new BStringView("rd2", kRedirectUri);
	BFont mono(be_fixed_font);
	redirect2->SetFont(&mono);

	fSetupGroup = new BView("setup", 0);
	BLayoutBuilder::Group<>(fSetupGroup, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(redirect)
		.Add(redirect2)
		.Add(fClientId)
		.AddGroup(B_HORIZONTAL).AddGlue().Add(fConnectBtn).End()
	.End();

	// --- control group (playback) ---------------------------------------------
	fTransferBtn = new BButton("xf", B_TRANSLATE("Trasferisci qui"), new BMessage(kMsgTransfer));
	fPrevBtn = new BButton("pv", "|<", new BMessage(kMsgPrev));
	fPlayBtn = new BButton("pp", B_TRANSLATE("Play/Pausa"), new BMessage(kMsgPlayPause));
	fNextBtn = new BButton("nx", ">|", new BMessage(kMsgNext));
	fVolume = new BSlider("vol", B_TRANSLATE("Volume"), new BMessage(kMsgVolume), 0, 100, B_HORIZONTAL);
	fVolume->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fVolume->SetHashMarkCount(11);
	fVolume->SetValue(50);
	fNowPlaying = new BStringView("np", "");
	fDisconnectBtn = new BButton("dc", B_TRANSLATE("Scollega"), new BMessage(kMsgDisconnect));

	fControlGroup = new BView("control", 0);
	BLayoutBuilder::Group<>(fControlGroup, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(fNowPlaying)
		.AddGroup(B_HORIZONTAL)
			.Add(fPrevBtn).Add(fPlayBtn).Add(fNextBtn).AddGlue().Add(fTransferBtn)
		.End()
		.Add(fVolume)
		.AddGroup(B_HORIZONTAL).AddGlue().Add(fDisconnectBtn).End()
	.End();

	fStatus = new BStringView("st", fHost.c_str());

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(fInfo)
		.Add(fSetupGroup)
		.Add(fControlGroup)
		.Add(fStatus)
	.End();

	CenterOnScreen();
	UpdateAuthUi();
	StartProbe();
	if (fAuth.HasRefresh())
		RunControl(new ControlJob{OpDevices, fAuth, "", "", fName, true, -1, BMessenger(this)});
}

void SpotifyWindow::UpdateAuthUi()
{
	bool linked = fAuth.HasRefresh();
	if (linked) {
		if (!fSetupGroup->IsHidden()) fSetupGroup->Hide();
		if (fControlGroup->IsHidden()) fControlGroup->Show();
	} else {
		if (fSetupGroup->IsHidden()) fSetupGroup->Show();
		if (!fControlGroup->IsHidden()) fControlGroup->Hide();
	}
	// Transfer needs a matched device id.
	fTransferBtn->SetEnabled(linked && !fMatchedDeviceId.empty());
}

void SpotifyWindow::SetBusy(const char* what)
{
	fStatus->SetText(what);
}

void SpotifyWindow::RunControl(ControlJob* job)
{
	thread_id t = spawn_thread(ControlThread, "spotify_ctl", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void SpotifyWindow::StartProbe()
{
	if (fPort == 0) {
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
				fStatus->SetText(B_TRANSLATE("Dispositivo non raggiungibile o non Spotify Connect."));
				return;
			}
			const char* remoteName = ""; msg->FindString("remoteName", &remoteName);
			const char* brand = ""; msg->FindString("brand", &brand);
			const char* model = ""; msg->FindString("model", &model);
			const char* deviceType = ""; msg->FindString("deviceType", &deviceType);
			const char* version = ""; msg->FindString("version", &version);
			const char* activeUser = ""; msg->FindString("activeUser", &activeUser);
			if (remoteName[0]) fRemoteName = remoteName;
			BString s;
			if (remoteName[0]) s << B_TRANSLATE("Nome: ") << remoteName << "\n";
			if (brand[0] || model[0]) s << B_TRANSLATE("Dispositivo: ") << brand << " " << model << "\n";
			if (deviceType[0]) s << B_TRANSLATE("Tipo: ") << deviceType << "\n";
			if (version[0]) s << B_TRANSLATE("Versione: ") << version << "\n";
			s << B_TRANSLATE("Account attivo: ") << (activeUser[0] ? activeUser : B_TRANSLATE("(nessuno)")) << "\n";
			s << B_TRANSLATE("Indirizzo: ") << fHost.c_str() << ":" << fPort << "\n";
			fInfo->SetText(s.String());
			fStatus->SetText(B_TRANSLATE("Pronto."));
			// If already linked, (re)match the device now that we know its name.
			if (fAuth.HasRefresh() && !fRemoteName.empty())
				RunControl(new ControlJob{OpDevices, fAuth, "", "", fRemoteName, true, -1, BMessenger(this)});
			return;
		}

		case kMsgConnect: {
			std::string cid = fClientId->Text() ? fClientId->Text() : "";
			// trim spaces
			while (!cid.empty() && (cid.front() == ' ')) cid.erase(cid.begin());
			while (!cid.empty() && (cid.back() == ' ')) cid.pop_back();
			if (cid.empty()) {
				fStatus->SetText(B_TRANSLATE("Inserisci il Client ID."));
				return;
			}
			fConnectBtn->SetEnabled(false);
			SetBusy(B_TRANSLATE("Autorizza Campiello nel browser, poi torna qui..."));
			RunControl(new ControlJob{OpAuthorize, fAuth, cid, "", "", true, -1, BMessenger(this)});
			return;
		}

		case kMsgDisconnect:
			SpotifyWebApi::ClearAuth();
			fAuth = AuthState();
			fMatchedDeviceId.clear();
			fNowPlaying->SetText("");
			UpdateAuthUi();
			fStatus->SetText(B_TRANSLATE("Account scollegato."));
			return;

		case kMsgTransfer:
			if (fMatchedDeviceId.empty()) { fStatus->SetText(B_TRANSLATE("Dispositivo non trovato nell'account.")); return; }
			SetBusy(B_TRANSLATE("Trasferisco la riproduzione..."));
			RunControl(new ControlJob{OpTransfer, fAuth, "", fMatchedDeviceId, "", true, -1, BMessenger(this)});
			return;

		case kMsgPlayPause:
			SetBusy(B_TRANSLATE("Invio comando..."));
			RunControl(new ControlJob{OpPlayPause, fAuth, "", "", "", !fPlaying, -1, BMessenger(this)});
			return;

		case kMsgNext:
			SetBusy(B_TRANSLATE("Invio comando..."));
			RunControl(new ControlJob{OpNext, fAuth, "", "", "", true, -1, BMessenger(this)});
			return;

		case kMsgPrev:
			SetBusy(B_TRANSLATE("Invio comando..."));
			RunControl(new ControlJob{OpPrev, fAuth, "", "", "", true, -1, BMessenger(this)});
			return;

		case kMsgVolume:
			SetBusy(B_TRANSLATE("Imposto il volume..."));
			RunControl(new ControlJob{OpVolume, fAuth, "", "", "", true, fVolume->Value(), BMessenger(this)});
			return;

		case kMsgRefresh:
			if (fAuth.HasRefresh())
				RunControl(new ControlJob{OpNowPlaying, fAuth, "", "", "", true, -1, BMessenger(this)});
			return;

		case kMsgControlDone: {
			int32 op = -1; msg->FindInt32("op", &op);
			bool ok = false; msg->FindBool("ok", &ok);
			const char* err = ""; msg->FindString("err", &err);

			// Update the warm token from any op that returned one.
			const char* at = nullptr;
			if (msg->FindString("accessToken", &at) == B_OK && at && at[0])
				fAuth.accessToken = at;
			const char* rt = nullptr;
			if (msg->FindString("refreshToken", &rt) == B_OK && rt && rt[0])
				fAuth.refreshToken = rt;
			int64 exp = 0;
			if (msg->FindInt64("expiresAt", &exp) == B_OK && exp)
				fAuth.expiresAt = (time_t)exp;

			if (op == OpAuthorize) {
				fConnectBtn->SetEnabled(true);
				if (ok) {
					const char* cid = ""; msg->FindString("clientId", &cid);
					fAuth.clientId = cid;
					UpdateAuthUi();
					fStatus->SetText(B_TRANSLATE("Account collegato."));
					if (!fRemoteName.empty())
						RunControl(new ControlJob{OpDevices, fAuth, "", "", fRemoteName, true, -1, BMessenger(this)});
				} else {
					fStatus->SetText(err[0] ? err : B_TRANSLATE("Collegamento non riuscito."));
				}
				return;
			}

			if (!ok) {
				fStatus->SetText(err[0] ? err : B_TRANSLATE("Operazione non riuscita."));
				return;
			}

			switch (op) {
				case OpDevices: {
					const char* mid = nullptr;
					if (msg->FindString("matchedId", &mid) == B_OK && mid && mid[0]) {
						fMatchedDeviceId = mid;
						fTransferBtn->SetEnabled(true);
						fStatus->SetText(B_TRANSLATE("Dispositivo pronto per il controllo."));
						RunControl(new ControlJob{OpNowPlaying, fAuth, "", "", "", true, -1, BMessenger(this)});
					} else {
						fMatchedDeviceId.clear();
						fTransferBtn->SetEnabled(false);
						fStatus->SetText(B_TRANSLATE("Speaker non visibile nell'account: aprilo una volta con l'app Spotify."));
					}
					return;
				}
				case OpTransfer:
					fStatus->SetText(B_TRANSLATE("Riproduzione trasferita."));
					RunControl(new ControlJob{OpNowPlaying, fAuth, "", "", "", true, -1, BMessenger(this)});
					return;
				case OpPlayPause:
				case OpNext:
				case OpPrev:
				case OpVolume:
					fStatus->SetText(B_TRANSLATE("Fatto."));
					RunControl(new ControlJob{OpNowPlaying, fAuth, "", "", "", true, -1, BMessenger(this)});
					return;
				case OpNowPlaying: {
					bool playing = false; msg->FindBool("playing", &playing);
					fPlaying = playing;
					const char* track = ""; msg->FindString("track", &track);
					const char* artist = ""; msg->FindString("artist", &artist);
					BString np;
					if (track[0]) {
						np << (playing ? B_TRANSLATE("In riproduzione: ") : B_TRANSLATE("In pausa: "));
						np << track;
						if (artist[0]) np << " - " << artist;
					} else {
						np << B_TRANSLATE("Niente in riproduzione.");
					}
					fNowPlaying->SetText(np.String());
					fPlayBtn->SetLabel(playing ? B_TRANSLATE("Pausa") : B_TRANSLATE("Play"));
					return;
				}
				default:
					return;
			}
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
