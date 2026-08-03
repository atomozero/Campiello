// campiello_eero.cpp
//
// The Campiello eero add-on: for an eero mesh Wi-Fi node discovered via _eero._tcp it shows the
// node's presence and what it advertises over mDNS (its base MAC, and any other TXT keys). It does
// NOT control eero: the eero system has no open local control API - configuration goes through the
// eero mobile app talking to eero's cloud (an account-bound service), so there is nothing on the LAN
// to drive. This add-on is therefore informational only (no fake control); see docs/addons/eero.md.
//
// Launched from the WON neighborhood on a double-click of an eero node (the eero.handler manifest),
// which passes CAMPIELLO:host/name and the mDNS TXT as CAMPIELLO:txt.<key>. No network, no
// third-party dependency: links only libbe. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_eero.cpp -lbe

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

static const char* const kSignature = "application/x-vnd.Campiello-eero";

// --------------------------------------------------------------------------- window
class EeroWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	EeroWindow(const std::string& host, const std::string& name,
		const std::vector<std::pair<std::string, std::string>>& txt)
		: BWindow(BRect(100, 100, 460, 380), "Router mesh eero", B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
	{
		BStringView* title = new BStringView("t", name.empty() ? host.c_str() : name.c_str());
		BFont f(be_bold_font);
		f.SetSize(f.Size() * 1.2f);
		title->SetFont(&f);

		BTextView* details = new BTextView("d");
		details->MakeEditable(false);
		details->SetExplicitMinSize(BSize(320, 110));
		BString s;
		s << "Nome: " << (name.empty() ? host.c_str() : name.c_str()) << "\n";
		s << "Indirizzo: " << host.c_str() << "\n";
		for (const std::pair<std::string, std::string>& kv : txt) {
			const char* label = (kv.first == "base_mac") ? "MAC base" : kv.first.c_str();
			s << label << ": " << kv.second.c_str() << "\n";
		}
		details->SetText(s.String());

		BTextView* note = new BTextView("n");
		note->MakeEditable(false);
		note->SetExplicitMinSize(BSize(320, 100));
		note->SetText(
			"Il sistema eero non offre un'interfaccia di controllo locale: la configurazione (rete, "
			"dispositivi, aggiornamenti) passa dall'app eero e dal cloud eero, legata al tuo account. "
			"Campiello puo' quindi solo mostrare la presenza del nodo mesh sulla rete, non "
			"configurarlo. Per gestirlo, usa l'app eero.");

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
class EeroApp : public BApplication {
public:
	EeroApp(const std::string& host, const std::string& name)
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
			(new EeroWindow(host.String(), name.String(), ReadTxt(node)))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Router mesh eero",
				"Nessun dispositivo. Aprilo dal vicinato WON.", "Chiudi"))->Go();
			Quit();
			return;
		}
		(new EeroWindow(fHost, fName, {}))->Show();
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

#ifndef EERO_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	EeroApp app(host, name);
	app.Run();
	return 0;
}
#endif
