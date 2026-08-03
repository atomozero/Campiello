// campiello_cast.cpp
//
// The Campiello Google Cast add-on: a control panel for a Chromecast / Google Cast device discovered
// via _googlecast._tcp. It speaks two protocols:
//   - CASTv2 (TLS 8009, the real Cast control channel): reads the device's true state (volume, the
//     running app) and casts a media URL into the Default Media Receiver, with a volume control.
//   - DIAL (plain HTTP 8008): a simple launch/stop of named receiver apps (YouTube, Netflix), kept as
//     a convenient fallback that also works when CASTv2 is unavailable.
// Network I/O runs on worker threads so the UI never blocks.
//
// Screen mirroring is deliberately NOT here: casting a mirrored desktop uses a separate proprietary
// Cast media-remoting channel (a closed WebRTC/RTP pipeline) with no open client implementation; it
// is documented as a follow-up in docs/addons/cast.md and is not faked.
//
// Launched from the WON neighborhood on a double-click of a Cast device (the cast.handler manifest),
// which passes CAMPIELLO:host/name, or from the command line with host=<ip> [name=<label>]. Links
// libbe + the network kit + OpenSSL (for the CASTv2 TLS channel). End-user strings are Italian.
//
//   g++ -std=c++17 campiello_cast.cpp DialClient.cpp CastChannel.cpp -lbe -lnetwork -lssl -lcrypto

#include <Application.h>
#include <Alert.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Button.h>
#include <DataIO.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
#include <Path.h>
#include <Screen.h>
#include <StringView.h>
#include <String.h>
#include <TextControl.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <Window.h>

#include <fs_attr.h>

#include <atomic>
#include <cstring>
#include <string>

#include "CastChannel.h"
#include "DialClient.h"
#include "MediaServer.h"
#include "MirrorSession.h"

using namespace campiello::cast;

static const char* const kSignature = "application/x-vnd.Campiello-cast";

static const uint32 kMsgInfo       = 'cinf';
static const uint32 kMsgInfoReady  = 'cinr';
static const uint32 kMsgLaunch     = 'clau'; // "app" = receiver app name (DIAL)
static const uint32 kMsgStop       = 'csto';
static const uint32 kMsgCastUrl    = 'curl';
static const uint32 kMsgPickFile   = 'cpik';
static const uint32 kMsgFilePicked = 'cfpk';
static const uint32 kMsgVolUp      = 'cvup';
static const uint32 kMsgVolDown    = 'cvdn';
static const uint32 kMsgScreenTgl  = 'cscr';
static const uint32 kMsgScreenEnd  = 'csnd';
static const uint32 kMsgMirrorTgl  = 'cmir';
static const uint32 kMsgMirrorEnd  = 'cmen';
static const uint32 kMsgActionDone = 'cact';

// The DIAL receiver apps this panel can launch by name.
static const char* const kApps[] = {"YouTube", "Netflix"};
static const int kNumApps = 2;

// Guess a Cast contentType from the URL's extension (best-effort; the receiver is the final judge).
static std::string GuessContentType(const std::string& url)
{
	auto ends = [&](const char* ext) {
		size_t n = std::strlen(ext);
		return url.size() >= n && url.compare(url.size() - n, n, ext) == 0;
	};
	if (ends(".mp3")) return "audio/mpeg";
	if (ends(".m4a") || ends(".aac")) return "audio/aac";
	if (ends(".ogg") || ends(".opus")) return "audio/ogg";
	if (ends(".flac")) return "audio/flac";
	if (ends(".wav")) return "audio/wav";
	if (ends(".webm")) return "video/webm";
	if (ends(".mkv")) return "video/x-matroska";
	if (ends(".m3u8")) return "application/x-mpegurl";
	if (ends(".mpd")) return "application/dash+xml";
	if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
	if (ends(".png")) return "image/png";
	return "video/mp4";
}

