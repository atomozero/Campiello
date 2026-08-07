// campiello_airplay.cpp
//
// The Campiello AirPlay add-on: for an AirPlay receiver discovered via _airplay._tcp / _raop._tcp it
// shows the receiver's advertised info (model, device id, source version) and decodes the "features"
// capability bitfield (audio, video, screen mirroring, AirPlay 2, ...) from the mDNS TXT that the WON
// device shortcut carries as CAMPIELLO:txt.<key> attributes. It does NOT stream or mirror: AirPlay
// streaming needs the AirPlay 2 handshake and FairPlay (crypto), a documented follow-up
// (docs/addons/airplay.md).
//
// Launched from the WON neighborhood on a double-click of an AirPlay device (the airplay.handler
// manifest). No network, no crypto: links only libbe. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_airplay.cpp AirplayInfo.cpp -lbe

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Node.h>
#include <StringView.h>
#include <String.h>
#include <TextView.h>
#include <TypeConstants.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>
#include <utility>
#include <vector>

#include "AirplayInfo.h"

using namespace campiello::airplay;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AirPlay"

static const char* const kSignature = "application/x-vnd.Campiello-airplay";

// --------------------------------------------------------------------------- window
class AirplayWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	AirplayWindow(const std::string& host, const std::string& name, const AirplayInfo& info)
		: BWindow(BRect(100, 100, 470, 400), B_TRANSLATE("Ricevitore AirPlay"), B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
	{
		BStringView* title = new BStringView("t", name.empty() ? host.c_str() : name.c_str());
		BFont f(be_bold_font);
		f.SetSize(f.Size() * 1.2f);
		title->SetFont(&f);

		BTextView* details = new BTextView("d");
		details->MakeEditable(false);
		details->SetExplicitMinSize(BSize(330, 130));
		BString s;
		s << B_TRANSLATE("Nome: ") << (name.empty() ? host.c_str() : name.c_str()) << "\n";
		if (!info.model.empty())      s << B_TRANSLATE("Modello: ") << info.model.c_str() << "\n";
		if (!info.srcVersion.empty())
			s << B_TRANSLATE("Versione AirPlay: ") << info.srcVersion.c_str() << "\n";
		if (!info.deviceId.empty())
			s << B_TRANSLATE("ID dispositivo: ") << info.deviceId.c_str() << "\n";
		if (!info.capabilities.empty()) {
			BString caps;
			for (size_t i = 0; i < info.capabilities.size(); ++i)
				caps << (i ? ", " : "") << B_TRANSLATE(info.capabilities[i].c_str());
			s << B_TRANSLATE("Funzioni: ") << caps << "\n";
		}
		s << B_TRANSLATE("Password: ")
			<< (info.passwordRequired ? B_TRANSLATE("richiesta") : B_TRANSLATE("no")) << "\n";
		s << B_TRANSLATE("Indirizzo: ") << host.c_str() << "\n";
		details->SetText(s.String());

		BTextView* note = new BTextView("n");
		note->MakeEditable(false);
		note->SetExplicitMinSize(BSize(330, 80));
		note->SetText(
			B_TRANSLATE("Campiello mostra le informazioni pubbliche del ricevitore. Trasmettere "
				"audio o video (streaming e mirroring) richiede l'handshake AirPlay 2 e FairPlay "
				"(crittografia Apple), non ancora disponibile. Per ora trasmetti da un dispositivo "
				"Apple con AirPlay."));

		BButton* close = new BButton("c", B_TRANSLATE("Chiudi"), new BMessage(B_QUIT_REQUESTED));

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(title)
			.Add(details)
			.Add(note)
			.AddGroup(B_HORIZONTAL).AddGlue().Add(close).End()
		.End();
		CenterOnScreen();
	}
};

// --------------------------------------------------------------------------- app
class AirplayApp : public BApplication {
public:
	AirplayApp(const std::string& host, const std::string& name)
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
			AirplayInfo info = ParseAirplayTxt(ReadTxt(node));
			(new AirplayWindow(host.String(), name.String(), info))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Ricevitore AirPlay",
				B_TRANSLATE("Nessun dispositivo. Aprilo dal vicinato WON."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new AirplayWindow(fHost, fName, AirplayInfo{}))->Show();
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

#ifndef AIRPLAY_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	AirplayApp app(host, name);
	app.Run();
	return 0;
}
#endif
