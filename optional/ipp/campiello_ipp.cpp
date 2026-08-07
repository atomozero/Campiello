// campiello_ipp.cpp
//
// The Campiello IPP printing add-on: a panel for a network printer discovered via _ipp._tcp. It
// queries the printer (Get-Printer-Attributes) and shows its name, state, and supported formats, and
// lets the user submit a document file (Print-Job). Network I/O runs on worker threads so the UI
// never blocks.
//
// Launched from the WON neighborhood on a double-click of a printer (the ipp.handler manifest), which
// passes the device via CAMPIELLO:host/name, or from the command line with host=<ip> [name=<label>].
// No third-party dependency (IPP is encoded by hand): links only libbe + the network kit. End-user
// strings are Italian.
//
//   g++ -std=c++17 campiello_ipp.cpp IppClient.cpp -lbe -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <String.h>
#include <TextView.h>
#include <Window.h>

#include <fs_attr.h>

#include <cctype>
#include <cstring>
#include <string>

#include "IppClient.h"

using namespace campiello::ipp;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "IPP"

static const char* const kSignature = "application/x-vnd.Campiello-ipp";

static const uint32 kMsgQuery      = 'iqry';
static const uint32 kMsgAttrsReady = 'iatr';
static const uint32 kMsgPick       = 'ipck';
static const uint32 kMsgFileChosen = 'ifch';
static const uint32 kMsgPrintDone  = 'iprd';

// --------------------------------------------------------------------------- helpers
static std::string DocFormatFor(const std::string& path)
{
	std::string ext;
	size_t dot = path.rfind('.');
	if (dot != std::string::npos)
		for (size_t i = dot + 1; i < path.size(); ++i)
			ext += (char)tolower(path[i]);
	if (ext == "pdf")  return "application/pdf";
	if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
	if (ext == "png")  return "image/png";
	if (ext == "ps")   return "application/postscript";
	if (ext == "txt")  return "text/plain";
	return "application/octet-stream"; // let the printer auto-detect
}

static std::string StateText(const std::string& raw)
{
	if (raw == "3") return B_TRANSLATE("Pronta");
	if (raw == "4") return B_TRANSLATE("In stampa");
	if (raw == "5") return B_TRANSLATE("Ferma");
	return raw;
}

// --------------------------------------------------------------------------- workers
struct QueryJob { std::string host, name; BMessenger reply; };
static int32 QueryThread(void* arg)
{
	QueryJob* job = static_cast<QueryJob*>(arg);
	IppClient c(job->host);
	bool ok = false;
	auto attrs = c.GetPrinterAttributes(&ok);
	BMessage m(kMsgAttrsReady);
	m.AddBool("ok", ok);
	for (auto& kv : attrs) {
		m.AddString("k", kv.first.c_str());
		m.AddString("v", kv.second.c_str());
	}
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct PrintJob { std::string host, path, format, jobName; BMessenger reply; };
static int32 PrintThread(void* arg)
{
	PrintJob* job = static_cast<PrintJob*>(arg);
	IppClient c(job->host);
	bool ok = c.PrintFile(job->path, job->format, job->jobName);
	BMessage m(kMsgPrintDone);
	m.AddBool("ok", ok);
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class IppWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	IppWindow(const std::string& host, const std::string& name);
	~IppWindow() override { delete fPanel; }
	void MessageReceived(BMessage* msg) override;

private:
	void StartQuery();

	std::string  fHost;
	std::string  fName;
	BStringView* fSummary = nullptr;
	BTextView*   fDetails = nullptr;
	BStringView* fStatus = nullptr;
	BFilePanel*  fPanel = nullptr;
};

IppWindow::IppWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(100, 100, 480, 520), B_TRANSLATE("Stampante"), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fName(name)
{
	BStringView* title = new BStringView("t",
		fName.empty() ? B_TRANSLATE("Stampante di rete") : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	fSummary = new BStringView("sum", B_TRANSLATE("Interrogo la stampante..."));
	fDetails = new BTextView("det");
	fDetails->MakeEditable(false);
	BScrollView* scroll = new BScrollView("ds", fDetails, 0, false, true);
	fStatus = new BStringView("st", fHost.c_str());
	BButton* print = new BButton("print", B_TRANSLATE("Stampa un file..."), new BMessage(kMsgPick));
	BButton* refresh = new BButton("refresh", B_TRANSLATE("Aggiorna"), new BMessage(kMsgQuery));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(fSummary)
		.Add(scroll)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(refresh)
			.AddGlue()
			.Add(print)
		.End()
	.End();

	CenterOnScreen();
	StartQuery();
}

void IppWindow::StartQuery()
{
	fStatus->SetText(B_TRANSLATE("Interrogo la stampante..."));
	QueryJob* job = new QueryJob{fHost, fName, BMessenger(this)};
	thread_id t = spawn_thread(QueryThread, "ipp_query", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void IppWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgQuery:
			StartQuery();
			return;
		case kMsgAttrsReady: {
			bool ok = false; msg->FindBool("ok", &ok);
			std::string model, state, location, formats;
			BString all;
			const char* k;
			for (int32 i = 0; msg->FindString("k", i, &k) == B_OK; ++i) {
				const char* v = ""; msg->FindString("v", i, &v);
				all << k << ": " << v << "\n";
				std::string key(k);
				if (key == "printer-make-and-model" && model.empty()) model = v;
				else if (key == "printer-name" && model.empty()) model = v;
				else if (key == "printer-state") state = StateText(v);
				else if (key == "printer-location") location = v;
				else if (key == "document-format-supported") formats = v;
			}
			if (!ok && all.Length() == 0) {
				fSummary->SetText(B_TRANSLATE("Stampante non raggiungibile o non IPP."));
				fStatus->SetText(fHost.c_str());
				return;
			}
			BString sum(model.c_str());
			if (!state.empty()) sum << "  -  " << state.c_str();
			if (!location.empty()) sum << "  (" << location.c_str() << ")";
			fSummary->SetText(sum.String());
			fDetails->SetText(all.String());
			fStatus->SetText(formats.empty() ? B_TRANSLATE("Pronta")
				: (std::string(B_TRANSLATE("Formati: ")) + formats).c_str());
			return;
		}
		case kMsgPick: {
			if (fPanel == nullptr) {
				fPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr,
					B_FILE_NODE, false, new BMessage(kMsgFileChosen));
			}
			fPanel->Show();
			return;
		}
		case kMsgFileChosen: {
			entry_ref ref;
			if (msg->FindRef("refs", &ref) != B_OK)
				return;
			BPath path(&ref);
			if (path.InitCheck() != B_OK)
				return;
			std::string p = path.Path();
			fStatus->SetText(B_TRANSLATE("Invio in stampa..."));
			PrintJob* job = new PrintJob{fHost, p, DocFormatFor(p), ref.name, BMessenger(this)};
			thread_id t = spawn_thread(PrintThread, "ipp_print", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgPrintDone: {
			bool ok = false; msg->FindBool("ok", &ok);
			fStatus->SetText(ok ? B_TRANSLATE("Inviato in stampa.") : B_TRANSLATE("Stampa non riuscita."));
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class IppApp : public BApplication {
public:
	IppApp(const std::string& host, const std::string& name)
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
			(new IppWindow(host.String(), name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert(B_TRANSLATE("Stampante"),
				B_TRANSLATE("Nessuna stampante. Apri una stampante dal vicinato WON, o passa host=<ip>."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new IppWindow(fHost, fName))->Show();
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

#ifndef IPP_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	IppApp app(host, name);
	app.Run();
	return 0;
}
#endif
