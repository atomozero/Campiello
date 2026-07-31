// campiello_lutron.cpp
//
// The Campiello Lutron add-on: for a Lutron Caseta / RA smart bridge discovered via _sleap._tcp it
// shows the device presence and its LEAP connection info. It does NOT control the lights/shades:
// Lutron's Secure LEAP protocol needs a TLS client-certificate pairing (press the button on the
// bridge to enroll) and then LEAP JSON messages over TLS - a heavy follow-up (docs/addons/lutron.md).
// This add-on is informational only (no fake control).
//
// Launched from the WON neighborhood on a double-click of a Lutron bridge (the lutron.handler
// manifest), which passes the device via CAMPIELLO:host/name, the SRV port as CAMPIELLO:port, and the
// mDNS TXT as CAMPIELLO:txt.<key>. No network, no third-party dependency: links only libbe. Italian
// strings.
//
//   g++ -std=c++17 campiello_lutron.cpp -lbe

#include <Application.h>
#include <Alert.h>
#include <Button.h>
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

static const char* const kSignature = "application/x-vnd.Campiello-lutron";
static const int kLeapPort = 8081; // Secure LEAP over TLS

// --------------------------------------------------------------------------- window
class LutronWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	LutronWindow(const std::string& host, int port, const std::string& name,
		const std::vector<std::pair<std::string, std::string>>& txt)
		: BWindow(BRect(100, 100, 470, 390), "Bridge Lutron", B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
	{
		int leap = (port != 0) ? port : kLeapPort;
		BStringView* title = new BStringView("t", name.empty() ? host.c_str() : name.c_str());
		BFont f(be_bold_font);
		f.SetSize(f.Size() * 1.2f);
		title->SetFont(&f);

		BTextView* details = new BTextView("d");
		details->MakeEditable(false);
		details->SetExplicitMinSize(BSize(330, 110));
		BString s;
		s << "Nome: " << (name.empty() ? host.c_str() : name.c_str()) << "\n";
		s << "LEAP sicuro: " << host.c_str() << ":" << leap << " (TLS)\n";
		for (const std::pair<std::string, std::string>& kv : txt)
			s << kv.first.c_str() << ": " << kv.second.c_str() << "\n";
		details->SetText(s.String());

		BTextView* note = new BTextView("n");
		note->MakeEditable(false);
		note->SetExplicitMinSize(BSize(330, 90));
		note->SetText(
			"Il controllo di luci e tapparelle usa il protocollo Lutron Secure LEAP: richiede "
			"l'accoppiamento con un certificato client TLS (si preme il pulsante sul bridge per "
			"registrarsi) e poi messaggi LEAP in JSON su TLS. Questo accoppiamento non e' ancora "
			"disponibile in Campiello. Per ora usa l'app Lutron.");

		BButton* close = new BButton("c", "Chiudi", new BMessage(B_QUIT_REQUESTED));

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
class LutronApp : public BApplication {
public:
	LutronApp(const std::string& host, int port, const std::string& name)
		: BApplication(kSignature), fHost(host), fName(name), fPort(port) {}

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
			int32 port = 0;
			node.ReadAttr("CAMPIELLO:port", B_INT32_TYPE, 0, &port, sizeof(port));
			(new LutronWindow(host.String(), port, name.String(), ReadTxt(node)))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Bridge Lutron",
				"Nessun bridge. Aprilo dal vicinato WON.", "Chiudi"))->Go();
			Quit();
			return;
		}
		(new LutronWindow(fHost, fPort, fName, {}))->Show();
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
	int fPort;
	bool fShown = false;
};

#ifndef LUTRON_NO_MAIN
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
	LutronApp app(host, port, name);
	app.Run();
	return 0;
}
#endif
