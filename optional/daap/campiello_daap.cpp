// campiello_daap.cpp
//
// The Campiello DAAP add-on: browses a shared iTunes/OwnTone music library discovered via _daap._tcp
// and lists its tracks (artist, title, album). Network I/O runs on a worker thread so the UI never
// blocks. Streaming/playing a track is a documented follow-up (docs/addons/daap.md).
//
// Launched from the WON neighborhood on a double-click of a music library (the daap.handler manifest),
// which passes the device via CAMPIELLO:host/name, or from the command line with host=<ip>
// [name=<label>]. No third-party dependency (DAAP is plain HTTP + a hand-rolled DMAP parser): links
// only libbe + the network kit. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_daap.cpp DaapClient.cpp -lbe -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <Messenger.h>
#include <Node.h>
#include <ScrollView.h>
#include <StringView.h>
#include <String.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>

#include "DaapClient.h"

using namespace campiello::daap;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DAAP"

static const char* const kSignature = "application/x-vnd.Campiello-daap";

static const uint32 kMsgLoad        = 'dlod';
static const uint32 kMsgTracksReady = 'dtrk';

// --------------------------------------------------------------------------- worker
struct LoadJob { std::string host; BMessenger reply; };
static int32 LoadThread(void* arg)
{
	LoadJob* job = static_cast<LoadJob*>(arg);
	DaapClient d(job->host);
	bool ok = false;
	std::vector<Track> tracks = d.ListTracks(1000, &ok);
	BMessage m(kMsgTracksReady);
	m.AddBool("ok", ok);
	for (const Track& t : tracks) {
		m.AddString("title", t.title.c_str());
		m.AddString("artist", t.artist.c_str());
		m.AddString("album", t.album.c_str());
	}
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class DaapWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	DaapWindow(const std::string& host, const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	void StartLoad();

	std::string  fHost;
	std::string  fName;
	BListView*   fList = nullptr;
	BStringView* fStatus = nullptr;
};

DaapWindow::DaapWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(100, 100, 520, 500), B_TRANSLATE("Libreria musicale"), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fName(name)
{
	BStringView* title = new BStringView("t",
		fName.empty() ? B_TRANSLATE("Libreria musicale") : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	fList = new BListView("tracks");
	BScrollView* scroll = new BScrollView("ls", fList, 0, false, true);
	fStatus = new BStringView("st", B_TRANSLATE("Carico la libreria..."));
	BButton* refresh = new BButton("refresh", B_TRANSLATE("Aggiorna"), new BMessage(kMsgLoad));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(refresh)
		.End()
	.End();

	CenterOnScreen();
	StartLoad();
}

void DaapWindow::StartLoad()
{
	fStatus->SetText(B_TRANSLATE("Carico la libreria..."));
	LoadJob* job = new LoadJob{fHost, BMessenger(this)};
	thread_id t = spawn_thread(LoadThread, "daap_load", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void DaapWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgLoad:
			StartLoad();
			return;
		case kMsgTracksReady: {
			bool ok = false; msg->FindBool("ok", &ok);
			// clear the list
			for (int32 i = fList->CountItems() - 1; i >= 0; --i)
				delete fList->RemoveItem(i);
			if (!ok) {
				fStatus->SetText(B_TRANSLATE("Libreria non raggiungibile o accesso negato."));
				return;
			}
			const char* title;
			int count = 0;
			for (int32 i = 0; msg->FindString("title", i, &title) == B_OK; ++i) {
				const char* artist = ""; msg->FindString("artist", i, &artist);
				const char* album = "";  msg->FindString("album", i, &album);
				BString line;
				if (artist[0]) line << artist << "  -  ";
				line << title;
				if (album[0]) line << "   (" << album << ")";
				fList->AddItem(new BStringItem(line.String()));
				++count;
			}
			BString st;
			st << count << (count == 1 ? B_TRANSLATE(" brano") : B_TRANSLATE(" brani"));
			fStatus->SetText(count == 0 ? B_TRANSLATE("Nessun brano.") : st.String());
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class DaapApp : public BApplication {
public:
	DaapApp(const std::string& host, const std::string& name)
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
			(new DaapWindow(host.String(), name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert(B_TRANSLATE("Libreria musicale"),
				B_TRANSLATE("Nessuna libreria. Apri una libreria dal vicinato WON, o passa host=<ip>."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new DaapWindow(fHost, fName))->Show();
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

#ifndef DAAP_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	DaapApp app(host, name);
	app.Run();
	return 0;
}
#endif
