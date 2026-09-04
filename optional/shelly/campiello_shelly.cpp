// campiello_shelly.cpp
//
// The Campiello Shelly add-on: a control panel for a Shelly smart relay/plug discovered on the
// network, over its LOCAL HTTP API (no cloud), for both device generations (gen1 REST and gen2 RPC,
// auto-detected). It lists the device's channels with an on/off button and a live power readout,
// draws a live watt graph, and opens the device web UI. Network I/O runs on worker threads so the UI
// never blocks; a message runner re-polls the device every few seconds to keep the graph moving.
//
// Launched from the WON neighborhood on a double-click of a Shelly (the shelly.handler manifest),
// which passes CAMPIELLO:host/name/port and the mDNS TXT as CAMPIELLO:txt.<key>; also runnable from
// the command line with host=<ip> [name=<label>] [port=80] [gen=2]. OPTIONAL, Haiku: links only the
// network kit (no third-party dependency), so it stays MIT-clean. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_shelly.cpp ShellyClient.cpp -lbe -lnetwork -llocalestub

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Dragger.h>
#include <Entry.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Node.h>
#include <Roster.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <String.h>
#include <Window.h>

#include <fs_attr.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "ShellyClient.h"

using namespace campiello::shelly;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Shelly"

static const char* const kSignature = "application/x-vnd.Campiello-shelly";

static const uint32 kMsgRefresh   = 'sref';
static const uint32 kMsgReady     = 'srdy';
static const uint32 kMsgToggle    = 'stog';
static const uint32 kMsgCmdDone   = 'scmd';
static const uint32 kMsgOpenWeb   = 'sweb';
static const uint32 kMsgOpenDesktop = 'sdsk';
static const uint32 kMsgRepTick   = 'srtk';

static const bigtime_t kPollInterval = 3000000; // 3 s
static const size_t kGraphSamples = 120;        // ~6 minutes at a 3 s poll

// The Desktop replicant class name. The shelf reloads it from this app's image via the "add_on"
// signature and finds Instantiate by building the mangled symbol from this string, so it MUST match
// the real C++ class exactly - the class lives in the global namespace, so no namespace prefix.
static const char* const kReplicantClass = "ShellyGraphReplicant";

// --------------------------------------------------------------------------- shared graph drawing
// A "nice" Y-axis ceiling (1/2/5 x 10^n) at or above v.
static float NiceCeil(float v)
{
	if (v <= 0) return 1.0f;
	float p = std::pow(10.0f, std::floor(std::log10(v)));
	float f = v / p;
	float nf = (f <= 1) ? 1 : (f <= 2) ? 2 : (f <= 5) ? 5 : 10;
	return nf * p;
}

// Draw a rolling watt line chart into `view`'s bounds: frame, grid, blue line, and Y-axis labels
// (full scale at the top, 0 at the bottom). When `title` is set (the replicant), it is drawn on a
// reserved top strip; the window instead carries the current value in its own section label. Shared
// by PowerGraph (in the window) and ShellyGraphReplicant (on the Desktop).
static void DrawWattHistory(BView* view, const std::deque<float>& data, const char* title)
{
	BRect b = view->Bounds();
	const float pad = 4.0f;
	const float topInset = (title != nullptr) ? 16.0f : 4.0f;
	BRect plot(b.left + pad, b.top + topInset, b.right - pad, b.bottom - pad);

	view->SetHighColor(tint_color(ui_color(B_CONTROL_BORDER_COLOR), B_LIGHTEN_1_TINT));
	view->StrokeRect(b);

	float scale = 1.0f;
	for (float v : data)
		if (!std::isnan(v) && v > scale) scale = v;
	scale = NiceCeil(scale);

	// Grid.
	view->SetHighColor(tint_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR), B_DARKEN_1_TINT));
	for (int i = 0; i <= 2; ++i) {
		float y = plot.bottom - (i / 2.0f) * plot.Height();
		view->StrokeLine(BPoint(plot.left, y), BPoint(plot.right, y));
	}

	// Line.
	if (data.size() >= 2) {
		view->SetHighColor(60, 130, 220);
		size_t n = data.size();
		BPoint prev;
		bool havePrev = false;
		for (size_t i = 0; i < n; ++i) {
			float v = data[i];
			if (std::isnan(v)) { havePrev = false; continue; }
			float x = plot.left + (float)i / (n - 1) * plot.Width();
			float y = plot.bottom - (v / scale) * plot.Height();
			BPoint p(x, y);
			if (havePrev) view->StrokeLine(prev, p);
			prev = p;
			havePrev = true;
		}
	}

	// Y-axis labels.
	view->SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	char full[32];
	std::snprintf(full, sizeof(full), "%g W", scale);
	view->DrawString(full, BPoint(plot.left + 2, plot.top + 12));
	view->DrawString("0 W", BPoint(plot.left + 2, plot.bottom - 3));

	if (title != nullptr)
		view->DrawString(title, BPoint(b.left + 4, b.top + 12));
}

