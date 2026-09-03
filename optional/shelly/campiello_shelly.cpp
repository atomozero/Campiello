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

static const bigtime_t kPollInterval = 3000000; // 3 s

// --------------------------------------------------------------------------- live watt graph
// A rolling line chart of total active power. Keeps the last kMax samples and auto-scales the Y axis
// to a "nice" ceiling. Dependency-light: a plain BView that redraws on each pushed sample.
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
		if (fData.size() > kMax)
			fData.pop_front();
		Invalidate();
	}

	bool HasData() const { return !fData.empty(); }

	void Draw(BRect) override
	{
		BRect b = Bounds();
		const float pad = 4.0f;
		BRect plot(b.left + pad, b.top + pad, b.right - pad, b.bottom - pad);

		// Frame.
		SetHighColor(tint_color(ui_color(B_CONTROL_BORDER_COLOR), B_LIGHTEN_1_TINT));
		StrokeRect(b);

		float scale = 1.0f;
		for (float v : fData)
			if (!std::isnan(v) && v > scale) scale = v;
		scale = NiceCeil(scale);

		// Horizontal grid lines + Y labels (0, mid, full scale).
		SetHighColor(tint_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR), B_DARKEN_1_TINT));
		SetLowColor(ViewColor());
		for (int i = 0; i <= 2; ++i) {
			float frac = i / 2.0f;
			float y = plot.bottom - frac * plot.Height();
			StrokeLine(BPoint(plot.left, y), BPoint(plot.right, y));
		}

		// The line.
		if (fData.size() >= 2) {
			SetHighColor(60, 130, 220); // a calm blue
			size_t n = fData.size();
			BPoint prev;
			bool havePrev = false;
			for (size_t i = 0; i < n; ++i) {
				float v = fData[i];
				if (std::isnan(v)) { havePrev = false; continue; }
				float x = plot.left + (n == 1 ? 0.0f : (float)i / (n - 1) * plot.Width());
				float y = plot.bottom - (v / scale) * plot.Height();
				BPoint p(x, y);
				if (havePrev)
					StrokeLine(prev, p);
				prev = p;
				havePrev = true;
			}
		}

		// Y-axis labels on the left: full scale at the top, 0 at the bottom (the current value is
		// shown in the section title above the graph, so it is not repeated here).
		SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
		char full[32];
		std::snprintf(full, sizeof(full), "%g W", scale);
		DrawString(full, BPoint(plot.left + 2, plot.top + 12));
		DrawString("0 W", BPoint(plot.left + 2, plot.bottom - 3));
	}

private:
	static float NiceCeil(float v)
	{
		if (v <= 0) return 1.0f;
		float p = std::pow(10.0f, std::floor(std::log10(v)));
		float f = v / p;
		float nf = (f <= 1) ? 1 : (f <= 2) ? 2 : (f <= 5) ? 5 : 10;
		return nf * p;
	}

	std::deque<float> fData;
	static const size_t kMax = 120; // ~6 minutes at a 3 s poll
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