// --------------------------------------------------------------------------- workers
struct InfoJob { std::string host; BMessenger reply; };
static int32 InfoThread(void* arg)
{
	InfoJob* job = static_cast<InfoJob*>(arg);
	BMessage m(kMsgInfoReady);

	// Prefer the CASTv2 truth (volume + running app); fall back to DIAL if it will not connect.
	CastChannel ch(job->host);
	bool v2 = ch.Connect();
	if (v2) {
		CastStatus st = ch.GetStatus();
		ch.Close();
		m.AddBool("v2", st.ok);
		if (st.ok) {
			m.AddString("app", st.displayName.c_str());
			m.AddString("status", st.statusText.c_str());
			m.AddString("session", st.sessionId.c_str());
			m.AddFloat("vol", st.volumeLevel);
			m.AddBool("muted", st.muted);
		}
	} else {
		m.AddBool("v2", false);
	}

	// Always fetch the DIAL friendly name (nice title) and which named app is running.
	DialClient d(job->host);
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

struct ActionJob { std::string host, app, action, session; BMessenger reply; };
static int32 ActionThread(void* arg)
{
	ActionJob* job = static_cast<ActionJob*>(arg);
	bool ok = false;
	if (job->action == "stop-v2") {
		CastChannel ch(job->host);
		if (ch.Connect()) { ok = ch.StopApp(job->session); ch.Close(); }
	} else {
		DialClient d(job->host);
		ok = (job->action == "launch") ? d.Launch(job->app) : d.Stop(job->app);
	}
	BMessage m(kMsgActionDone);
	m.AddBool("ok", ok);
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct CastJob { std::string host, url, contentType, title; BMessenger reply; };
static int32 CastThread(void* arg)
{
	CastJob* job = static_cast<CastJob*>(arg);
	CastChannel ch(job->host);
	bool ok = false;
	BString err;
	if (ch.Connect()) {
		ok = ch.CastUrl(job->url, job->contentType, job->title);
		if (!ok && ch.Error() != nullptr)
			err = ch.Error();
		ch.Close();
	} else {
		err = ch.Error() != nullptr ? ch.Error() : "connessione non riuscita";
	}
	BMessage m(kMsgActionDone);
	m.AddBool("ok", ok);
	if (!ok)
		m.AddString("err", err.String());
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct VolumeJob { std::string host; float level; BMessenger reply; };
static int32 VolumeThread(void* arg)
{
	VolumeJob* job = static_cast<VolumeJob*>(arg);
	CastChannel ch(job->host);
	bool ok = false;
	if (ch.Connect()) { ok = ch.SetVolume(job->level); ch.Close(); }
	BMessage m(kMsgActionDone);
	m.AddBool("ok", ok);
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// Grab the main screen and compress it to an in-memory JPEG via the Translation Kit (no external
// dependency). Returns false if the screen or the encoder is unavailable.
static bool CaptureJpeg(std::string& out)
{
	BScreen screen(B_MAIN_SCREEN_ID);
	if (!screen.IsValid())
		return false;
	BBitmap* shot = nullptr;
	if (screen.GetBitmap(&shot, false) != B_OK || shot == nullptr)
		return false;

	BBitmapStream stream(shot); // wraps `shot`; detached again below so we can free it
	BMallocIO mio;
	status_t s = BTranslatorRoster::Default()->Translate(&stream, nullptr, nullptr, &mio,
		B_JPEG_FORMAT);
	BBitmap* detached = nullptr;
	stream.DetachBitmap(&detached);
	delete shot;
	if (s != B_OK || mio.BufferLength() == 0)
		return false;
	out.assign(static_cast<const char*>(mio.Buffer()), mio.BufferLength());
	return true;
}

// The full screen-mirroring loop (Cast Streaming): negotiate, then capture the screen and send it as
// encrypted VP8 over Cast RTP for as long as the user leaves it on. ~15 fps, no audio yet.
struct MirrorJob {
	std::string host;
	std::atomic<bool>* stop;
	BMessenger reply;
};
static int32 MirrorThread(void* arg)
{
	MirrorJob* job = static_cast<MirrorJob*>(arg);
	BMessage done(kMsgMirrorEnd);

	BScreen screen(B_MAIN_SCREEN_ID);
	BBitmap* probe = nullptr;
	if (!screen.IsValid() || screen.GetBitmap(&probe, false) != B_OK || probe == nullptr) {
		done.AddString("err", "Cattura schermo non disponibile.");
		job->reply.SendMessage(&done);
		delete job;
		return 0;
	}
	int w = ((int)probe->Bounds().Width() + 1) & ~1;
	int h = ((int)probe->Bounds().Height() + 1) & ~1;
	delete probe;

	const int fps = 15;
	MirrorSession session(job->host);
	if (!session.Start(w, h, fps)) {
		done.AddString("err", session.LastError().c_str());
		job->reply.SendMessage(&done);
		delete job;
		return 0;
	}

	const bigtime_t frameInterval = 1000000 / fps;
	while (!job->stop->load()) {
		bigtime_t t0 = system_time();
		BBitmap* f = nullptr;
		if (screen.GetBitmap(&f, false) == B_OK && f != nullptr) {
			session.SendFrame(reinterpret_cast<const uint8_t*>(f->Bits()), f->BytesPerRow());
			delete f;
		}
		// Pace to the target frame rate (encode+send already took some time).
		bigtime_t spent = system_time() - t0;
		if (spent < frameInterval)
			snooze(frameInterval - spent);
	}
	session.Stop();
	done.AddBool("ok", true);
	job->reply.SendMessage(&done);
	delete job;
	return 0;
}

// The ~1 fps "screen on the TV" loop: connect once, launch the media receiver once, then every second
// grab the screen, publish the JPEG on the media server's buffer, and LOAD a fresh (cache-busting)
// image URL. Honest limits: no audio, ~1 fps (this is a refreshing still, not smooth mirroring).
struct ScreenJob {
	std::string host;
	MediaServer* server;
	std::atomic<bool>* stop;
	int port;
	BMessenger reply;
};
static int32 ScreenThread(void* arg)
{
	ScreenJob* job = static_cast<ScreenJob*>(arg);
	BMessage done(kMsgScreenEnd);

	std::string ip = LocalIpToward(job->host);
	CastChannel ch(job->host);
	if (ip.empty() || !ch.Connect() || !ch.LaunchMediaReceiver()) {
		done.AddString("err", "Impossibile avviare la sessione sul dispositivo.");
		job->reply.SendMessage(&done);
		delete job;
		return 0;
	}

	int frame = 0;
	while (!job->stop->load()) {
		std::string jpeg;
		if (CaptureJpeg(jpeg)) {
			job->server->UpdateBuffer(jpeg);
			BString url;
			url << "http://" << ip.c_str() << ":" << job->port << "/frame" << frame << ".jpg";
			ch.Load(std::string(url.String()), "image/jpeg", "Schermo Campiello", false);
			++frame;
		}
		// Sleep ~1 s, but wake quickly when asked to stop.
		for (int i = 0; i < 10 && !job->stop->load(); ++i)
			snooze(100000);
	}
	ch.Close();
	done.AddBool("ok", true);
	job->reply.SendMessage(&done);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class CastWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	CastWindow(const std::string& host, const std::string& name);
	~CastWindow() override
	{
		fScreenStop.store(true);
		if (fScreenThread >= 0) {
			status_t r;
			wait_for_thread(fScreenThread, &r);
		}
		fMirrorStop.store(true);
		if (fMirrorThread >= 0) {
			status_t r;
			wait_for_thread(fMirrorThread, &r);
		}
		fMediaServer.Stop();
		delete fFilePanel;
	}
	void MessageReceived(BMessage* msg) override;

private:
	void StartInfo();
	void RunAction(const std::string& app, const std::string& action);
	void StartScreen();
	void StopScreen();
	void StartMirror();
	void StopMirror();

	std::string  fHost;
	std::string  fName;
	std::string  fRunning;    // DIAL app currently running, if known
	std::string  fSession;    // CASTv2 sessionId (for STOP)
	float        fVolume = -1.0f;
	BStringView* fSummary = nullptr;
	BStringView* fVolView = nullptr;
	BTextControl* fUrl = nullptr;
	BStringView* fStatus = nullptr;
	BButton*     fScreenBtn = nullptr;
	MediaServer  fMediaServer; // serves a local file (or the live screen JPEG) over HTTP
	BFilePanel*  fFilePanel = nullptr;
	bool         fScreenOn = false;
	std::atomic<bool> fScreenStop{false};
	thread_id    fScreenThread = -1;
	BButton*     fMirrorBtn = nullptr;
	bool         fMirrorOn = false;
	std::atomic<bool> fMirrorStop{false};
	thread_id    fMirrorThread = -1;
};

CastWindow::CastWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(100, 100, 460, 420), "Google Cast", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fName(name)
{
	BStringView* title = new BStringView("t", fName.empty() ? "Dispositivo Cast" : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	fSummary = new BStringView("sum", "Interrogo il dispositivo...");
	fVolView = new BStringView("vol", "Volume: -");

	BMessage* yt = new BMessage(kMsgLaunch); yt->AddString("app", "YouTube");
	BMessage* nf = new BMessage(kMsgLaunch); nf->AddString("app", "Netflix");
	BButton* bYt = new BButton("yt", "Avvia YouTube", yt);
	BButton* bNf = new BButton("nf", "Avvia Netflix", nf);
	BButton* bStop = new BButton("stop", "Ferma app", new BMessage(kMsgStop));
	BButton* bRefresh = new BButton("refresh", "Aggiorna", new BMessage(kMsgInfo));

	BButton* bVolDn = new BButton("vd", "Vol -", new BMessage(kMsgVolDown));
	BButton* bVolUp = new BButton("vu", "Vol +", new BMessage(kMsgVolUp));

	fUrl = new BTextControl("url", "URL media:", "", nullptr);
	BButton* bCast = new BButton("cast", "Casta URL", new BMessage(kMsgCastUrl));
	BButton* bLocal = new BButton("local", "Casta file locale...", new BMessage(kMsgPickFile));
	fScreenBtn = new BButton("screen", "Schermo su TV", new BMessage(kMsgScreenTgl));
	fMirrorBtn = new BButton("mirror", "Specchia schermo", new BMessage(kMsgMirrorTgl));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(fSummary)
		.AddGroup(B_HORIZONTAL)
			.Add(fVolView)
			.AddGlue()
			.Add(bVolDn)
			.Add(bVolUp)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(bYt)
			.Add(bNf)
			.Add(bStop)
		.End()
		.Add(fUrl)
		.AddGroup(B_HORIZONTAL)
			.Add(bLocal)
			.Add(fScreenBtn)
			.AddGlue()
			.Add(bCast)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fMirrorBtn)
			.Add(new BStringView("mnote", "(sperimentale, ~15 fps, senza audio)"))
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus = new BStringView("st", fHost.c_str()))
			.AddGlue()
			.Add(bRefresh)
		.End()
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
	fStatus->SetText(action == "launch" ? "Avvio in corso..." : "Comando in corso...");
	ActionJob* job = new ActionJob{fHost, app, action, fSession, BMessenger(this)};
	thread_id t = spawn_thread(ActionThread, "cast_act", B_NORMAL_PRIORITY, job);
	if (t < 0) delete job; else resume_thread(t);
}

void CastWindow::StartScreen()
{
	int port = fMediaServer.ServeBuffer("image/jpeg");
	if (port == 0) {
		fStatus->SetText("Impossibile avviare il server locale.");
		return;
	}
	fScreenStop.store(false);
	ScreenJob* job = new ScreenJob{fHost, &fMediaServer, &fScreenStop, port, BMessenger(this)};
	fScreenThread = spawn_thread(ScreenThread, "cast_screen", B_NORMAL_PRIORITY, job);
	if (fScreenThread < 0) {
		delete job;
		fMediaServer.Stop();
		fStatus->SetText("Impossibile avviare la cattura schermo.");
		return;
	}
	resume_thread(fScreenThread);
	fScreenOn = true;
	fScreenBtn->SetLabel("Ferma schermo su TV");
	fStatus->SetText("Schermo su TV attivo (~1 fps, senza audio).");
}

void CastWindow::StopScreen()
{
	if (!fScreenOn)
		return;
	fScreenStop.store(true);
	if (fScreenThread >= 0) {
		status_t r;
		wait_for_thread(fScreenThread, &r); // the loop exits within ~1 s
		fScreenThread = -1;
	}
	fMediaServer.Stop();
	fScreenOn = false;
	fScreenBtn->SetLabel("Schermo su TV");
	fStatus->SetText("Schermo su TV fermato.");
}

void CastWindow::StartMirror()
{
	fMirrorStop.store(false);
	MirrorJob* job = new MirrorJob{fHost, &fMirrorStop, BMessenger(this)};
	fMirrorThread = spawn_thread(MirrorThread, "cast_mirror", B_NORMAL_PRIORITY, job);
	if (fMirrorThread < 0) {
		delete job;
		fStatus->SetText("Impossibile avviare il mirroring.");
		return;
	}
	resume_thread(fMirrorThread);
	fMirrorOn = true;
	fMirrorBtn->SetLabel("Ferma mirroring");
	fStatus->SetText("Mirroring in avvio (negoziazione)...");
}

void CastWindow::StopMirror()
{
	if (!fMirrorOn)
		return;
	fMirrorStop.store(true);
	if (fMirrorThread >= 0) {
		status_t r;
		wait_for_thread(fMirrorThread, &r);
		fMirrorThread = -1;
	}
	fMirrorOn = false;
	fMirrorBtn->SetLabel("Specchia schermo");
	fStatus->SetText("Mirroring fermato.");
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
			bool v2 = false; msg->FindBool("v2", &v2);

			BString sum(name[0] ? name : fHost.c_str());
			if (v2) {
				const char* app = ""; msg->FindString("app", &app);
				const char* status = ""; msg->FindString("status", &status);
				const char* session = ""; msg->FindString("session", &session);
				fSession = session;
				if (app[0] != '\0') {
					sum << "\nIn esecuzione: " << app;
					if (status[0] != '\0')
						sum << " (" << status << ")";
				} else {
					sum << "\nNessuna app in esecuzione.";
				}
				float vol = -1.0f; msg->FindFloat("vol", &vol);
				bool muted = false; msg->FindBool("muted", &muted);
				fVolume = vol;
				BString v("Volume: ");
				if (vol >= 0.0f) v << (int)(vol * 100 + 0.5f) << " %"; else v << "-";
				if (muted) v << " (muto)";
				fVolView->SetText(v.String());
			} else {
				fSession.clear();
				if (fRunning.empty())
					sum << "\nNessuna app in esecuzione (CASTv2 non disponibile).";
				else
					sum << "\nIn esecuzione: " << fRunning.c_str() << " (via DIAL)";
				fVolView->SetText("Volume: - (CASTv2 non disponibile)");
			}
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
			// Prefer the CASTv2 STOP (stops any app) when we have a session; else DIAL-stop a known app.
			if (!fSession.empty())
				RunAction("", "stop-v2");
			else
				RunAction(fRunning.empty() ? "YouTube" : fRunning, "stop");
			return;
		case kMsgCastUrl: {
			BString url(fUrl != nullptr ? fUrl->Text() : "");
			url.Trim();
			if (url.Length() == 0) {
				fStatus->SetText("Inserisci un URL media.");
				return;
			}
			if (fMirrorOn) StopMirror();
			if (fScreenOn) StopScreen();
			fStatus->SetText("Casting in corso...");
			CastJob* job = new CastJob{fHost, std::string(url.String()),
				GuessContentType(url.String()), "Campiello", BMessenger(this)};
			thread_id t = spawn_thread(CastThread, "cast_url", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgPickFile: {
			if (fFilePanel == nullptr) {
				fFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr,
					B_FILE_NODE, false, new BMessage(kMsgFilePicked));
				fFilePanel->Window()->SetTitle("Scegli un file da castare");
			}
			fFilePanel->Show();
			return;
		}
		case kMsgFilePicked: {
			entry_ref ref;
			if (msg->FindRef("refs", &ref) != B_OK) {
				fStatus->SetText("Nessun file scelto.");
				return;
			}
			BEntry entry(&ref);
			BPath path;
			if (entry.GetPath(&path) != B_OK || path.Path() == nullptr) {
				fStatus->SetText("File non valido.");
				return;
			}
			if (fMirrorOn) StopMirror();
			if (fScreenOn) StopScreen();
			std::string filePath = path.Path();
			std::string leaf = path.Leaf() != nullptr ? path.Leaf() : "stream";
			std::string ct = GuessMediaType(filePath);

			int port = fMediaServer.Serve(filePath, ct);
			if (port == 0) {
				fStatus->SetText("Impossibile avviare il server locale.");
				return;
			}
			std::string ip = LocalIpToward(fHost);
			if (ip.empty()) {
				fStatus->SetText("Impossibile determinare l'IP locale.");
				fMediaServer.Stop();
				return;
			}
			// The Chromecast ignores the path (we serve one file), but keep the extension so it and
			// any format sniffing see a sensible name.
			std::string ext;
			size_t dot = leaf.rfind('.');
			if (dot != std::string::npos)
				ext = leaf.substr(dot);
			BString url;
			url << "http://" << ip.c_str() << ":" << port << "/stream" << ext.c_str();

			fStatus->SetText("Casting del file locale...");
			CastJob* job = new CastJob{fHost, std::string(url.String()), ct, leaf, BMessenger(this)};
			thread_id t = spawn_thread(CastThread, "cast_file", B_NORMAL_PRIORITY, job);
			if (t < 0) { delete job; fMediaServer.Stop(); }
			else resume_thread(t);
			return;
		}
		case kMsgScreenTgl:
			if (fScreenOn) {
				StopScreen();
			} else {
				if (fMirrorOn) StopMirror();
				StartScreen();
			}
			return;
		case kMsgMirrorTgl:
			if (fMirrorOn) {
				StopMirror();
			} else {
				if (fScreenOn) StopScreen();
				StartMirror();
			}
			return;
		case kMsgMirrorEnd: {
			if (fMirrorThread >= 0) {
				status_t r;
				wait_for_thread(fMirrorThread, &r);
				fMirrorThread = -1;
			}
			if (fMirrorOn) {
				fMirrorOn = false;
				fMirrorBtn->SetLabel("Specchia schermo");
				const char* err = ""; msg->FindString("err", &err);
				fStatus->SetText(err[0] ? err : "Mirroring terminato.");
			}
			return;
		}
		case kMsgScreenEnd: {
			// The screen loop ended on its own (an error connecting/launching). Reset the UI.
			if (fScreenThread >= 0) {
				status_t r;
				wait_for_thread(fScreenThread, &r);
				fScreenThread = -1;
			}
			if (fScreenOn) {
				fMediaServer.Stop();
				fScreenOn = false;
				fScreenBtn->SetLabel("Schermo su TV");
				const char* err = ""; msg->FindString("err", &err);
				fStatus->SetText(err[0] ? err : "Schermo su TV terminato.");
			}
			return;
		}
		case kMsgVolUp:
		case kMsgVolDown: {
			if (fVolume < 0.0f) {
				fStatus->SetText("Volume non disponibile (serve CASTv2).");
				return;
			}
			float level = fVolume + (msg->what == kMsgVolUp ? 0.1f : -0.1f);
			if (level < 0.0f) level = 0.0f;
			if (level > 1.0f) level = 1.0f;
			fVolume = level;
			BString v("Volume: "); v << (int)(level * 100 + 0.5f) << " %";
			fVolView->SetText(v.String());
			fStatus->SetText("Regolo il volume...");
			VolumeJob* job = new VolumeJob{fHost, level, BMessenger(this)};
			thread_id t = spawn_thread(VolumeThread, "cast_vol", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgActionDone: {
			bool ok = false; msg->FindBool("ok", &ok);
			const char* err = ""; msg->FindString("err", &err);
			if (ok)
				fStatus->SetText("Fatto.");
			else
				fStatus->SetText(err[0] ? err : "Comando non riuscito.");
			if (ok) StartInfo(); // refresh state
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
