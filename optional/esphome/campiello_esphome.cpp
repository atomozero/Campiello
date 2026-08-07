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
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Node.h>
#include <Roster.h>
#include <StringView.h>
#include <String.h>
#include <TextView.h>
#include <TypeConstants.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>
#include <utility>
#include <vector>

#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ESPHome"

static const char* const kSignature = "application/x-vnd.Campiello-esphome";

static const uint32 kMsgOpenWeb = 'eweb';

// Look up a TXT value by key.
static std::string TxtGet(const std::vector<std::pair<std::string, std::string>>& txt,
	const char* key)
{
	for (const auto& kv : txt)
		if (kv.first == key)
			return kv.second;
	return "";
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

		BButton* web = new BButton("w", B_TRANSLATE("Apri interfaccia web"), new BMessage(kMsgOpenWeb));
		BButton* close = new BButton("c", B_TRANSLATE("Chiudi"), new BMessage(B_QUIT_REQUESTED));

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(title)
			.Add(details)
			.Add(note)
			.AddGroup(B_HORIZONTAL)
				.Add(web)
				.AddGlue()
				.Add(close)
			.End()
		.End();
		CenterOnScreen();
	}

	void MessageReceived(BMessage* msg) override
	{
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
};

// --------------------------------------------------------------------------- app
class EsphomeApp : public BApplication {
public:
	EsphomeApp(const std::string& host, const std::string& name)
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
	bool fShown = false;
};

#ifndef ESPHOME_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	EsphomeApp app(host, name);
	app.Run();
	return 0;
}
#endif
