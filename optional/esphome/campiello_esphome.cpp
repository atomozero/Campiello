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
#include <DataIO.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
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

static const uint32 kMsgOpenWeb   = 'eweb';
static const uint32 kMsgOpenVideo = 'evid';
static const uint32 kMsgFrame     = 'nfrm';
static const uint32 kMsgCamError  = 'cer!';
static const uint32 kMsgReconnect = 'rcon';

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

// A window that streams the ESP32-CAM MJPEG feed on a worker thread.
class CameraWindow : public BWindow {
public:
	CameraWindow(const std::string& host, int port, const std::string& title)
		: BWindow(BRect(120, 120, 120 + 640, 120 + 520), title.c_str(), B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
		  fHost(host), fPort(port)
	{
		fView = new CameraView();
		fStatus = new BStringView("st", B_TRANSLATE("Connessione..."));
		fPortCtl = new BTextControl("p", B_TRANSLATE("Porta:"),
			std::to_string(port).c_str(), nullptr);
		fPortCtl->SetExplicitMaxSize(BSize(140, B_SIZE_UNSET));
		BButton* recon = new BButton("r", B_TRANSLATE("Riconnetti"), new BMessage(kMsgReconnect));

		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(fView)
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.SetInsets(B_USE_SMALL_INSETS)
				.Add(fStatus)
				.AddGlue()
				.Add(fPortCtl)
				.Add(recon)
			.End()
		.End();
		CenterOnScreen();
		Start();
	}

	bool QuitRequested() override { Stop(); return true; }

	void MessageReceived(BMessage* m) override
	{
		switch (m->what) {
			case kMsgFrame: {
				BBitmap* b = nullptr;
				if (m->FindPointer("bmp", (void**)&b) == B_OK && b != nullptr) {
					fView->SetFrame(b);
					++fFrames;
					bigtime_t now = system_time();
					if (fT0 == 0) fT0 = now;
					double sec = (now - fT0) / 1e6;
					if (sec > 0.5) {
						BString s;
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
				Stop();
				fPort = std::atoi(fPortCtl->Text());
				fFrames = 0; fT0 = 0;
				fStatus->SetText(B_TRANSLATE("Connessione..."));
				Start();
				return;
		}
		BWindow::MessageReceived(m);
	}

private:
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

	std::string   fHost;
	int           fPort;
	CameraView*   fView = nullptr;
	BStringView*  fStatus = nullptr;
	BTextControl* fPortCtl = nullptr;
	MjpegStream*  fStream = nullptr;
	thread_id     fThread = -1;
	volatile bool fRunning = false;
	int64         fFrames = 0;
	bigtime_t     fT0 = 0;
};

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

		// An ESP32-CAM node exposes an MJPEG stream (esp32_camera_web_server); detect it from the
		// board or the device name and offer a live video button.
		auto lc = [](std::string x) {
			for (char& c : x) c = (char)std::tolower((unsigned char)c);
			return x;
		};
		std::string board = TxtGet(txt, "board");
		std::string ln = lc(name), lp = lc(project), lb = lc(board);
		fIsCamera = lb.find("cam") != std::string::npos
			|| ln.find("camera") != std::string::npos || ln.find("telecamera") != std::string::npos
			|| lp.find("camera") != std::string::npos || lp.find("telecamera") != std::string::npos;
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
			(new EsphomeWindow(host.String(), name.String(), ReadTxt(node)))->Show();
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
