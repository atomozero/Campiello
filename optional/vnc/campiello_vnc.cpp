// campiello_vnc.cpp
//
// The Campiello VNC add-on: a small launcher for a remote-desktop host discovered via _rfb._tcp. It
// does NOT implement the RFB protocol (a full VNC client is a heavy follow-up, docs/addons/vnc.md);
// instead it hands the connection to an installed VNC viewer. It builds a "vnc://host:port" URL and,
// if a handler for the vnc scheme is registered, launches it through be_roster. If no viewer is
// installed it shows the connection details (host:port) and how to install one, and can copy the
// address to the clipboard.
//
// Launched from the WON neighborhood on a double-click of a remote desktop (the vnc.handler
// manifest), which passes the device via CAMPIELLO:host/name, or from the command line with
// host=<ip> [name=<label>] [port=<n>]. No third-party dependency: links only libbe. Italian strings.
//
//   g++ -std=c++17 campiello_vnc.cpp -lbe

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Clipboard.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Node.h>
#include <Roster.h>
#include <StringView.h>
#include <String.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>

static const char* const kSignature = "application/x-vnd.Campiello-vnc";
// Haiku registers a URL scheme "vnc" as this MIME type; a viewer that handles vnc:// claims it.
static const char* const kVncUrlType = "application/x-vnd.Be-URL.vnc";

static const uint32 kMsgOpen = 'vopn';
static const uint32 kMsgCopy = 'vcpy';

// Try to open the vnc:// URL in a registered viewer. Returns true if one was launched.
static bool LaunchViewer(const std::string& url)
{
	entry_ref ref;
	if (be_roster->FindApp(kVncUrlType, &ref) != B_OK)
		return false; // no handler for the vnc scheme
	const char* argv[1] = { url.c_str() };
	return be_roster->Launch(&ref, 1, const_cast<char**>(argv), nullptr) == B_OK
		|| be_roster->Launch(kVncUrlType, 1, const_cast<char**>(argv), nullptr) == B_OK;
}

// --------------------------------------------------------------------------- window
class VncWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	VncWindow(const std::string& host, int port, const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	std::string  fUrl;
	std::string  fAddress;
	BStringView* fStatus = nullptr;
};

VncWindow::VncWindow(const std::string& host, int port, const std::string& name)
	: BWindow(BRect(100, 100, 420, 300), "Desktop remoto", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fAddress = host + ":" + std::to_string(port);
	fUrl = "vnc://" + fAddress;

	BStringView* title = new BStringView("t", name.empty() ? "Desktop remoto (VNC)" : name.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	BStringView* addr = new BStringView("a", ("Indirizzo: " + fAddress).c_str());
	fStatus = new BStringView("st", "");

	BButton* open = new BButton("open", "Apri nel visualizzatore", new BMessage(kMsgOpen));
	BButton* copy = new BButton("copy", "Copia indirizzo", new BMessage(kMsgCopy));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(addr)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(copy)
			.AddGlue()
			.Add(open)
		.End()
	.End();

	CenterOnScreen();

	// Try to open a viewer immediately; fall back to showing the details.
	if (LaunchViewer(fUrl))
		fStatus->SetText("Aperto nel visualizzatore VNC.");
	else
		fStatus->SetText("Nessun visualizzatore VNC installato. Installane uno da HaikuDepot\n"
			"(cerca \"VNC\"), poi premi \"Apri nel visualizzatore\".");
}

void VncWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgOpen:
			fStatus->SetText(LaunchViewer(fUrl)
				? "Aperto nel visualizzatore VNC."
				: "Nessun visualizzatore VNC registrato per lo schema vnc://.");
			return;
		case kMsgCopy: {
			if (be_clipboard->Lock()) {
				be_clipboard->Clear();
				BMessage* data = be_clipboard->Data();
				if (data != nullptr) {
					data->AddData("text/plain", B_MIME_TYPE, fAddress.data(), fAddress.size());
					be_clipboard->Commit();
				}
				be_clipboard->Unlock();
				fStatus->SetText("Indirizzo copiato negli appunti.");
			}
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class VncApp : public BApplication {
public:
	VncApp(const std::string& host, int port, const std::string& name)
		: BApplication(kSignature), fHost(host), fPort(port), fName(name) {}

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
			(new VncWindow(host.String(), fPort, name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Desktop remoto",
				"Nessun host. Apri un desktop remoto dal vicinato WON, o passa host=<ip>.",
				"Chiudi"))->Go();
			Quit();
			return;
		}
		(new VncWindow(fHost, fPort, fName))->Show();
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
	int         fPort;
	std::string fName;
	bool fShown = false;
};

#ifndef VNC_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	int port = 5900; // default RFB port
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
		else if (a.compare(0, 5, "port=") == 0) port = std::atoi(a.c_str() + 5);
	}
	VncApp app(host, port, name);
	app.Run();
	return 0;
}
#endif
