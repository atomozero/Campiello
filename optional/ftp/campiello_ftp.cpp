// campiello_ftp.cpp
//
// The Campiello FTP add-on: a simple browser for an FTP server discovered via _ftp._tcp. It connects
// (anonymous by default, or with credentials), lists a directory, lets the user navigate folders, and
// downloads a file to a chosen location. Each operation uses its own short-lived connection on a
// worker thread, so the UI never blocks. Plain FTP only (FTPS is a follow-up, docs/addons/ftp.md).
//
// Launched from the WON neighborhood on a double-click of an FTP server (the ftp.handler manifest),
// which passes the device via CAMPIELLO:host/name, or from the command line with host=<ip>
// [name=<label>]. No third-party dependency (pure sockets): links only libbe + the network kit.
// End-user strings are Italian.
//
//   g++ -std=c++17 campiello_ftp.cpp FtpClient.cpp -lbe -lnetwork -ltracker

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <Messenger.h>
#include <Node.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <String.h>
#include <TextControl.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>
#include <vector>

#include "FtpClient.h"

using namespace campiello::ftp;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FTP"

static const char* const kSignature = "application/x-vnd.Campiello-ftp";

static const uint32 kMsgConnect     = 'fcon';
static const uint32 kMsgListReady   = 'flst';
static const uint32 kMsgOpen        = 'fopn';
static const uint32 kMsgSaveFile    = 'fsav';
static const uint32 kMsgDownDone    = 'fdwn';

