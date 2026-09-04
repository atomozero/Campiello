// campiello_esphome.cpp
//
// The Campiello ESPHome add-on: for an ESPHome node discovered via _esphomelib._tcp it shows what
// the device advertises over mDNS (ESPHome/firmware version, project, board, platform, MAC) and
// opens its web interface in a browser. ESPHome's rich local control lives in its native API, a
// protobuf channel on TCP 6053 (optionally Noise-encrypted, and password/API-key protected); that is
// a documented follow-up (see below) and is NOT faked here - this add-on covers identify + web UI.
//
// Launched from the WON neighborhood on a double-click of an ESPHome device (the esphome.handler
// manifest), which passes CAMPIELLO:host/name/port and the mDNS TXT as CAMPIELLO:txt.<key>, or from
// the command line with host=<ip> [name=<label>]. No network, no third-party dependency: links only
// libbe (the web UI opens via be_roster). End-user strings are Italian.
//
//   g++ -std=c++17 campiello_esphome.cpp -lbe

#include <Application.h>
#include <Alert.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <Button.h>
#include <CheckBox.h>
#include <DataIO.h>
#include <Dragger.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Node.h>
#include <PopUpMenu.h>
#include <Path.h>
#include <Roster.h>
#include <StringView.h>
#include <String.h>
#include <TextControl.h>
#include <TextView.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <TypeConstants.h>
#include <View.h>
#include <Window.h>

#include <fs_attr.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <Catalog.h>

#include "MjpegStream.h"

using campiello::esphome::MjpegStream;
using campiello::esphome::kDefaultStreamPort;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ESPHome"

static const char* const kSignature = "application/x-vnd.Campiello-esphome";

static const uint32 kMsgOpenWeb    = 'eweb';
static const uint32 kMsgOpenVideo  = 'evid';
static const uint32 kMsgFrame      = 'nfrm';
static const uint32 kMsgCamError   = 'cer!';
static const uint32 kMsgReconnect  = 'rcon';
static const uint32 kMsgSnapshot   = 'csnp';
static const uint32 kMsgToggleAuto = 'caut';
static const uint32 kMsgSaveFrame  = 'csav';
static const uint32 kMsgDesktopWidget = 'cwid';
static const uint32 kMsgRepStream  = 'rpst';   // replicant: switch to live stream
static const uint32 kMsgRepSnap    = 'rpsn';   // replicant: switch to snapshot poll
static const uint32 kMsgRepTick    = 'rptk';   // replicant: snapshot timer tick

// Must match the real C++ class exactly: the shelf builds the mangled Instantiate symbol from this
// string. The class lives in the global namespace, so no namespace prefix.
static const char* const kReplicantClass = "EsphomeCameraReplicant";

// Look up a TXT value by key.
static std::string TxtGet(const std::vector<std::pair<std::string, std::string>>& txt,
	const char* key)
{
	for (const auto& kv : txt)
		if (kv.first == key)
			return kv.second;
	return "";
}

// --------------------------------------------------------------------------- camera video
// Decode a JPEG buffer to a BBitmap via the Translation Kit (caller owns). nullptr on failure.
static BBitmap* DecodeJpeg(const unsigned char* data, size_t len)
{
	BMemoryIO mem(data, len);
	BBitmapStream out;
	BBitmap* bmp = nullptr;
	if (BTranslatorRoster::Default()->Translate(&mem, nullptr, nullptr, &out,
			B_TRANSLATOR_BITMAP) == B_OK)
		out.DetachBitmap(&bmp);
	return bmp;
}

// An ESP32-CAM node exposes an MJPEG stream; detect it from the board or the device/project name.
static bool LooksLikeCamera(const std::string& name, const std::string& project,
	const std::string& board)
{
	auto lc = [](std::string x) {
		for (char& c : x) c = (char)std::tolower((unsigned char)c);
		return x;
	};
	std::string ln = lc(name), lp = lc(project), lb = lc(board);
	return lb.find("cam") != std::string::npos
		|| ln.find("camera") != std::string::npos || ln.find("telecamera") != std::string::npos
		|| lp.find("camera") != std::string::npos || lp.find("telecamera") != std::string::npos;
}

