// campiello_daikin.cpp
//
// The Campiello Daikin add-on: a control panel for a Daikin air conditioner discovered via
// _dkapi._tcp (the BRP069/BRP072 Wi-Fi adapter). Over the adapter's plain-HTTP local API it reads
// the REAL state (indoor/outdoor temperature, power, mode, target temperature, fan) and actually
// commands the unit: power on/off, mode, target temperature and fan speed. No cloud, no account, no
// heavy crypto - the classic Daikin local API is open text over HTTP. Network I/O runs on worker
// threads so the UI never blocks.
//
// Launched from the WON neighborhood on a double-click of a Daikin device (the daikin.handler
// manifest), which passes the device via CAMPIELLO:host/name and CAMPIELLO:port, or from the command
// line with host=<ip> [port=<n>] [name=<label>]. No third-party dependency: links libbe + the
// network kit. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_daikin.cpp DaikinClient.cpp -lbe -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Node.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <String.h>
#include <Window.h>

#include <fs_attr.h>

#include <cstdlib>
#include <string>

#include "DaikinClient.h"

using namespace campiello::daikin;

static const char* const kSignature = "application/x-vnd.Campiello-daikin";

static const uint32 kMsgRefresh   = 'drfr';
static const uint32 kMsgInfoReady = 'dinr';
static const uint32 kMsgPower     = 'dpow';
static const uint32 kMsgMode      = 'dmod'; // int32 "mode"
static const uint32 kMsgFan       = 'dfan'; // string "rate"
static const uint32 kMsgTempUp    = 'dtup';
static const uint32 kMsgTempDown  = 'dtdn';
static const uint32 kMsgSetDone   = 'dset';

// True when a target-temperature string is a real number (cool/heat/auto), false for the "M"/"--"
// markers the unit reports in fan and dehumidify modes (where there is no target temperature).
static bool NumericTemp(const std::string& s, float* out = nullptr)
{
	if (s.empty())
		return false;
	char* end = nullptr;
	float v = std::strtof(s.c_str(), &end);
	if (end == s.c_str() || (end != nullptr && *end != '\0'))
		return false;
	if (out != nullptr)
		*out = v;
	return true;
}