// --------------------------------------------------------------------------- workers
struct ListJob { std::string host, user, pass, path; BMessenger reply; };
static int32 ListThread(void* arg)
{
	ListJob* job = static_cast<ListJob*>(arg);
	FtpClient c(job->host, 21, job->user, job->pass);
	BMessage m(kMsgListReady);
	bool ok = false;
	if (c.Connect()) {
		std::vector<Entry> entries = c.List(job->path, &ok);
		m.AddString("path", job->path.c_str());
		for (const Entry& e : entries) {
			m.AddString("name", e.name.c_str());
			m.AddBool("dir", e.isDir);
			m.AddInt64("size", e.size);
		}
	}
	m.AddBool("ok", ok);
	m.AddString("error", c.LastError().c_str());
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct RetrJob { std::string host, user, pass, remote, local; BMessenger reply; };
static int32 RetrThread(void* arg)
{
	RetrJob* job = static_cast<RetrJob*>(arg);
	FtpClient c(job->host, 21, job->user, job->pass);
	bool ok = c.Connect() && c.Retrieve(job->remote, job->local);
	BMessage m(kMsgDownDone);
	m.AddBool("ok", ok);
	m.AddString("local", job->local.c_str());
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class FtpWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	FtpWindow(const std::string& host, const std::string& name);
	~FtpWindow() override { delete fPanel; }
	void MessageReceived(BMessage* msg) override;

private:
	void StartList(const std::string& path);
	std::string ParentOf(const std::string& path) const;

	std::string  fHost;
	std::string  fPath = "/";
	std::vector<Entry> fEntries; // mirrors the list (index 0 = ".." when not at root)
	BTextControl* fUser = nullptr;
	BTextControl* fPass = nullptr;
	BStringView*  fPathView = nullptr;
	BListView*    fList = nullptr;
	BStringView*  fStatus = nullptr;
	BFilePanel*   fPanel = nullptr;
	std::string   fPendingDownload; // remote path awaiting a save location
};

FtpWindow::FtpWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(100, 100, 520, 520), "FTP", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host)
{
	BStringView* title = new BStringView("t", name.empty() ? host.c_str() : name.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	fUser = new BTextControl("u", B_TRANSLATE("Utente:"), "anonymous", nullptr);
	fPass = new BTextControl("p", B_TRANSLATE("Password:"), "anonymous@", nullptr);
	BButton* connect = new BButton("c", B_TRANSLATE("Connetti"), new BMessage(kMsgConnect));

	fPathView = new BStringView("pv", "/");
	fList = new BListView("entries");
	fList->SetInvocationMessage(new BMessage(kMsgOpen)); // double-click / Enter
	BScrollView* scroll = new BScrollView("sv", fList, 0, false, true);
	fStatus = new BStringView("st", "");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.AddGroup(B_HORIZONTAL)
			.Add(fUser)
			.Add(fPass)
			.Add(connect)
		.End()
		.Add(fPathView)
		.Add(scroll)
		.Add(fStatus)
	.End();

	CenterOnScreen();
	StartList("/");
}

std::string FtpWindow::ParentOf(const std::string& path) const
{
	if (path == "/" || path.empty())
		return "/";
	std::string p = path;
	if (p.back() == '/')
		p.pop_back();
	size_t slash = p.rfind('/');
	return (slash == std::string::npos || slash == 0) ? "/" : p.substr(0, slash);
}

void FtpWindow::StartList(const std::string& path)
{
	fStatus->SetText(B_TRANSLATE("Carico..."));
	ListJob* job = new ListJob{fHost, fUser->Text(), fPass->Text(), path, BMessenger(this)};
	thread_id t = spawn_thread(ListThread, "ftp_list", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void FtpWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgConnect:
			StartList("/");
			return;
		case kMsgListReady: {
			bool ok = false; msg->FindBool("ok", &ok);
			for (int32 i = fList->CountItems() - 1; i >= 0; --i)
				delete fList->RemoveItem(i);
			fEntries.clear();
			if (!ok) {
				const char* err = ""; msg->FindString("error", &err);
				fStatus->SetText(err[0] ? err : B_TRANSLATE("Connessione non riuscita."));
				return;
			}
			const char* path = "/"; msg->FindString("path", &path);
			fPath = path;
			fPathView->SetText(fPath.c_str());

			if (fPath != "/") { // a ".." row to go up
				Entry up; up.name = ".."; up.isDir = true;
				fEntries.push_back(up);
				fList->AddItem(new BStringItem("[..]"));
			}
			// directories first, then files
			const char* name;
			std::vector<Entry> dirs, files;
			for (int32 i = 0; msg->FindString("name", i, &name) == B_OK; ++i) {
				bool dir = false; msg->FindBool("dir", i, &dir);
				int64 size = 0; msg->FindInt64("size", i, &size);
				Entry e; e.name = name; e.isDir = dir; e.size = size;
				(dir ? dirs : files).push_back(e);
			}
			for (const Entry& e : dirs) {
				fEntries.push_back(e);
				fList->AddItem(new BStringItem(("[" + e.name + "]").c_str()));
			}
			for (const Entry& e : files) {
				fEntries.push_back(e);
				BString row(e.name.c_str());
				row << "   (" << (long long)e.size << B_TRANSLATE(" byte)");
				fList->AddItem(new BStringItem(row.String()));
			}
			BString st;
			st << (int)(dirs.size() + files.size()) << B_TRANSLATE(" elementi");
			fStatus->SetText(st.String());
			return;
		}
		case kMsgOpen: {
			int32 idx = fList->CurrentSelection();
			if (idx < 0 || idx >= (int32)fEntries.size())
				return;
			const Entry& e = fEntries[idx];
			if (e.isDir) {
				std::string next = (e.name == "..") ? ParentOf(fPath)
					: (fPath == "/" ? "/" + e.name : fPath + "/" + e.name);
				StartList(next);
			} else {
				fPendingDownload = (fPath == "/" ? "/" + e.name : fPath + "/" + e.name);
				if (fPanel == nullptr)
					fPanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), nullptr,
						B_FILE_NODE, false, new BMessage(kMsgSaveFile));
				fPanel->SetSaveText(e.name.c_str());
				fPanel->Show();
			}
			return;
		}
		case kMsgSaveFile: {
			entry_ref dir;
			const char* nm = "";
			if (msg->FindRef("directory", &dir) != B_OK || msg->FindString("name", &nm) != B_OK)
				return;
			BPath p(&dir);
			if (p.InitCheck() != B_OK)
				return;
			p.Append(nm);
			fStatus->SetText(B_TRANSLATE("Scarico..."));
			RetrJob* job = new RetrJob{fHost, fUser->Text(), fPass->Text(),
				fPendingDownload, p.Path(), BMessenger(this)};
			thread_id t = spawn_thread(RetrThread, "ftp_retr", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgDownDone: {
			bool ok = false; msg->FindBool("ok", &ok);
			const char* local = ""; msg->FindString("local", &local);
			if (ok) { BString s(B_TRANSLATE("Salvato: ")); s << local; fStatus->SetText(s.String()); }
			else fStatus->SetText(B_TRANSLATE("Download non riuscito."));
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class FtpApp : public BApplication {
public:
	FtpApp(const std::string& host, const std::string& name)
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
			(new FtpWindow(host.String(), name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("FTP",
				B_TRANSLATE("Nessun server. Apri un server FTP dal vicinato WON, o passa host=<ip>."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new FtpWindow(fHost, fName))->Show();
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

#ifndef FTP_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	FtpApp app(host, name);
	app.Run();
	return 0;
}
#endif