// Shows the latest camera frame, letterbox-scaled to fit.
class CameraView : public BView {
public:
	CameraView() : BView("cam", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewColor(20, 20, 20);
		SetExplicitMinSize(BSize(320, 240));
	}
	~CameraView() override { delete fBitmap; }

	void SetFrame(BBitmap* b) { delete fBitmap; fBitmap = b; Invalidate(); }

	void Draw(BRect) override
	{
		if (fBitmap == nullptr)
			return;
		BRect bounds = Bounds();
		BRect src = fBitmap->Bounds();
		float sw = src.Width() + 1, sh = src.Height() + 1;
		float bw = bounds.Width() + 1, bh = bounds.Height() + 1;
		float scale = std::min(bw / sw, bh / sh);
		float dw = sw * scale, dh = sh * scale;
		BRect dst((bw - dw) / 2, (bh - dh) / 2, (bw - dw) / 2 + dw - 1, (bh - dh) / 2 + dh - 1);
		DrawBitmap(fBitmap, src, dst);
	}

private:
	BBitmap* fBitmap = nullptr;
};

// Opens the draggable desktop-widget holder (defined after the replicant class below).
static void OpenCameraWidget(const std::string& host, int streamPort, int snapPort,
	const std::string& title);

// A window that shows the ESP32-CAM feed. Two modes: the live MJPEG stream (port 8080), and a
// low-rate snapshot poll (port 8081) that fetches one JPEG at a time - useful because the ESP32-CAM
// serves a single MJPEG client, so a snapshot is the way to peek while something else (e.g. Home
// Assistant) holds the stream. The host and both ports are editable, and a frame can be saved to disk.
class CameraWindow : public BWindow {
public:
	CameraWindow(const std::string& host, int port, const std::string& title)
		: BWindow(BRect(120, 120, 120 + 640, 120 + 540), title.c_str(), B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
		  fHost(host), fPort(port), fSnapPort(port == 8080 ? 8081 : port + 1)
	{
		fView = new CameraView();
		fStatus = new BStringView("st", B_TRANSLATE("Connessione..."));
		fHostCtl = new BTextControl("h", B_TRANSLATE("Host:"), host.c_str(), nullptr);
		fPortCtl = new BTextControl("p", B_TRANSLATE("Stream:"), std::to_string(fPort).c_str(), nullptr);
		fSnapCtl = new BTextControl("s", B_TRANSLATE("Foto:"), std::to_string(fSnapPort).c_str(), nullptr);
		fPortCtl->SetExplicitMaxSize(BSize(90, B_SIZE_UNSET));
		fSnapCtl->SetExplicitMaxSize(BSize(80, B_SIZE_UNSET));
		fAuto = new BCheckBox("a", B_TRANSLATE("Foto auto"), new BMessage(kMsgToggleAuto));

		BButton* recon = new BButton("r", B_TRANSLATE("Diretta"), new BMessage(kMsgReconnect));
		BButton* snap = new BButton("n", B_TRANSLATE("Fotogramma"), new BMessage(kMsgSnapshot));
		BButton* save = new BButton("v", B_TRANSLATE("Salva..."), new BMessage(kMsgSaveFrame));
		BButton* widget = new BButton("d", B_TRANSLATE("Desktop"), new BMessage(kMsgDesktopWidget));
		BButton* web = new BButton("w", B_TRANSLATE("Web"), new BMessage(kMsgOpenWeb));

		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(fView)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.SetInsets(B_USE_SMALL_INSETS)
				.Add(fHostCtl)
				.Add(fPortCtl)
				.Add(fSnapCtl)
				.Add(recon)
				.Add(snap)
				.Add(fAuto)
				.Add(save)
				.Add(widget)
				.Add(web)
			.End()
			.AddGroup(B_HORIZONTAL)
				.SetInsets(B_USE_SMALL_INSETS)
				.Add(fStatus)
				.AddGlue()
			.End()
		.End();
		CenterOnScreen();
		Start();
	}

	bool QuitRequested() override { StopAuto(); Stop(); return true; }

	void MessageReceived(BMessage* m) override
	{
		switch (m->what) {
			case kMsgFrame: {
				BBitmap* b = nullptr;
				if (m->FindPointer("bmp", (void**)&b) == B_OK && b != nullptr) {
					fView->SetFrame(b);
					const void* jpeg = nullptr; ssize_t jlen = 0;
					if (m->FindData("jpeg", B_RAW_TYPE, &jpeg, &jlen) == B_OK && jpeg && jlen > 0)
						fLastJpeg.assign((const unsigned char*)jpeg, (const unsigned char*)jpeg + jlen);
					++fFrames;
					bigtime_t now = system_time();
					if (fT0 == 0) fT0 = now;
					double sec = (now - fT0) / 1e6;
					if (sec > 0.5) {
						BString s;
						if (fSnapMode)
							s.SetToFormat("%s  -  %s", fHost.c_str(), B_TRANSLATE("fotogramma"));
						else
							s.SetToFormat("%s  -  %.1f fps", fHost.c_str(), fFrames / sec);
						fStatus->SetText(s.String());
					}
				}
				return;
			}
			case kMsgCamError: {
				const char* e = ""; m->FindString("err", &e);
				BString s(B_TRANSLATE("Errore: ")); s << e;
				fStatus->SetText(s.String());
				return;
			}
			case kMsgReconnect:
				StopAuto();
				Stop();
				ReadFields();
				fSnapMode = false; fFrames = 0; fT0 = 0;
				fStatus->SetText(B_TRANSLATE("Connessione..."));
				Start();
				return;
			case kMsgSnapshot:
				Stop();                 // free the single connection for a snapshot
				ReadFields();
				fSnapMode = true; fFrames = 0; fT0 = 0;
				TakeSnapshot();
				return;
			case kMsgToggleAuto:
				if (fAuto->Value() == B_CONTROL_ON) {
					Stop();
					ReadFields();
					fSnapMode = true; fFrames = 0; fT0 = 0;
					fStatus->SetText(B_TRANSLATE("Foto automatiche..."));
					TakeSnapshot();
					fRunner = new BMessageRunner(BMessenger(this), new BMessage(kMsgSnapshot), 1500000);
				} else {
					StopAuto();
				}
				return;
			case kMsgSaveFrame: {
				if (fLastJpeg.empty()) { fStatus->SetText(B_TRANSLATE("Nessun fotogramma da salvare.")); return; }
				char dir[B_PATH_NAME_LENGTH];
				if (find_directory(B_DESKTOP_DIRECTORY, -1, false, dir, sizeof(dir)) != B_OK) {
					fStatus->SetText(B_TRANSLATE("Salvataggio non riuscito.")); return;
				}
				BString name;
				name.SetToFormat("camera-%s-%ld.jpg", fHost.c_str(), (long)time(nullptr));
				BPath path(dir); path.Append(name.String());
				BFile f(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
				if (f.InitCheck() == B_OK && f.Write(fLastJpeg.data(), fLastJpeg.size()) > 0) {
					BString s(B_TRANSLATE("Salvato sulla Scrivania: ")); s << name;
					fStatus->SetText(s.String());
				} else {
					fStatus->SetText(B_TRANSLATE("Salvataggio non riuscito."));
				}
				return;
			}
			case kMsgDesktopWidget:
				ReadFields();
				OpenCameraWidget(fHost, fPort, fSnapPort, fHost);
				return;
			case kMsgOpenWeb: {
				BString url("http://");
				url << fHost.c_str() << "/";
				char* argv[] = {const_cast<char*>(url.String()), nullptr};
				if (be_roster->Launch("application/x-vnd.Be.URL.http", 1, argv) != B_OK)
					be_roster->Launch("text/html", 1, argv);
				return;
			}
		}
		BWindow::MessageReceived(m);
	}

private:
	void ReadFields()
	{
		if (fHostCtl->Text() && fHostCtl->Text()[0]) fHost = fHostCtl->Text();
		fPort = std::atoi(fPortCtl->Text());
		fSnapPort = std::atoi(fSnapCtl->Text());
	}

	void Start()
	{
		fStream = new MjpegStream(fHost, fPort, "/");
		fRunning = true;
		fThread = spawn_thread(Reader, "cam_reader", B_NORMAL_PRIORITY, this);
		if (fThread < 0) { fRunning = false; delete fStream; fStream = nullptr; }
		else resume_thread(fThread);
	}

	void Stop()
	{
		fRunning = false;
		if (fStream != nullptr) fStream->Close();   // unblock a blocked read
		if (fThread >= 0) { status_t st; wait_for_thread(fThread, &st); fThread = -1; }
		delete fStream; fStream = nullptr;
	}

	void StopAuto()
	{
		delete fRunner; fRunner = nullptr;
		if (fAuto->Value() == B_CONTROL_ON) fAuto->SetValue(B_CONTROL_OFF);
	}

	// One-shot snapshot on a worker thread (kept short-lived; no shared stream object).
	void TakeSnapshot()
	{
		SnapJob* job = new SnapJob{fHost, fSnapPort, BMessenger(this)};
		thread_id t = spawn_thread(SnapReader, "cam_snap", B_NORMAL_PRIORITY, job);
		if (t < 0) delete job; else resume_thread(t);
	}

	struct SnapJob { std::string host; int port; BMessenger reply; };

	static int32 SnapReader(void* arg)
	{
		SnapJob* job = static_cast<SnapJob*>(arg);
		std::vector<unsigned char> jpeg;
		std::string err;
		if (MjpegStream::Snapshot(job->host, job->port, "/", jpeg, &err)) {
			BBitmap* b = DecodeJpeg(jpeg.data(), jpeg.size());
			if (b != nullptr) {
				BMessage m(kMsgFrame);
				m.AddPointer("bmp", b);
				m.AddData("jpeg", B_RAW_TYPE, jpeg.data(), jpeg.size());
				if (job->reply.SendMessage(&m) != B_OK) delete b;
			} else {
				BMessage m(kMsgCamError); m.AddString("err", "JPEG non decodificabile"); job->reply.SendMessage(&m);
			}
		} else {
			BMessage m(kMsgCamError); m.AddString("err", err.c_str()); job->reply.SendMessage(&m);
		}
		delete job;
		return 0;
	}

	static int32 Reader(void* arg)
	{
		CameraWindow* w = static_cast<CameraWindow*>(arg);
		BMessenger me(w);
		std::string err;
		if (!w->fStream->Open(&err)) {
			if (w->fRunning) { BMessage m(kMsgCamError); m.AddString("err", err.c_str()); me.SendMessage(&m); }
			return 0;
		}
		std::vector<unsigned char> frame;
		while (w->fRunning) {
			if (!w->fStream->NextFrame(frame, &err)) {
				if (w->fRunning) { BMessage m(kMsgCamError); m.AddString("err", err.c_str()); me.SendMessage(&m); }
				break;
			}
			BBitmap* b = DecodeJpeg(frame.data(), frame.size());
			if (b == nullptr)
				continue;
			BMessage m(kMsgFrame);
			m.AddPointer("bmp", b);
			if (me.SendMessage(&m) != B_OK)
				delete b;   // window gone
		}
		return 0;
	}

	std::string     fHost;
	int             fPort;
	int             fSnapPort;
	CameraView*     fView = nullptr;
	BStringView*    fStatus = nullptr;
	BTextControl*   fHostCtl = nullptr;
	BTextControl*   fPortCtl = nullptr;
	BTextControl*   fSnapCtl = nullptr;
	BCheckBox*      fAuto = nullptr;
	MjpegStream*    fStream = nullptr;
	thread_id       fThread = -1;
	volatile bool   fRunning = false;
	bool            fSnapMode = false;
	int64           fFrames = 0;
	bigtime_t       fT0 = 0;
	std::vector<unsigned char> fLastJpeg;
	BMessageRunner* fRunner = nullptr;
};

// --------------------------------------------------------------------------- desktop replicant
// A self-contained camera view that can be dragged onto the Desktop as a replicant. It reconnects on
// its own (reloaded from this app's image via the "add_on" signature) and can switch between the live
// MJPEG stream and a low-rate snapshot poll from a right-click menu. Snapshot is the default: it is
// light, coexists with other viewers (e.g. Home Assistant), and survives Wi-Fi hiccups.
class EsphomeCameraReplicant : public BView {
public:
	EsphomeCameraReplicant(BRect frame, const std::string& host, int streamPort, int snapPort,
		bool snap, bigtime_t interval)
		: BView(frame, "esphome_cam", B_FOLLOW_ALL, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		  fHost(host), fStreamPort(streamPort), fSnapPort(snapPort), fSnap(snap), fInterval(interval)
	{
		SetViewColor(18, 18, 18);
		BRect r = Bounds();
		BDragger* d = new BDragger(BRect(r.right - 8, r.bottom - 8, r.right, r.bottom), this,
			B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
		AddChild(d);
	}

	EsphomeCameraReplicant(BMessage* archive)
		: BView(archive)
	{
		const char* h = ""; archive->FindString("campiello:host", &h); fHost = h ? h : "";
		archive->FindInt32("campiello:sport", &fStreamPort);
		archive->FindInt32("campiello:nport", &fSnapPort);
		bool snap = true; archive->FindBool("campiello:snap", &snap); fSnap = snap;
		int64 iv = 2000000; archive->FindInt64("campiello:interval", &iv); fInterval = iv;
	}

	~EsphomeCameraReplicant() override { StopAll(); delete fBitmap; }

	// Defined out-of-line below so the compiler always emits the symbol the shelf looks up by name;
	// nothing here calls it, so an inline definition would be dropped at -O2 and the dropped replicant
	// would come back as a grey zombie box.
	static EsphomeCameraReplicant* Instantiate(BMessage* archive);

	status_t Archive(BMessage* into, bool deep) const override
	{
		status_t err = BView::Archive(into, deep);
		if (err != B_OK)
			return err;
		into->AddString("add_on", kSignature);
		into->AddString("class", kReplicantClass);
		into->AddString("campiello:host", fHost.c_str());
		into->AddInt32("campiello:sport", fStreamPort);
		into->AddInt32("campiello:nport", fSnapPort);
		into->AddBool("campiello:snap", fSnap);
		into->AddInt64("campiello:interval", fInterval);
		return B_OK;
	}

	void AttachedToWindow() override { BView::AttachedToWindow(); StartMode(); }
	void DetachedFromWindow() override { StopAll(); BView::DetachedFromWindow(); }

	void Draw(BRect) override
	{
		BRect b = Bounds();
		if (fBitmap != nullptr) {
			BRect s = fBitmap->Bounds();
			float sw = s.Width() + 1, sh = s.Height() + 1, bw = b.Width() + 1, bh = b.Height() + 1;
			float k = std::min(bw / sw, bh / sh);
			float dw = sw * k, dh = sh * k;
			DrawBitmap(fBitmap, s, BRect((bw - dw) / 2, (bh - dh) / 2, (bw - dw) / 2 + dw - 1,
				(bh - dh) / 2 + dh - 1));
		} else {
			SetHighColor(200, 200, 200);
			BString t(fHost.c_str()); t << (fSnap ? "  (foto)" : "  (live)");
			DrawString(t.String(), BPoint(8, b.bottom / 2));
		}
	}

	void MouseDown(BPoint where) override
	{
		BPopUpMenu* menu = new BPopUpMenu("m", false, false);
		BMenuItem* live = new BMenuItem(B_TRANSLATE("Diretta (video)"), new BMessage(kMsgRepStream));
		BMenuItem* snap = new BMenuItem(B_TRANSLATE("Foto ogni 2s"), new BMessage(kMsgRepSnap));
		live->SetMarked(!fSnap);
		snap->SetMarked(fSnap);
		menu->AddItem(live);
		menu->AddItem(snap);
		menu->SetTargetForItems(this);
		ConvertToScreen(&where);
		menu->Go(where, true, true, true);
	}

	void MessageReceived(BMessage* m) override
	{
		switch (m->what) {
			case kMsgRepStream: if (fSnap) { fSnap = false; StopAll(); StartMode(); } return;
			case kMsgRepSnap:   if (!fSnap) { fSnap = true; StopAll(); StartMode(); } return;
			case kMsgRepTick:   TakeSnapshot(); return;
			case kMsgFrame: {
				BBitmap* b = nullptr;
				if (m->FindPointer("bmp", (void**)&b) == B_OK && b != nullptr) {
					delete fBitmap; fBitmap = b; Invalidate();
				}
				return;
			}
			case kMsgCamError:
				return;   // keep the last frame; a snapshot tick will retry
		}
		BView::MessageReceived(m);
	}

private:
	void StartMode()
	{
		if (fSnap) {
			TakeSnapshot();
			delete fRunner;
			fRunner = new BMessageRunner(BMessenger(this), new BMessage(kMsgRepTick), fInterval);
		} else {
			fStream = new MjpegStream(fHost, fStreamPort, "/");
			fRunning = true;
			fThread = spawn_thread(StreamReader, "rep_stream", B_NORMAL_PRIORITY, this);
			if (fThread < 0) { fRunning = false; delete fStream; fStream = nullptr; }
			else resume_thread(fThread);
		}
	}

	void StopAll()
	{
		delete fRunner; fRunner = nullptr;
		fRunning = false;
		if (fStream != nullptr) fStream->Close();
		if (fThread >= 0) { status_t st; wait_for_thread(fThread, &st); fThread = -1; }
		delete fStream; fStream = nullptr;
	}

	void TakeSnapshot()
	{
		SnapJob* job = new SnapJob{fHost, fSnapPort, BMessenger(this)};
		thread_id t = spawn_thread(SnapReader, "rep_snap", B_NORMAL_PRIORITY, job);
		if (t < 0) delete job; else resume_thread(t);
	}

	struct SnapJob { std::string host; int port; BMessenger reply; };

	static int32 SnapReader(void* arg)
	{
		SnapJob* job = static_cast<SnapJob*>(arg);
		std::vector<unsigned char> jpeg;
		std::string err;
		if (MjpegStream::Snapshot(job->host, job->port, "/", jpeg, &err)) {
			BBitmap* b = DecodeJpeg(jpeg.data(), jpeg.size());
			if (b != nullptr) {
				BMessage m(kMsgFrame); m.AddPointer("bmp", b);
				if (job->reply.SendMessage(&m) != B_OK) delete b;
			}
		}
		delete job;
		return 0;
	}

	static int32 StreamReader(void* arg)
	{
		EsphomeCameraReplicant* w = static_cast<EsphomeCameraReplicant*>(arg);
		BMessenger me(w);
		std::string err;
		if (!w->fStream->Open(&err)) return 0;
		std::vector<unsigned char> frame;
		while (w->fRunning) {
			if (!w->fStream->NextFrame(frame, &err)) break;
			BBitmap* b = DecodeJpeg(frame.data(), frame.size());
			if (b == nullptr) continue;
			BMessage m(kMsgFrame); m.AddPointer("bmp", b);
			if (me.SendMessage(&m) != B_OK) delete b;
		}
		return 0;
	}

	std::string     fHost;
	int32           fStreamPort = 8080;
	int32           fSnapPort = 8081;
	bool            fSnap = true;
	bigtime_t       fInterval = 2000000;
	BBitmap*        fBitmap = nullptr;
	MjpegStream*    fStream = nullptr;
	thread_id       fThread = -1;
	volatile bool   fRunning = false;
	BMessageRunner* fRunner = nullptr;
};

// The Desktop shelf reloads a replicant from this app's image via the "add_on" signature and finds
// this Instantiate by class name. Defined out-of-line so it is emitted as a real symbol (see the
// declaration above).
EsphomeCameraReplicant* EsphomeCameraReplicant::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kReplicantClass))
		return nullptr;
	return new EsphomeCameraReplicant(archive);
}

// A small window that hosts a draggable camera replicant: the user drags the corner handle onto the
// Desktop to pin it there.
class ReplicantHolder : public BWindow {
public:
	ReplicantHolder(const std::string& host, int streamPort, int snapPort, const std::string& title)
		: BWindow(BRect(140, 140, 140 + 340, 140 + 300),
			(title + " - widget").c_str(), B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
	{
		// The corner grab handle is only painted while the system-wide "show replicants" flag is on;
		// turn it on so the handle is visible when the user wants to drag the camera out.
		if (!BDragger::AreDraggersDrawn())
			BDragger::ShowAllDraggers();

		EsphomeCameraReplicant* rep = new EsphomeCameraReplicant(
			BRect(0, 0, 319, 239), host, streamPort, snapPort, true, 2000000);
		rep->SetExplicitMinSize(BSize(320, 240));
		BStringView* hint = new BStringView("h",
			B_TRANSLATE("Trascina l'angolo in basso a destra sul Desktop. Click = cambia modo."));
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_INSETS)
			.Add(rep)
			.Add(hint)
		.End();
		CenterOnScreen();
	}
	bool QuitRequested() override { return true; }
};

static void OpenCameraWidget(const std::string& host, int streamPort, int snapPort,
	const std::string& title)
{
	(new ReplicantHolder(host, streamPort, snapPort, title))->Show();
}

// --------------------------------------------------------------------------- window
class EsphomeWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	EsphomeWindow(const std::string& host, const std::string& name,
		const std::vector<std::pair<std::string, std::string>>& txt)
		: BWindow(BRect(100, 100, 480, 420), B_TRANSLATE("Dispositivo ESPHome"), B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		  fHost(host)
	{
		std::string project = TxtGet(txt, "project_name");
		BStringView* title = new BStringView("t",
			!project.empty() ? project.c_str() : (name.empty() ? host.c_str() : name.c_str()));
		BFont f(be_bold_font);
		f.SetSize(f.Size() * 1.2f);
		title->SetFont(&f);

		BTextView* details = new BTextView("d");
		details->MakeEditable(false);
		details->SetExplicitMinSize(BSize(340, 150));
		BString s;
		s << B_TRANSLATE("Nome: ") << (name.empty() ? host.c_str() : name.c_str()) << "\n";
		s << B_TRANSLATE("Indirizzo: ") << host.c_str() << "\n";
		auto add = [&](const char* label, const char* key) {
			std::string v = TxtGet(txt, key);
			if (!v.empty())
				s << label << ": " << v.c_str() << "\n";
		};
		add(B_TRANSLATE("Progetto"), "project_name");
		add(B_TRANSLATE("Versione progetto"), "project_version");
		add(B_TRANSLATE("Versione ESPHome"), "version");
		add(B_TRANSLATE("Scheda"), "board");
		add(B_TRANSLATE("Piattaforma"), "platform");
		add(B_TRANSLATE("Rete"), "network");
		add(B_TRANSLATE("Indirizzo MAC"), "mac");
		details->SetText(s.String());

		BTextView* note = new BTextView("n");
		note->MakeEditable(false);
		note->SetExplicitMinSize(BSize(340, 80));
		note->SetText(B_TRANSLATE(
			"Campiello mostra le informazioni annunciate dal dispositivo e ne apre l'interfaccia web "
			"(se il componente web_server e' attivo). Il controllo completo di entita' e sensori usa "
			"l'API nativa ESPHome (canale protobuf sulla porta 6053, con eventuale cifratura Noise e "
			"password/chiave API): e' un'estensione futura, non ancora implementata."));

		// An ESP32-CAM node exposes an MJPEG stream (esp32_camera_web_server); detect it and offer a
		// live video button (double-click on a camera opens the stream directly, see RefsReceived).
		fIsCamera = LooksLikeCamera(name, project, TxtGet(txt, "board"));
		fTitle = !project.empty() ? project : (name.empty() ? host : name);

		BButton* video = fIsCamera
			? new BButton("v", B_TRANSLATE("Guarda video"), new BMessage(kMsgOpenVideo)) : nullptr;
		BButton* web = new BButton("w", B_TRANSLATE("Apri interfaccia web"), new BMessage(kMsgOpenWeb));
		BButton* close = new BButton("c", B_TRANSLATE("Chiudi"), new BMessage(B_QUIT_REQUESTED));

		BLayoutBuilder::Group<> col(this, B_VERTICAL, B_USE_DEFAULT_SPACING);
		col.SetInsets(B_USE_WINDOW_INSETS).Add(title).Add(details).Add(note);
		auto row = col.AddGroup(B_HORIZONTAL);
		if (video != nullptr)
			row.Add(video);
		row.Add(web).AddGlue().Add(close).End();
		col.End();
		CenterOnScreen();
	}

	void MessageReceived(BMessage* msg) override
	{
		if (msg->what == kMsgOpenVideo) {
			(new CameraWindow(fHost, kDefaultStreamPort, fTitle))->Show();
			return;
		}
		if (msg->what == kMsgOpenWeb) {
			// ESPHome's web_server listens on port 80; the mDNS SRV port is the native API (6053).
			// Hand the URL to the registered http handler (WebPositive claims this MIME type).
			BString url("http://");
			url << fHost.c_str() << "/";
			char* argv[] = {const_cast<char*>(url.String()), nullptr};
			if (be_roster->Launch("application/x-vnd.Be.URL.http", 1, argv) != B_OK)
				be_roster->Launch("text/html", 1, argv);
			return;
		}
		BWindow::MessageReceived(msg);
	}

private:
	std::string fHost;
	std::string fTitle;
	bool        fIsCamera = false;
};

// --------------------------------------------------------------------------- app
class EsphomeApp : public BApplication {
public:
	EsphomeApp(const std::string& host, const std::string& name, bool video = false, int port = 0)
		: BApplication(kSignature), fHost(host), fName(name),
		  fVideo(video), fPort(port > 0 ? port : kDefaultStreamPort) {}

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
			auto txt = ReadTxt(node);
			std::string project = TxtGet(txt, "project_name");
			// A camera opens straight into the live stream; other nodes get the info window.
			if (LooksLikeCamera(name.String(), project, TxtGet(txt, "board"))) {
				std::string title = !project.empty() ? project
					: (name.Length() ? std::string(name.String()) : std::string(host.String()));
				(new CameraWindow(host.String(), kDefaultStreamPort, title))->Show();
			} else {
				(new EsphomeWindow(host.String(), name.String(), txt))->Show();
			}
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert(B_TRANSLATE("Dispositivo ESPHome"),
				B_TRANSLATE("Nessun dispositivo. Aprilo dal vicinato WON, o passa host=<ip>."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		if (fVideo) {
			(new CameraWindow(fHost, fPort, fHost))->Show();
			fShown = true;
			return;
		}
		(new EsphomeWindow(fHost, fName, {}))->Show();
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

	static std::vector<std::pair<std::string, std::string>> ReadTxt(BNode& node)
	{
		std::vector<std::pair<std::string, std::string>> out;
		const std::string prefix = "CAMPIELLO:txt.";
		char name[B_ATTR_NAME_LENGTH];
		node.RewindAttrs();
		while (node.GetNextAttrName(name) == B_OK) {
			std::string n(name);
			if (n.compare(0, prefix.size(), prefix) != 0)
				continue;
			BString value;
			ReadAttr(node, name, value);
			out.push_back({n.substr(prefix.size()), std::string(value.String())});
		}
		return out;
	}

	std::string fHost;
	std::string fName;
	bool fVideo = false;
	int fPort = 0;
	bool fShown = false;
};

#ifndef ESPHOME_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	bool video = false;
	int port = 0;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
		else if (a.compare(0, 6, "video=") == 0) video = (a.substr(6) == "1" || a.substr(6) == "true");
		else if (a.compare(0, 5, "port=") == 0) port = std::atoi(a.c_str() + 5);
	}
	EsphomeApp app(host, name, video, port);
	app.Run();
	return 0;
}
#endif
