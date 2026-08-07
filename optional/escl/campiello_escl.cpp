// campiello_escl.cpp
//
// The Campiello eSCL scanning add-on: a panel for a network scanner discovered via _uscan._tcp. It
// queries the scanner (ScannerCapabilities) and shows its model and supported modes/formats, and
// scans a page to a file the user chooses (color, resolution, format selectable). Network I/O runs
// on worker threads so the UI never blocks.
//
// Launched from the WON neighborhood on a double-click of a scanner (the escl.handler manifest), which
// passes the device via CAMPIELLO:host/name, or from the command line with host=<ip> [name=<label>].
// No third-party dependency (eSCL is plain HTTP/XML): links only libbe + the network kit. End-user
// strings are Italian.
//
//   g++ -std=c++17 campiello_escl.cpp EsclClient.cpp -lbe -lnetwork -ltracker

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Node.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <String.h>
#include <Window.h>

#include <fs_attr.h>

#include <string>

#include "EsclClient.h"

using namespace campiello::escl;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "eSCL"

static const char* const kSignature = "application/x-vnd.Campiello-escl";

static const uint32 kMsgQuery      = 'sqry';
static const uint32 kMsgCapsReady  = 'scap';
static const uint32 kMsgPick       = 'spck';
static const uint32 kMsgSave       = 'ssav';
static const uint32 kMsgScanDone   = 'sdon';

