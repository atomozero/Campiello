// campiello_matter.cpp
//
// The Campiello Matter add-on: for a Matter device discovered via _matter._tcp / _matterc._udp it
// shows the device's advertised info (vendor/product, device type, commissioning state, discriminator,
// session timing) decoded from the mDNS TXT that the WON device shortcut carries as CAMPIELLO:txt.<key>
// attributes. It does NOT commission or control the device: that needs PASE/CASE sessions, attestation
// certificates, and Thread/Wi-Fi onboarding (the Matter SDK / crypto), a documented follow-up
// (docs/addons/matter.md).
//
// Launched from the WON neighborhood on a double-click of a Matter device (the matter.handler
// manifest). No third-party dependency, no crypto: links only libbe. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_matter.cpp MatterInfo.cpp -lbe

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Node.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>
#include <TypeConstants.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>
#include <utility>
#include <vector>

#include "MatterInfo.h"

using namespace campiello::matter;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Matter"

static const char* const kSignature = "application/x-vnd.Campiello-matter";

// MatterInfo.cpp's DeviceTypeName()/CommissioningText() return plain Italian literals (it has no
// translation context of its own, like FireTVRemote.cpp). The literals are marked here so catkeys
// collects them, and looked up by content with B_TRANSLATE() where the returned string is drawn
// (below), the same pattern campiello_firetv.cpp uses for FireTVRemote's Cmd labels.
static const char* const kDeviceTypeLabels[] = {
	B_TRANSLATE_MARK("Serratura"),
	B_TRANSLATE_MARK("Aggregatore (bridge)"),
	B_TRANSLATE_MARK("Sensore di contatto"),
	B_TRANSLATE_MARK("Ventilatore"),
	B_TRANSLATE_MARK("Sensore qualita' aria"),
	B_TRANSLATE_MARK("Rilevatore fumo/CO"),
	B_TRANSLATE_MARK("Luce on/off"),
	B_TRANSLATE_MARK("Luce dimmerabile"),
	B_TRANSLATE_MARK("Interruttore luce on/off"),
	B_TRANSLATE_MARK("Interruttore dimmer"),
	B_TRANSLATE_MARK("Interruttore dimmer a colori"),
	B_TRANSLATE_MARK("Sensore di luce"),
	B_TRANSLATE_MARK("Sensore di presenza"),
	B_TRANSLATE_MARK("Presa on/off"),
	B_TRANSLATE_MARK("Presa dimmerabile"),
	B_TRANSLATE_MARK("Luce a temperatura colore"),
	B_TRANSLATE_MARK("Luce a colori"),
	B_TRANSLATE_MARK("Tenda / oscurante"),
	B_TRANSLATE_MARK("Termostato"),
	B_TRANSLATE_MARK("Sensore di temperatura"),
	B_TRANSLATE_MARK("Sensore di umidita'"),
	B_TRANSLATE_MARK("Sconosciuto"),
};
static const char* const kCommissioningLabels[] = {
	B_TRANSLATE_MARK("Non in accoppiamento"),
	B_TRANSLATE_MARK("In accoppiamento (standard)"),
	B_TRANSLATE_MARK("In accoppiamento (avanzato)"),
};

// --------------------------------------------------------------------------- window
class MatterWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	MatterWindow(const std::string& host, const std::string& name, const MatterInfo& info)
		: BWindow(BRect(100, 100, 470, 420), B_TRANSLATE("Dispositivo Matter"), B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
	{
		std::string display = !info.deviceName.empty() ? info.deviceName
			: (name.empty() ? host : name);
		BStringView* title = new BStringView("t", display.c_str());
		BFont f(be_bold_font);
		f.SetSize(f.Size() * 1.2f);
		title->SetFont(&f);

		BTextView* details = new BTextView("d");
		details->MakeEditable(false);
		details->SetExplicitMinSize(BSize(330, 140));
		BString s;
		if (!info.deviceName.empty())
			s << B_TRANSLATE("Nome: ") << info.deviceName.c_str() << "\n";
		if (info.deviceTypeId > 0)
			s << B_TRANSLATE("Tipo: ") << B_TRANSLATE(info.deviceType.c_str())
				<< " (id " << info.deviceTypeId << ")\n";
		if (info.vendorId > 0)
			s << B_TRANSLATE("Vendor / Product: ") << info.vendorId << " / " << info.productId << "\n";
		if (!info.commissioningText.empty())
			s << B_TRANSLATE("Stato: ") << B_TRANSLATE(info.commissioningText.c_str()) << "\n";
		if (!info.discriminator.empty())
			s << B_TRANSLATE("Discriminator: ") << info.discriminator.c_str() << "\n";
		s << B_TRANSLATE("Trasporto: ")
			<< (info.tcpSupported ? B_TRANSLATE("TCP + UDP") : B_TRANSLATE("UDP")) << "\n";
		s << B_TRANSLATE("Indirizzo: ") << host.c_str() << "\n";
		if (s.Length() == 0)
			s << B_TRANSLATE("Dispositivo Matter.\nIndirizzo: ") << host.c_str() << "\n";
		details->SetText(s.String());

		BTextView* note = new BTextView("n");
		note->MakeEditable(false);
		note->SetExplicitMinSize(BSize(330, 80));
		note->SetText(B_TRANSLATE(
			"Campiello mostra le informazioni pubbliche del dispositivo. L'accoppiamento (commissioning) "
			"e il controllo richiedono le sessioni sicure Matter (PASE/CASE), i certificati di "
			"attestazione e l'onboarding Thread/Wi-Fi, non ancora disponibili. Per usarlo ora, "
			"accoppialo con un'app compatibile Matter e il relativo hub."));

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
class MatterApp : public BApplication {
public:
	MatterApp(const std::string& host, const std::string& name)
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
			MatterInfo info = ParseMatterTxt(ReadTxt(node));
			(new MatterWindow(host.String(), name.String(), info))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert(B_TRANSLATE("Dispositivo Matter"),
				B_TRANSLATE("Nessun dispositivo. Aprilo dal vicinato WON."), B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new MatterWindow(fHost, fName, MatterInfo{}))->Show();
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

#ifndef MATTER_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	MatterApp app(host, name);
	app.Run();
	return 0;
}
#endif