// --------------------------------------------------------------------------- live watt graph
// A rolling line chart of total active power, embedded in the control window.
class PowerGraph : public BView {
public:
	PowerGraph()
		: BView("graph", B_WILL_DRAW | B_FRAME_EVENTS)
	{
		SetViewColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
		SetExplicitMinSize(BSize(220, 130));
	}

	// Append a sample (watts). A NaN means "no reading this tick" and breaks the line (a gap).
	void Push(float watts)
	{
		fData.push_back(watts);
		if (fData.size() > kGraphSamples)
			fData.pop_front();
		Invalidate();
	}

	void Draw(BRect) override { DrawWattHistory(this, fData, nullptr); }

private:
	std::deque<float> fData;
};

// --------------------------------------------------------------------------- workers
struct RefreshJob { std::string host; int port; int gen; BMessenger reply; };
static int32 RefreshThread(void* arg)
{
	RefreshJob* job = static_cast<RefreshJob*>(arg);
	ShellyClient c(job->host, job->port, job->gen);
	ShellyInfo info;
	bool ok = c.FetchInfo(info);
	std::vector<ShellyChannel> chans;
	if (ok)
		ok = c.FetchChannels(chans);

	BMessage m(kMsgReady);
	m.AddBool("ok", ok);
	m.AddInt32("gen", info.gen);
	m.AddString("model", info.model.c_str());
	m.AddString("app", info.app.c_str());
	m.AddString("fw", info.fw.c_str());
	m.AddBool("auth", info.authEnabled);
	for (const ShellyChannel& ch : chans) {
		m.AddInt32("ch.id", ch.id);
		m.AddString("ch.name", ch.name.c_str());
		m.AddBool("ch.ctl", ch.controllable);
		m.AddBool("ch.on", ch.on);
		m.AddBool("ch.hp", ch.hasPower);
		m.AddDouble("ch.pw", ch.power);
		m.AddDouble("ch.v", ch.voltage);
		m.AddDouble("ch.wh", ch.total);
	}
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct SetJob { std::string host; int port; int gen; int id; bool on; BMessenger reply; };
static int32 SetThread(void* arg)
{
	SetJob* job = static_cast<SetJob*>(arg);
	ShellyClient c(job->host, job->port, job->gen);
	bool ok = c.SetOn(job->id, job->on);
	BMessage m(kMsgCmdDone);
	m.AddBool("ok", ok);
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- Desktop replicant
// A self-contained watt graph that can be dragged onto the Desktop as a replicant. It re-polls the
// device on its own (reloaded from this app's image via the "add_on" signature) and keeps its own
// rolling history. It reuses RefreshThread (posting kMsgReady) and DrawWattHistory, so it stays in
// sync with the in-window graph. The archive carries host/port/gen/name, so it survives a reboot.
class ShellyGraphReplicant : public BView {
public:
	ShellyGraphReplicant(BRect frame, const std::string& host, int port, int gen,
		const std::string& name)
		: BView(frame, "shelly_graph", B_FOLLOW_ALL, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		  fHost(host), fPort(port), fGen(gen), fName(name)
	{
		SetViewColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
		BRect r = Bounds();
		BDragger* d = new BDragger(BRect(r.right - 8, r.bottom - 8, r.right, r.bottom), this,
			B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
		AddChild(d);
	}

	ShellyGraphReplicant(BMessage* archive)
		: BView(archive)
	{
		const char* h = ""; archive->FindString("campiello:host", &h); fHost = h ? h : "";
		archive->FindInt32("campiello:port", &fPort);
		archive->FindInt32("campiello:gen", &fGen);
		const char* n = ""; archive->FindString("campiello:name", &n); fName = n ? n : "";
	}

	~ShellyGraphReplicant() override { StopPoll(); }

	// Defined out-of-line below so the compiler always emits the symbol: the shelf finds it by name
	// in the app image, and nothing in this program calls it, so an inline definition would be
	// discarded at -O2 and the replicant would come back as a grey zombie box on the Desktop.
	static ShellyGraphReplicant* Instantiate(BMessage* archive);

	status_t Archive(BMessage* into, bool deep) const override
	{
		status_t err = BView::Archive(into, deep);
		if (err != B_OK)
			return err;
		into->AddString("add_on", kSignature);
		into->AddString("class", kReplicantClass);
		into->AddString("campiello:host", fHost.c_str());
		into->AddInt32("campiello:port", fPort);
		into->AddInt32("campiello:gen", fGen);
		into->AddString("campiello:name", fName.c_str());
		return B_OK;
	}

	void AttachedToWindow() override { BView::AttachedToWindow(); StartPoll(); }
	void DetachedFromWindow() override { StopPoll(); BView::DetachedFromWindow(); }

	void Draw(BRect) override
	{
		char title[80];
		const char* nm = fName.empty() ? fHost.c_str() : fName.c_str();
		if (fHavePower)
			std::snprintf(title, sizeof(title), "%s  %.1f W", nm, fLast);
		else
			std::snprintf(title, sizeof(title), "%s", nm);
		DrawWattHistory(this, fData, title);
	}

	void MessageReceived(BMessage* m) override
	{
		switch (m->what) {
			case kMsgRepTick:
				Poll();
				return;
			case kMsgReady: {
				fInFlight = false;
				bool ok = false; m->FindBool("ok", &ok);
				if (!ok) {
					fHavePower = false;
					fData.push_back(NAN);
				} else {
					int32 gen = 0;
					if (m->FindInt32("gen", &gen) == B_OK && gen >= 1) fGen = gen;
					double total = 0; bool any = false; double pw = 0;
					for (int32 i = 0; m->FindDouble("ch.pw", i, &pw) == B_OK; ++i) {
						bool hp = false; m->FindBool("ch.hp", i, &hp);
						if (hp) { total += pw; any = true; }
					}
					fHavePower = any;
					fLast = (float)total;
					fData.push_back(any ? (float)total : NAN);
				}
				if (fData.size() > kGraphSamples) fData.pop_front();
				Invalidate();
				return;
			}
		}
		BView::MessageReceived(m);
	}

private:
	void Poll()
	{
		if (fInFlight)
			return; // do not pile up workers if the device is slow/unreachable
		RefreshJob* job = new RefreshJob{fHost, (int)fPort, (int)fGen, BMessenger(this)};
		thread_id t = spawn_thread(RefreshThread, "shelly_rep_poll", B_NORMAL_PRIORITY, job);
		if (t < 0) { delete job; return; }
		fInFlight = true;
		resume_thread(t);
	}

	void StartPoll()
	{
		Poll(); // immediate first sample
		delete fRunner;
		fRunner = new BMessageRunner(BMessenger(this), new BMessage(kMsgRepTick), kPollInterval);
	}

	void StopPoll()
	{
		delete fRunner;
		fRunner = nullptr;
		// An in-flight poll thread may still post kMsgReady; BMessenger delivery to a gone handler
		// fails harmlessly, so we do not block DetachedFromWindow waiting for it.
	}

	std::string     fHost;
	int32           fPort = 80;
	int32           fGen = 0;
	std::string     fName;
	bool            fInFlight = false;
	bool            fHavePower = false;
	float           fLast = 0.0f;
	std::deque<float> fData;
	BMessageRunner* fRunner = nullptr;
};

// The Desktop shelf reloads a replicant from this app's image via the "add_on" signature and finds
// this Instantiate by class name. Defined out-of-line so it is emitted as a real symbol (see the
// declaration above).
ShellyGraphReplicant* ShellyGraphReplicant::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kReplicantClass))
		return nullptr;
	return new ShellyGraphReplicant(archive);
}

// A small window that hosts a draggable watt-graph replicant: the user drags the corner handle onto
// the Desktop to pin it there.
class ReplicantHolder : public BWindow {
public:
	ReplicantHolder(const std::string& host, int port, int gen, const std::string& name)
		: BWindow(BRect(160, 160, 160 + 320, 160 + 210),
			(((name.empty() ? std::string("Shelly") : name)) + " - widget").c_str(), B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
	{
		// The corner grab handle is only painted when the system-wide "show replicants" flag is on;
		// if the user has it off the handle is invisible and the widget looks undraggable. Turn it on
		// so the handle is there exactly when they want to drag the graph out to the Desktop.
		if (!BDragger::AreDraggersDrawn())
			BDragger::ShowAllDraggers();

		ShellyGraphReplicant* rep = new ShellyGraphReplicant(
			BRect(0, 0, 299, 149), host, port, gen, name);
		rep->SetExplicitMinSize(BSize(300, 150));
		BStringView* hint = new BStringView("h",
			B_TRANSLATE("Trascina la maniglia in basso a destra sul Desktop."));
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_INSETS)
			.Add(rep)
			.Add(hint)
		.End();
		CenterOnScreen();
	}
	bool QuitRequested() override { return true; }
};

static void OpenGraphWidget(const std::string& host, int port, int gen, const std::string& name)
{
	(new ReplicantHolder(host, port, gen, name))->Show();
}

// --------------------------------------------------------------------------- window
class ShellyWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	ShellyWindow(const std::string& host, const std::string& name, int port, int gen);
	~ShellyWindow() override { delete fRunner; }
	void MessageReceived(BMessage* msg) override;

private:
	void BuildChrome();          // one-time persistent layout
	void UpdateFromReady(BMessage* ready);
	void StartRefresh();

	std::string    fHost;
	std::string    fName;
	int            fPort;
	int            fGen;
	bool           fInFlight = false;

	BStringView*   fStatus  = nullptr;
	BStringView*   fGraphLabel = nullptr;
	PowerGraph*    fGraph   = nullptr;
	BGroupView*    fChannels = nullptr;
	BStringView*   fAuthNote = nullptr;
	BMessageRunner* fRunner = nullptr;
};

