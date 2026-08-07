// campiello_nfs.cpp
//
// The Campiello NFS add-on: for an NFS server discovered via _nfs._tcp, it lists the server's
// exports (like "showmount -e", over ONC RPC) and shows how to mount one on Haiku. It does NOT mount
// anything itself (mounting/unmounting userlandfs volumes is a KDL hazard; the user does it via
// Tracker or `mount`), and it does not implement an NFS filesystem client (a heavy follow-up,
// docs/addons/nfs.md).
//
// Launched from the WON neighborhood on a double-click of an NFS server (the nfs.handler manifest),
// which passes the device via CAMPIELLO:host/name, or from the command line with host=<ip>
// [name=<label>]. No third-party dependency (RPC by hand): links only libbe + the network kit.
// End-user strings are Italian.
//
//   g++ -std=c++17 campiello_nfs.cpp NfsProbe.cpp -lbe -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Clipboard.h>
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
#include <vector>

#include "NfsProbe.h"

using namespace campiello::nfs;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NFS"

static const char* const kSignature = "application/x-vnd.Campiello-nfs";

static const uint32 kMsgProbe       = 'nprb';
static const uint32 kMsgExportsReady = 'nexp';
static const uint32 kMsgCopy        = 'ncpy';

// --------------------------------------------------------------------------- worker
struct ProbeJob { std::string host; BMessenger reply; };
static int32 ProbeThread(void* arg)
{
	ProbeJob* job = static_cast<ProbeJob*>(arg);
	NfsProbe p(job->host);
	bool ok = false;
	std::vector<std::string> exports = p.ListExports(&ok);
	BMessage m(kMsgExportsReady);
	m.AddBool("ok", ok);
	for (const std::string& e : exports)
		m.AddString("export", e.c_str());
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class NfsWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	NfsWindow(const std::string& host, const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	void StartProbe();
	std::string SelectedAddress() const;

	std::string  fHost;
	BListView*   fList = nullptr;
	std::vector<std::string> fExports;
	BStringView* fStatus = nullptr;
};

NfsWindow::NfsWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(100, 100, 480, 460), B_TRANSLATE("Server NFS"), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host)
{
	BStringView* title = new BStringView("t", name.empty() ? host.c_str() : name.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	BStringView* hint = new BStringView("h", B_TRANSLATE("Condivisioni esportate dal server NFS:"));
	fList = new BListView("exports");
	BScrollView* scroll = new BScrollView("sv", fList, 0, false, true);

	BStringView* howto = new BStringView("ht",
		B_TRANSLATE("Per montarla: Tracker, menu Dischi > Monta, oppure da Terminale\n"
		"mount -t nfs4 -p \"indirizzo\" /boot/home/nfs   (crea prima la cartella)."));
	fStatus = new BStringView("st", fHost.c_str());

	BButton* refresh = new BButton("r", B_TRANSLATE("Aggiorna"), new BMessage(kMsgProbe));
	BButton* copy = new BButton("c", B_TRANSLATE("Copia indirizzo"), new BMessage(kMsgCopy));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(hint)
		.Add(scroll)
		.Add(howto)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(refresh)
			.AddGlue()
			.Add(copy)
		.End()
	.End();

	CenterOnScreen();
	StartProbe();
}

std::string NfsWindow::SelectedAddress() const
{
	int32 idx = fList->CurrentSelection();
	if (idx >= 0 && idx < (int32)fExports.size())
		return fHost + ":" + fExports[idx];
	return fHost;
}

void NfsWindow::StartProbe()
{
	fStatus->SetText(B_TRANSLATE("Interrogo il server..."));
	ProbeJob* job = new ProbeJob{fHost, BMessenger(this)};
	thread_id t = spawn_thread(ProbeThread, "nfs_probe", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void NfsWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgProbe:
			StartProbe();
			return;
		case kMsgExportsReady: {
			bool ok = false; msg->FindBool("ok", &ok);
			for (int32 i = fList->CountItems() - 1; i >= 0; --i)
				delete fList->RemoveItem(i);
			fExports.clear();
			const char* e;
			for (int32 i = 0; msg->FindString("export", i, &e) == B_OK; ++i) {
				fExports.push_back(e);
				fList->AddItem(new BStringItem((fHost + ":" + e).c_str()));
			}
			if (!ok)
				fStatus->SetText(
					B_TRANSLATE("Elenco export non disponibile. Usa l'indirizzo host:/percorso."));
			else if (fExports.empty())
				fStatus->SetText(B_TRANSLATE("Nessuna condivisione esportata."));
			else {
				BString st;
				st << (int)fExports.size()
					<< B_TRANSLATE(" condivisioni. Selezionane una e copia l'indirizzo.");
				fStatus->SetText(st.String());
			}
			return;
		}
		case kMsgCopy: {
			std::string addr = SelectedAddress();
			if (be_clipboard->Lock()) {
				be_clipboard->Clear();
				BMessage* data = be_clipboard->Data();
				if (data != nullptr) {
					data->AddData("text/plain", B_MIME_TYPE, addr.data(), addr.size());
					be_clipboard->Commit();
				}
				be_clipboard->Unlock();
				fStatus->SetText((std::string(B_TRANSLATE("Copiato: ")) + addr).c_str());
			}
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class NfsApp : public BApplication {
public:
	NfsApp(const std::string& host, const std::string& name)
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
			(new NfsWindow(host.String(), name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert(B_TRANSLATE("Server NFS"),
				B_TRANSLATE("Nessun server. Apri un server NFS dal vicinato WON, o passa host=<ip>."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new NfsWindow(fHost, fName))->Show();
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

#ifndef NFS_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	NfsApp app(host, name);
	app.Run();
	return 0;
}
#endif