// --------------------------------------------------------------------------- workers
struct QueryJob { std::string host; BMessenger reply; };
static int32 QueryThread(void* arg)
{
	QueryJob* job = static_cast<QueryJob*>(arg);
	EsclClient c(job->host);
	bool ok = false;
	auto caps = c.GetCapabilities(&ok);
	BMessage m(kMsgCapsReady);
	m.AddBool("ok", ok);
	for (auto& kv : caps) {
		m.AddString("k", kv.first.c_str());
		m.AddString("v", kv.second.c_str());
	}
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct ScanJob { std::string host, dest; ScanOptions opt; BMessenger reply; };
static int32 ScanThread(void* arg)
{
	ScanJob* job = static_cast<ScanJob*>(arg);
	EsclClient c(job->host);
	bool ok = c.Scan(job->dest, job->opt);
	BMessage m(kMsgScanDone);
	m.AddBool("ok", ok);
	m.AddString("dest", job->dest.c_str());
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class EsclWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	EsclWindow(const std::string& host, const std::string& name);
	~EsclWindow() override { delete fPanel; }
	void MessageReceived(BMessage* msg) override;

private:
	void StartQuery();
	ScanOptions CurrentOptions() const;
	int SelectedInt(BMenuField* f, int fallback) const;
	int32 SelectedIndex(BMenuField* f) const; // index into the field's item list, or 0

	std::string  fHost;
	std::string  fName;
	BStringView* fSummary = nullptr;
	BStringView* fStatus = nullptr;
	BMenuField*  fColor = nullptr;
	BMenuField*  fRes = nullptr;
	BMenuField*  fFormat = nullptr;
	BFilePanel*  fPanel = nullptr;
};

static BMenuField* MakeMenu(const char* name, const char* label, const char* const* items, int n)
{
	BPopUpMenu* menu = new BPopUpMenu(items[0]);
	for (int i = 0; i < n; ++i) {
		BMenuItem* it = new BMenuItem(items[i], nullptr);
		if (i == 0) it->SetMarked(true);
		menu->AddItem(it);
	}
	return new BMenuField(name, label, menu);
}

EsclWindow::EsclWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(100, 100, 440, 380), B_TRANSLATE("Scanner"), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fName(name)
{
	BStringView* title = new BStringView("t",
		fName.empty() ? B_TRANSLATE("Scanner di rete") : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	fSummary = new BStringView("sum", B_TRANSLATE("Interrogo lo scanner..."));

	// Fixed order: color menu is {Colore, Scala di grigi}, format menu is {JPEG, PDF}; the index
	// (not the translated label) drives CurrentOptions() and the save-filename suggestion below.
	const char* colors[] = {B_TRANSLATE("Colore"), B_TRANSLATE("Scala di grigi")};
	const char* resos[]  = {"300 dpi", "150 dpi", "600 dpi"};
	const char* fmts[]   = {"JPEG", "PDF"};
	fColor  = MakeMenu("color", B_TRANSLATE("Colore:"), colors, 2);
	fRes    = MakeMenu("res", B_TRANSLATE("Risoluzione:"), resos, 3);
	fFormat = MakeMenu("fmt", B_TRANSLATE("Formato:"), fmts, 2);

	fStatus = new BStringView("st", fHost.c_str());
	BButton* scan = new BButton("scan", B_TRANSLATE("Scansiona..."), new BMessage(kMsgPick));
	BButton* refresh = new BButton("refresh", B_TRANSLATE("Aggiorna"), new BMessage(kMsgQuery));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(fSummary)
		.Add(fColor)
		.Add(fRes)
		.Add(fFormat)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(refresh)
			.AddGlue()
			.Add(scan)
		.End()
	.End();

	CenterOnScreen();
	StartQuery();
}

int EsclWindow::SelectedInt(BMenuField* field, int fallback) const
{
	if (field == nullptr || field->Menu() == nullptr) return fallback;
	BMenuItem* it = field->Menu()->FindMarked();
	if (it == nullptr) return fallback;
	return std::atoi(it->Label());
}

// The item's position in the menu, not its (possibly translated) label: the label is only for
// display, so decisions based on which item is selected must not depend on its text.
int32 EsclWindow::SelectedIndex(BMenuField* field) const
{
	if (field == nullptr || field->Menu() == nullptr) return 0;
	BMenuItem* it = field->Menu()->FindMarked();
	if (it == nullptr) return 0;
	return field->Menu()->IndexOf(it);
}

ScanOptions EsclWindow::CurrentOptions() const
{
	ScanOptions o;
	o.colorMode = (SelectedIndex(fColor) == 1) ? "Grayscale8" : "RGB24"; // 0 Colore, 1 Scala di grigi
	o.resolution = SelectedInt(fRes, 300);
	o.format = (SelectedIndex(fFormat) == 1) ? "application/pdf" : "image/jpeg"; // 0 JPEG, 1 PDF
	// A4 in pixels at the chosen resolution (8.27 x 11.69 inches).
	o.widthPx  = (int)(8.27 * o.resolution);
	o.heightPx = (int)(11.69 * o.resolution);
	return o;
}

void EsclWindow::StartQuery()
{
	fStatus->SetText(B_TRANSLATE("Interrogo lo scanner..."));
	QueryJob* job = new QueryJob{fHost, BMessenger(this)};
	thread_id t = spawn_thread(QueryThread, "escl_query", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void EsclWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgQuery:
			StartQuery();
			return;
		case kMsgCapsReady: {
			bool ok = false; msg->FindBool("ok", &ok);
			if (!ok) {
				fSummary->SetText(B_TRANSLATE("Scanner non raggiungibile o non eSCL."));
				fStatus->SetText(fHost.c_str());
				return;
			}
			BString sum;
			const char* k;
			for (int32 i = 0; msg->FindString("k", i, &k) == B_OK; ++i) {
				const char* v = ""; msg->FindString("v", i, &v);
				if (sum.Length() > 0) sum << "\n";
				sum << k << ": " << v;
			}
			fSummary->SetText(sum.Length() > 0 ? sum.String() : B_TRANSLATE("Scanner pronto."));
			fStatus->SetText(B_TRANSLATE("Pronto."));
			return;
		}
		case kMsgPick: {
			if (fPanel == nullptr)
				fPanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this), nullptr,
					B_FILE_NODE, false, new BMessage(kMsgSave));
			std::string suggested = (SelectedIndex(fFormat) == 1) // 0 JPEG, 1 PDF
				? "scansione.pdf" : "scansione.jpg";
			fPanel->SetSaveText(suggested.c_str());
			fPanel->Show();
			return;
		}
		case kMsgSave: {
			entry_ref dir;
			const char* name = "";
			if (msg->FindRef("directory", &dir) != B_OK || msg->FindString("name", &name) != B_OK)
				return;
			BPath path(&dir);
			if (path.InitCheck() != B_OK)
				return;
			path.Append(name);
			fStatus->SetText(B_TRANSLATE("Scansione in corso..."));
			ScanJob* job = new ScanJob{fHost, path.Path(), CurrentOptions(), BMessenger(this)};
			thread_id t = spawn_thread(ScanThread, "escl_scan", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgScanDone: {
			bool ok = false; msg->FindBool("ok", &ok);
			const char* dest = ""; msg->FindString("dest", &dest);
			if (ok) {
				BString s(B_TRANSLATE("Salvato: "));
				s << dest;
				fStatus->SetText(s.String());
			} else {
				fStatus->SetText(B_TRANSLATE("Scansione non riuscita."));
			}
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class EsclApp : public BApplication {
public:
	EsclApp(const std::string& host, const std::string& name)
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
			(new EsclWindow(host.String(), name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert(B_TRANSLATE("Scanner"),
				B_TRANSLATE("Nessuno scanner. Apri uno scanner dal vicinato WON, o passa host=<ip>."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new EsclWindow(fHost, fName))->Show();
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

#ifndef ESCL_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	EsclApp app(host, name);
	app.Run();
	return 0;
}
#endif