// --------------------------------------------------------------------------- workers
struct InfoJob { std::string host; int port; BMessenger reply; };
static int32 InfoThread(void* arg)
{
	InfoJob* job = static_cast<InfoJob*>(arg);
	DaikinClient d(job->host, job->port);
	BasicInfo bi = d.GetBasicInfo();
	ControlInfo ci = d.GetControlInfo();
	SensorInfo si = d.GetSensorInfo();

	BMessage m(kMsgInfoReady);
	m.AddBool("ok", ci.ok || si.ok || bi.ok);
	m.AddString("name", bi.name.c_str());
	m.AddString("ver", bi.ver.c_str());
	m.AddInt32("pow", ci.pow);
	m.AddInt32("mode", ci.mode);
	m.AddString("stemp", ci.stemp.c_str());
	m.AddInt32("shum", ci.shum);
	m.AddString("frate", ci.fRate.c_str());
	m.AddInt32("fdir", ci.fDir);
	m.AddString("htemp", si.htemp.c_str());
	m.AddString("otemp", si.otemp.c_str());
	m.AddString("hhum", si.hhum.c_str());
	m.AddInt32("cmpfreq", si.cmpfreq);
	m.AddInt32("err", bi.err);
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct SetJob {
	std::string host; int port;
	int pow, mode, shum, fDir;
	std::string stemp, fRate;
	BMessenger reply;
};
static int32 SetThread(void* arg)
{
	SetJob* job = static_cast<SetJob*>(arg);
	DaikinClient d(job->host, job->port);
	bool ok = d.SetControlInfo(job->pow, job->mode, job->stemp, job->shum, job->fRate, job->fDir);
	BMessage m(kMsgSetDone);
	m.AddBool("ok", ok);
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class DaikinWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	DaikinWindow(const std::string& host, int port, const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	void StartInfo();
	void ApplySet(int pow, int mode, const std::string& stemp, int shum,
		const std::string& fRate, int fDir);
	void UpdateViews();
	BMenuField* MakeModeMenu();
	BMenuField* MakeFanMenu();

	std::string fHost;
	int         fPort;
	std::string fName;
	ControlInfo fControl; // last known control state (drives the set calls)
	SensorInfo  fSensor;
	bool        fHave = false;

	BStringView* fTitle = nullptr;
	BStringView* fRoom = nullptr;
	BStringView* fOutdoor = nullptr;
	BStringView* fTarget = nullptr;
	BStringView* fState = nullptr;
	BButton*     fPower = nullptr;
	BMenuField*  fModeField = nullptr;
	BMenuField*  fFanField = nullptr;
	BButton*     fTempUp = nullptr;
	BButton*     fTempDown = nullptr;
	BStringView* fStatus = nullptr;
};

DaikinWindow::DaikinWindow(const std::string& host, int port, const std::string& name)
	: BWindow(BRect(100, 100, 460, 420), "Condizionatore Daikin", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fPort(port), fName(name)
{
	fTitle = new BStringView("t", fName.empty() ? host.c_str() : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	fTitle->SetFont(&f);

	fRoom = new BStringView("room", "Temperatura interna: -");
	fOutdoor = new BStringView("out", "Temperatura esterna: -");
	fTarget = new BStringView("tgt", "Temperatura impostata: -");
	fState = new BStringView("state", "Stato: interrogo il dispositivo...");

	fPower = new BButton("pow", "Accendi/Spegni", new BMessage(kMsgPower));
	fModeField = MakeModeMenu();
	fFanField = MakeFanMenu();
	fTempDown = new BButton("td", "-", new BMessage(kMsgTempDown));
	fTempUp = new BButton("tu", "+", new BMessage(kMsgTempUp));
	BButton* refresh = new BButton("rf", "Aggiorna", new BMessage(kMsgRefresh));

	fStatus = new BStringView("st", host.c_str());

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fTitle)
		.Add(fRoom)
		.Add(fOutdoor)
		.Add(fTarget)
		.Add(fState)
		.AddGroup(B_HORIZONTAL)
			.Add(fPower)
			.AddGlue()
			.Add(new BStringView("tl", "Target"))
			.Add(fTempDown)
			.Add(fTempUp)
		.End()
		.Add(fModeField)
		.Add(fFanField)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(refresh)
		.End()
		.Add(fStatus)
	.End();

	CenterOnScreen();
	StartInfo();
}

BMenuField* DaikinWindow::MakeModeMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("modo");
	const struct { const char* label; int mode; } modes[] = {
		{"Automatico", 0}, {"Raffrescamento", 3}, {"Riscaldamento", 4},
		{"Deumidificazione", 2}, {"Ventilazione", 6},
	};
	for (const auto& m : modes) {
		BMessage* msg = new BMessage(kMsgMode);
		msg->AddInt32("mode", m.mode);
		menu->AddItem(new BMenuItem(m.label, msg));
	}
	return new BMenuField("modeField", "Modo:", menu);
}

BMenuField* DaikinWindow::MakeFanMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("ventola");
	const struct { const char* label; const char* rate; } rates[] = {
		{"Automatica", "A"}, {"Silenziosa", "B"}, {"Livello 1", "3"}, {"Livello 2", "4"},
		{"Livello 3", "5"}, {"Livello 4", "6"}, {"Livello 5", "7"},
	};
	for (const auto& r : rates) {
		BMessage* msg = new BMessage(kMsgFan);
		msg->AddString("rate", r.rate);
		menu->AddItem(new BMenuItem(r.label, msg));
	}
	return new BMenuField("fanField", "Ventola:", menu);
}

void DaikinWindow::StartInfo()
{
	fStatus->SetText("Aggiorno...");
	InfoJob* job = new InfoJob{fHost, fPort, BMessenger(this)};
	thread_id t = spawn_thread(InfoThread, "daikin_info", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void DaikinWindow::ApplySet(int pow, int mode, const std::string& stemp, int shum,
	const std::string& fRate, int fDir)
{
	// Fan and dehumidify modes have no target temperature: send the "M" marker the unit expects.
	std::string temp = stemp;
	if ((mode == 2 || mode == 6) && NumericTemp(temp))
		temp = "M";
	if (temp.empty())
		temp = "M";

	fStatus->SetText("Invio comando...");
	SetJob* job = new SetJob{fHost, fPort, pow, mode, shum, fDir, temp, fRate, BMessenger(this)};
	thread_id t = spawn_thread(SetThread, "daikin_set", B_NORMAL_PRIORITY, job);
	if (t < 0) delete job; else resume_thread(t);
}

void DaikinWindow::UpdateViews()
{
	BString room("Temperatura interna: ");
	room << (fSensor.htemp.empty() || fSensor.htemp == "-" ? "-" : (fSensor.htemp + "°C").c_str());
	fRoom->SetText(room.String());

	BString out("Temperatura esterna: ");
	out << (fSensor.otemp.empty() || fSensor.otemp == "-" ? "-" : (fSensor.otemp + "°C").c_str());
	fOutdoor->SetText(out.String());

	BString tgt("Temperatura impostata: ");
	if (NumericTemp(fControl.stemp))
		tgt << (fControl.stemp + "°C").c_str();
	else
		tgt << "non applicabile";
	fTarget->SetText(tgt.String());

	BString state("Stato: ");
	state << (fControl.pow ? "acceso" : "spento");
	state << " - " << ModeName(fControl.mode).c_str();
	state << " - ventola " << FanRateName(fControl.fRate).c_str();
	fState->SetText(state.String());

	fPower->SetLabel(fControl.pow ? "Spegni" : "Accendi");
	bool numeric = NumericTemp(fControl.stemp);
	fTempUp->SetEnabled(numeric);
	fTempDown->SetEnabled(numeric);

	// Reflect the current mode/fan in the menus.
	if (fModeField != nullptr && fModeField->Menu() != nullptr) {
		BMenu* m = fModeField->Menu();
		for (int32 i = 0; i < m->CountItems(); ++i) {
			BMenuItem* it = m->ItemAt(i);
			int32 mode = -1;
			if (it->Message() != nullptr && it->Message()->FindInt32("mode", &mode) == B_OK
				&& mode == fControl.mode)
				it->SetMarked(true);
		}
	}
	if (fFanField != nullptr && fFanField->Menu() != nullptr) {
		BMenu* m = fFanField->Menu();
		for (int32 i = 0; i < m->CountItems(); ++i) {
			BMenuItem* it = m->ItemAt(i);
			const char* rate = nullptr;
			if (it->Message() != nullptr && it->Message()->FindString("rate", &rate) == B_OK
				&& fControl.fRate == rate)
				it->SetMarked(true);
		}
	}
}

void DaikinWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgRefresh:
			StartInfo();
			return;
		case kMsgInfoReady: {
			bool ok = false; msg->FindBool("ok", &ok);
			if (!ok) {
				fState->SetText("Stato: nessuna risposta dal dispositivo.");
				fStatus->SetText("Non raggiungibile.");
				return;
			}
			const char* s = "";
			msg->FindInt32("pow", &fControl.pow);
			msg->FindInt32("mode", &fControl.mode);
			if (msg->FindString("stemp", &s) == B_OK) fControl.stemp = s;
			msg->FindInt32("shum", &fControl.shum);
			if (msg->FindString("frate", &s) == B_OK) fControl.fRate = s;
			msg->FindInt32("fdir", &fControl.fDir);
			if (msg->FindString("htemp", &s) == B_OK) fSensor.htemp = s;
			if (msg->FindString("otemp", &s) == B_OK) fSensor.otemp = s;
			if (msg->FindString("hhum", &s) == B_OK) fSensor.hhum = s;
			msg->FindInt32("cmpfreq", &fSensor.cmpfreq);
			const char* name = ""; msg->FindString("name", &name);
			if (name[0] != '\0' && fName.empty())
				fTitle->SetText(name);
			fHave = true;
			UpdateViews();
			fStatus->SetText("Pronto.");
			return;
		}
		case kMsgPower:
			if (!fHave) return;
			ApplySet(fControl.pow ? 0 : 1, fControl.mode, fControl.stemp, fControl.shum,
				fControl.fRate, fControl.fDir);
			return;
		case kMsgMode: {
			if (!fHave) return;
			int32 mode = fControl.mode;
			msg->FindInt32("mode", &mode);
			// Changing mode turns the unit on (a mode change on a powered-off unit is otherwise a no-op).
			ApplySet(1, mode, fControl.stemp, fControl.shum, fControl.fRate, fControl.fDir);
			return;
		}
		case kMsgFan: {
			if (!fHave) return;
			const char* rate = fControl.fRate.c_str();
			msg->FindString("rate", &rate);
			ApplySet(fControl.pow, fControl.mode, fControl.stemp, fControl.shum, rate, fControl.fDir);
			return;
		}
		case kMsgTempUp:
		case kMsgTempDown: {
			if (!fHave) return;
			float t = 0.0f;
			if (!NumericTemp(fControl.stemp, &t))
				return;
			t += (msg->what == kMsgTempUp) ? 0.5f : -0.5f;
			if (t < 16.0f) t = 16.0f;
			if (t > 30.0f) t = 30.0f;
			char buf[16];
			std::snprintf(buf, sizeof(buf), "%.1f", t);
			ApplySet(fControl.pow, fControl.mode, buf, fControl.shum, fControl.fRate, fControl.fDir);
			return;
		}
		case kMsgSetDone: {
			bool ok = false; msg->FindBool("ok", &ok);
			fStatus->SetText(ok ? "Comando inviato." : "Comando non riuscito.");
			if (ok) StartInfo(); // re-read the real state
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class DaikinApp : public BApplication {
public:
	DaikinApp(const std::string& host, int port, const std::string& name)
		: BApplication(kSignature), fHost(host), fPort(port), fName(name) {}

	void RefsReceived(BMessage* msg) override
	{
		entry_ref ref;
		for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; ++i) {
			BNode node(&ref);
			if (node.InitCheck() != B_OK)
				continue;
			BString host, name, port;
			ReadAttr(node, "CAMPIELLO:host", host);
			ReadAttr(node, "CAMPIELLO:name", name);
			ReadAttr(node, "CAMPIELLO:port", port);
			if (host.Length() == 0)
				continue;
			int p = (port.Length() > 0) ? std::atoi(port.String()) : 80;
			if (p <= 0) p = 80;
			(new DaikinWindow(host.String(), p, name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Condizionatore Daikin",
				"Nessun dispositivo. Apri un condizionatore Daikin dal vicinato WON, o passa host=<ip>.",
				"Chiudi"))->Go();
			Quit();
			return;
		}
		(new DaikinWindow(fHost, fPort, fName))->Show();
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
	int fPort;
	std::string fName;
	bool fShown = false;
};

#ifndef DAIKIN_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	int port = 80;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "port=") == 0) port = std::atoi(a.substr(5).c_str());
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	if (port <= 0) port = 80;
	DaikinApp app(host, port, name);
	app.Run();
	return 0;
}
#endif