ShellyWindow::ShellyWindow(const std::string& host, const std::string& name, int port, int gen)
	: BWindow(BRect(100, 100, 480, 460), "Shelly",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fName(name), fPort(port <= 0 ? 80 : port), fGen(gen)
{
	BuildChrome();
	StartRefresh();
	// Keep the graph moving without the user pressing Aggiorna.
	fRunner = new BMessageRunner(BMessenger(this), new BMessage(kMsgRefresh), kPollInterval);
	CenterOnScreen();
}

void ShellyWindow::BuildChrome()
{
	BStringView* title = new BStringView("t",
		fName.empty() ? B_TRANSLATE("Shelly") : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);

	BButton* refresh = new BButton("refresh", B_TRANSLATE("Aggiorna"), new BMessage(kMsgRefresh));
	BButton* web = new BButton("web", B_TRANSLATE("Apri web"), new BMessage(kMsgOpenWeb));
	BButton* desk = new BButton("desk", B_TRANSLATE("Desktop"), new BMessage(kMsgOpenDesktop));

	fStatus = new BStringView("st", B_TRANSLATE("Carico lo stato..."));
	fGraphLabel = new BStringView("gl", B_TRANSLATE("Potenza (W)"));
	fGraph = new PowerGraph();
	fChannels = new BGroupView(B_VERTICAL);
	fAuthNote = new BStringView("auth", "");
	fAuthNote->Hide();

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(title)
			.AddGlue()
			.Add(desk)
			.Add(web)
			.Add(refresh)
		.End()
		.Add(fStatus)
		.Add(fGraphLabel)
		.Add(fGraph)
		.Add(fChannels)
		.Add(fAuthNote)
		.AddGlue()
	.End();
}

void ShellyWindow::StartRefresh()
{
	if (fInFlight)
		return; // avoid piling up workers if the device is slow/unreachable
	RefreshJob* job = new RefreshJob{fHost, fPort, fGen, BMessenger(this)};
	thread_id t = spawn_thread(RefreshThread, "shelly_refresh", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	fInFlight = true;
	resume_thread(t);
}

void ShellyWindow::UpdateFromReady(BMessage* ready)
{
	fInFlight = false;

	bool ok = false;
	ready->FindBool("ok", &ok);
	if (!ok) {
		fStatus->SetText(B_TRANSLATE("Dispositivo irraggiungibile."));
		fGraph->Push(NAN); // break the line for this gap
		return;
	}

	int32 gen = 1; ready->FindInt32("gen", &gen);
	if (gen >= 1) fGen = gen; // cache for control calls, saves a probe
	const char* model = ""; ready->FindString("model", &model);
	const char* fw = ""; ready->FindString("fw", &fw);
	char buf[256];
	std::snprintf(buf, sizeof(buf), "gen%d  %s  %s  %s", (int)gen, model, fw, fHost.c_str());
	fStatus->SetText(buf);

	// Repopulate the channel rows.
	while (BView* c = fChannels->ChildAt(0)) { fChannels->RemoveChild(c); delete c; }

	double totalPower = 0.0;
	bool anyPower = false;
	int32 id;
	int count = 0;
	for (int32 i = 0; ready->FindInt32("ch.id", i, &id) == B_OK; ++i) {
		const char* nm = ""; ready->FindString("ch.name", i, &nm);
		bool ctl = false; ready->FindBool("ch.ctl", i, &ctl);
		bool on = false; ready->FindBool("ch.on", i, &on);
		bool hp = false; ready->FindBool("ch.hp", i, &hp);
		double pw = 0; ready->FindDouble("ch.pw", i, &pw);
		double v = 0; ready->FindDouble("ch.v", i, &v);
		double wh = 0; ready->FindDouble("ch.wh", i, &wh);

		if (hp) { totalPower += pw; anyPower = true; }

		std::string readout;
		if (hp) {
			char bb[128];
			std::snprintf(bb, sizeof(bb), "%.1f W", pw);
			readout = bb;
			if (v > 0) { std::snprintf(bb, sizeof(bb), "  %.0f V", v); readout += bb; }
			if (wh > 0) { std::snprintf(bb, sizeof(bb), "  %.2f kWh", wh / 1000.0); readout += bb; }
		}

		BGroupView* row = new BGroupView(B_HORIZONTAL);
		row->GroupLayout()->AddView(new BStringView("", nm));
		row->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
		row->GroupLayout()->AddView(new BStringView("", readout.c_str()));
		if (ctl) {
			BMessage* tg = new BMessage(kMsgToggle);
			tg->AddInt32("id", id);
			tg->AddBool("on", !on); // pressing flips it
			row->GroupLayout()->AddView(
				new BButton("", on ? B_TRANSLATE("Acceso") : B_TRANSLATE("Spento"), tg));
		}
		fChannels->GroupLayout()->AddView(row);
		++count;
	}

	if (count == 0)
		fStatus->SetText(B_TRANSLATE("Nessun canale rilevato."));

	bool auth = false; ready->FindBool("auth", &auth);
	if (auth) {
		fAuthNote->SetText(
			B_TRANSLATE("Autenticazione attiva sul dispositivo: il controllo potrebbe fallire."));
		if (fAuthNote->IsHidden()) fAuthNote->Show();
	} else if (!fAuthNote->IsHidden()) {
		fAuthNote->Hide();
	}

	// The section title carries the current total power, so it is not repeated inside the graph.
	char lab[64];
	if (anyPower)
		std::snprintf(lab, sizeof(lab), "%s: %.1f W", B_TRANSLATE("Potenza"), totalPower);
	else
		std::snprintf(lab, sizeof(lab), "%s (W)", B_TRANSLATE("Potenza"));
	fGraphLabel->SetText(lab);

	// Feed the graph: total active power across metered channels (0 W is a valid reading).
	fGraph->Push(anyPower ? (float)totalPower : NAN);
}

void ShellyWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgRefresh:
			StartRefresh();
			return;
		case kMsgReady:
			UpdateFromReady(msg);
			return;
		case kMsgToggle: {
			int32 id = 0; msg->FindInt32("id", &id);
			bool on = false; msg->FindBool("on", &on);
			if (fStatus != nullptr) fStatus->SetText(B_TRANSLATE("Invio comando..."));
			SetJob* job = new SetJob{fHost, fPort, fGen, (int)id, on, BMessenger(this)};
			thread_id t = spawn_thread(SetThread, "shelly_set", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgCmdDone: {
			bool ok = false; msg->FindBool("ok", &ok);
			if (!ok && fStatus != nullptr)
				fStatus->SetText(B_TRANSLATE("Comando non riuscito."));
			else
				StartRefresh(); // reflect the new state right away
			return;
		}
		case kMsgOpenWeb: {
			std::string url = "http://" + fHost + "/";
			char* argv[1] = { const_cast<char*>(url.c_str()) };
			if (be_roster->Launch("application/x-vnd.Be.URL.http", 1, argv) != B_OK)
				be_roster->Launch("text/html", 1, argv);
			return;
		}
		case kMsgOpenDesktop:
			OpenGraphWidget(fHost, fPort, fGen, fName);
			return;
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class ShellyApp : public BApplication {
public:
	ShellyApp(const std::string& host, const std::string& name, int port, int gen)
		: BApplication(kSignature), fHost(host), fName(name), fPort(port), fGen(gen) {}

	void RefsReceived(BMessage* msg) override
	{
		entry_ref ref;
		for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; ++i) {
			BNode node(&ref);
			if (node.InitCheck() != B_OK)
				continue;
			BString host, name, port, gen;
			ReadAttr(node, "CAMPIELLO:host", host);
			ReadAttr(node, "CAMPIELLO:name", name);
			ReadAttr(node, "CAMPIELLO:port", port);
			ReadAttr(node, "CAMPIELLO:txt.gen", gen);
			if (host.Length() == 0)
				continue;
			int p = (port.Length() > 0) ? std::atoi(port.String()) : 80;
			int g = (gen.Length() > 0) ? std::atoi(gen.String()) : 0;
			(new ShellyWindow(host.String(), name.String(), p, g))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Shelly",
				B_TRANSLATE("Nessun dispositivo. Apri uno Shelly dal vicinato WON, o passa host=<ip>."),
				B_TRANSLATE("Chiudi")))->Go();
			Quit();
			return;
		}
		(new ShellyWindow(fHost, fName, fPort, fGen))->Show();
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
	int         fPort;
	int         fGen;
	bool        fShown = false;
};

int main(int argc, char** argv)
{
	std::string host, name;
	int port = 80, gen = 0;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
		else if (a.compare(0, 5, "port=") == 0) port = std::atoi(a.c_str() + 5);
		else if (a.compare(0, 4, "gen=") == 0) gen = std::atoi(a.c_str() + 4);
	}
	ShellyApp app(host, name, port, gen);
	app.Run();
	return 0;
}
