// campiello_homekit.cpp
//
// The Campiello HomeKit add-on: for an Apple HomeKit (HAP) accessory discovered via _hap._tcp it
// shows the accessory's advertised info (name, category, pairing state, config/protocol) decoded from
// the mDNS TXT that the WON device shortcut carries as CAMPIELLO:txt.<key> attributes. It does NOT
// control the accessory: HomeKit control needs a paired secure session (SRP pairing + ChaCha20-
// Poly1305 + Ed25519), a heavy crypto module and a documented follow-up (docs/addons/homekit.md).
//
// Launched from the WON neighborhood on a double-click of a HomeKit accessory (the homekit.handler
// manifest). No third-party dependency, no crypto: links only libbe. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_homekit.cpp HapInfo.cpp -lbe

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Node.h>
#include <Roster.h>
#include <StringView.h>
#include <String.h>
#include <TextView.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>
#include <utility>
#include <vector>

#include "HapInfo.h"

using namespace campiello::homekit;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "HomeKit"

static const char* const kSignature = "application/x-vnd.Campiello-homekit";
static const char* const kHueSignature = "application/x-vnd.Campiello-hue";
static const uint32 kMsgOpenHue = 'hkhu';

// A Philips Hue bridge advertises _hap._tcp with model "BSB001" (v1) or "BSB002" (v2). Campiello can
// control it fully through the Hue add-on, so offer that when we recognize the model.
static bool IsHueBridge(const std::string& model)
{
	return model.size() >= 3 && (model[0] == 'B' || model[0] == 'b')
		&& (model[1] == 'S' || model[1] == 's') && (model[2] == 'B' || model[2] == 'b');
}

// --------------------------------------------------------------------------- window
class HomeKitWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	HomeKitWindow(const std::string& host, const std::string& name, const HapInfo& info)
		: BWindow(BRect(100, 100, 460, 400), B_TRANSLATE("Accessorio HomeKit"), B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		  fHost(host)
	{
		bool hueBridge = IsHueBridge(info.name);
		std::string display = !info.name.empty() ? info.name : (name.empty() ? host : name);
		BStringView* title = new BStringView("t", display.c_str());
		BFont f(be_bold_font);
		f.SetSize(f.Size() * 1.2f);
		title->SetFont(&f);

		BTextView* details = new BTextView("d");
		details->MakeEditable(false);
		details->SetExplicitMinSize(BSize(320, 120));
		BString info_s;
		info_s << B_TRANSLATE("Nome: ") << (info.name.empty() ? BString(display.c_str()) : BString(info.name.c_str())) << "\n";
		if (info.categoryId > 0)
			info_s << B_TRANSLATE("Categoria: ") << info.category.c_str() << " (id " << info.categoryId << ")\n";
		if (info.pairedKnown)
			info_s << B_TRANSLATE("Stato: ") << (info.paired ? B_TRANSLATE("Accoppiato") : B_TRANSLATE("Non accoppiato (aggiungibile)")) << "\n";
		if (!info.configNumber.empty())
			info_s << B_TRANSLATE("Configurazione: ") << info.configNumber.c_str() << "\n";
		info_s << B_TRANSLATE("Protocollo HAP: ") << info.protocolVersion.c_str() << "\n";
		if (!info.id.empty())
			info_s << B_TRANSLATE("ID: ") << info.id.c_str() << "\n";
		info_s << B_TRANSLATE("Indirizzo: ") << host.c_str() << "\n";
		details->SetText(info_s.String());

		BTextView* note = new BTextView("n");
		note->MakeEditable(false);
		note->SetExplicitMinSize(BSize(320, 70));
		note->SetText(hueBridge
			? B_TRANSLATE("Questo e' un bridge Philips Hue. Campiello puo' controllarlo direttamente: premi "
			  "\"Apri controllo Hue\" per accoppiarti (pulsante sul bridge) e gestire le luci.")
			: B_TRANSLATE("Campiello mostra le informazioni pubbliche dell'accessorio. Il controllo (accendere, "
			  "regolare) richiede l'accoppiamento sicuro HomeKit (crittografia SRP + ChaCha20-Poly1305), "
			  "non ancora disponibile. Per usarlo ora, aggiungilo con l'app Casa di Apple."));

		BButton* close = new BButton("c", B_TRANSLATE("Chiudi"), new BMessage(B_QUIT_REQUESTED));

		if (hueBridge) {
			BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
				.SetInsets(B_USE_WINDOW_INSETS)
				.Add(title).Add(details).Add(note)
				.AddGroup(B_HORIZONTAL)
					.Add(new BButton("hue", B_TRANSLATE("Apri controllo Hue"), new BMessage(kMsgOpenHue)))
					.AddGlue()
					.Add(close)
				.End()
			.End();
		} else {
			BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
				.SetInsets(B_USE_WINDOW_INSETS)
				.Add(title).Add(details).Add(note)
				.AddGroup(B_HORIZONTAL).AddGlue().Add(close).End()
			.End();
		}
		CenterOnScreen();
	}

	void MessageReceived(BMessage* msg) override
	{
		if (msg->what == kMsgOpenHue) {
			std::string arg = "host=" + fHost;
			const char* argv[1] = { arg.c_str() };
			status_t r = be_roster->Launch(kHueSignature, 1, const_cast<char**>(argv));
			if (r != B_OK && r != B_ALREADY_RUNNING)
				(new BAlert("Hue",
					B_TRANSLATE("Modulo Hue non installato. Installa il pacchetto campiello_hue."),
					B_TRANSLATE("Chiudi")))->Go();
			return;
		}
		BWindow::MessageReceived(msg);
	}

private:
	std::string fHost;
};

// --------------------------------------------------------------------------- app
class HomeKitApp : public BApplication {
public:
	HomeKitApp(const std::string& host, const std::string& name)
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
			HapInfo info = ParseHapTxt(ReadTxt(node));
			(new HomeKitWindow(host.String(), name.String(), info))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Accessorio HomeKit",
				B_TRANSLATE("Nessun accessorio. Aprilo dal vicinato WON."), B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new HomeKitWindow(fHost, fName, HapInfo{}))->Show();
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

	// Collect the CAMPIELLO:txt.<key> attributes into HAP TXT (key, value) pairs.
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

#ifndef HOMEKIT_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	HomeKitApp app(host, name);
	app.Run();
	return 0;
}
#endif
