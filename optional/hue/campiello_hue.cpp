// campiello_hue.cpp
//
// The Campiello Philips Hue add-on: a control panel for a Hue bridge discovered on the network. It
// pairs with the bridge (press the round link button, then "Abbina"), then lists the lights with an
// on/off toggle and a brightness slider. Network I/O runs on worker threads so the UI never blocks.
//
// Launched from the WON neighborhood on a double-click of a Hue bridge (the hue.handler manifest),
// which passes the device via the CAMPIELLO:host/name attributes, or from the command line with
// host=<ip> [name=<label>]. OPTIONAL, Haiku: links OpenSSL and the network kit; the MIT core does
// not depend on it. End-user strings are Italian.
//
//   g++ -std=c++17 campiello_hue.cpp HueBridge.cpp -lbe -lssl -lcrypto -lnetwork

#include <Application.h>
#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Node.h>
#include <ScrollView.h>
#include <Slider.h>
#include <StringView.h>
#include <String.h>
#include <Window.h>

#include <fs_attr.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "HueBridge.h"

using namespace campiello::hue;

static const char* const kSignature = "application/x-vnd.Campiello-hue";

static const uint32 kMsgPair       = 'hpar';
static const uint32 kMsgPairDone   = 'hpad';
static const uint32 kMsgRefresh    = 'href';
static const uint32 kMsgLightsReady= 'hlrd';
static const uint32 kMsgToggle     = 'htog';
static const uint32 kMsgBright     = 'hbri';
static const uint32 kMsgCmdDone    = 'hcmd';

// --------------------------------------------------------------------------- app-key store
// <settings>/Campiello/hue_keys, "host\tappkey" lines, owner-only. The application key is a bridge
// credential; a later step can move it to the encrypted secret store the SMB helper uses.
static std::string KeyStorePath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) != B_OK)
		return "";
	std::string dir = std::string(settings) + "/Campiello";
	::mkdir(dir.c_str(), 0755);
	return dir + "/hue_keys";
}

static std::string LoadKey(const std::string& host)
{
	std::string path = KeyStorePath();
	if (path.empty())
		return "";
	FILE* f = std::fopen(path.c_str(), "r");
	if (f == nullptr)
		return "";
	std::string prefix = host + "\t";
	char line[512];
	std::string key;
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		if (s.compare(0, prefix.size(), prefix) == 0) {
			key = s.substr(prefix.size());
			break;
		}
	}
	std::fclose(f);
	return key;
}

static void SaveKey(const std::string& host, const std::string& key)
{
	std::string path = KeyStorePath();
	if (path.empty() || host.empty() || key.empty())
		return;
	std::vector<std::string> keep;
	std::string prefix = host + "\t";
	FILE* in = std::fopen(path.c_str(), "r");
	if (in != nullptr) {
		char line[512];
		while (std::fgets(line, sizeof(line), in) != nullptr) {
			std::string s(line);
			while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
				s.pop_back();
			if (!s.empty() && s.compare(0, prefix.size(), prefix) != 0)
				keep.push_back(s);
		}
		std::fclose(in);
	}
	keep.push_back(prefix + key);
	FILE* out = std::fopen(path.c_str(), "w");
	if (out == nullptr)
		return;
	::chmod(path.c_str(), 0600);
	for (const std::string& s : keep)
		std::fprintf(out, "%s\n", s.c_str());
	std::fclose(out);
}

// --------------------------------------------------------------------------- workers
struct PairJob { std::string host, name; BMessenger reply; };
static int32 PairThread(void* arg)
{
	PairJob* job = static_cast<PairJob*>(arg);
	HueBridge b(job->host);
	std::string key = b.Pair(job->name.empty() ? std::string("campiello#haiku") : job->name);
	BMessage m(kMsgPairDone);
	m.AddString("key", key.c_str());
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct ListJob { std::string host, key; BMessenger reply; };
static int32 ListThread(void* arg)
{
	ListJob* job = static_cast<ListJob*>(arg);
	HueBridge b(job->host, job->key);
	std::vector<Light> lights = b.ListLights();
	BMessage m(kMsgLightsReady);
	for (const Light& l : lights) {
		m.AddString("id", l.id.c_str());
		m.AddString("name", l.name.c_str());
		m.AddBool("on", l.on);
		m.AddInt32("bri", l.brightness);
		m.AddBool("dim", l.dimmable);
	}
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

struct SetJob { std::string host, key, id, kind; bool on; int value; BMessenger reply; };
static int32 SetThread(void* arg)
{
	SetJob* job = static_cast<SetJob*>(arg);
	HueBridge b(job->host, job->key);
	bool ok = (job->kind == "on") ? b.SetOn(job->id, job->on)
								   : b.SetBrightness(job->id, job->value);
	BMessage m(kMsgCmdDone);
	m.AddBool("ok", ok);
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- window
class HueWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	HueWindow(const std::string& host, const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	void BuildPairing();
	void BuildLights(BMessage* lights); // lights == nullptr -> "loading"
	void StartListing();

	std::string  fHost;
	std::string  fName;
	std::string  fKey;
	BStringView* fStatus = nullptr;
};

HueWindow::HueWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(100, 100, 460, 520), "Philips Hue",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fName(name)
{
	fKey = LoadKey(host);
	if (fKey.empty())
		BuildPairing();
	else {
		BuildLights(nullptr);
		StartListing();
	}
	CenterOnScreen();
}

void HueWindow::BuildPairing()
{
	while (BView* c = ChildAt(0)) { RemoveChild(c); delete c; }

	BStringView* title = new BStringView("t", "Abbina il bridge Philips Hue");
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.25f);
	title->SetFont(&f);
	BStringView* i1 = new BStringView("i1", "1) Premi il grande pulsante rotondo sul bridge Hue.");
	BStringView* i2 = new BStringView("i2", "2) Entro 30 secondi premi \"Abbina\" qui sotto.");
	fStatus = new BStringView("st", fHost.c_str());
	BButton* pair = new BButton("pair", "Abbina", new BMessage(kMsgPair));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(i1)
		.Add(i2)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(pair)
		.End()
		.AddGlue()
	.End();
}

void HueWindow::BuildLights(BMessage* lights)
{
	while (BView* c = ChildAt(0)) { RemoveChild(c); delete c; }

	BStringView* title = new BStringView("t", fName.empty() ? "Luci Hue" : fName.c_str());
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.2f);
	title->SetFont(&f);
	BButton* refresh = new BButton("refresh", "Aggiorna", new BMessage(kMsgRefresh));
	fStatus = new BStringView("st", lights == nullptr ? "Carico le luci..." : "");

	BLayoutBuilder::Group<> root = BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(title)
			.AddGlue()
			.Add(refresh)
		.End()
		.Add(fStatus);

	if (lights != nullptr) {
		const char* id;
		int count = 0;
		for (int32 i = 0; lights->FindString("id", i, &id) == B_OK; ++i) {
			const char* name = "";
			lights->FindString("name", i, &name);
			bool on = false; lights->FindBool("on", i, &on);
			bool dim = false; lights->FindBool("dim", i, &dim);
			int32 bri = 0; lights->FindInt32("bri", i, &bri);

			BMessage* toggle = new BMessage(kMsgToggle);
			toggle->AddString("id", id);
			toggle->AddBool("on", !on); // pressing flips it
			BButton* btn = new BButton("", on ? "On" : "Off", toggle);

			BStringView* label = new BStringView("", name);

			root.AddGroup(B_HORIZONTAL)
				.Add(label)
				.AddGlue()
				.Add(btn)
			.End();

			if (dim) {
				BMessage* brm = new BMessage(kMsgBright);
				brm->AddString("id", id);
				BSlider* sl = new BSlider("", "", brm, 0, 100, B_HORIZONTAL);
				sl->SetValue(bri);
				sl->SetModificationMessage(nullptr); // fire only on release
				root.Add(sl);
			}
			++count;
		}
		if (count == 0)
			fStatus->SetText("Nessuna luce trovata (bridge abbinato?).");
	}
	root.AddGlue().End();
}

void HueWindow::StartListing()
{
	ListJob* job = new ListJob{fHost, fKey, BMessenger(this)};
	thread_id t = spawn_thread(ListThread, "hue_list", B_NORMAL_PRIORITY, job);
	if (t < 0) { delete job; return; }
	resume_thread(t);
}

void HueWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgPair: {
			if (fStatus != nullptr) fStatus->SetText("Abbinamento in corso...");
			PairJob* job = new PairJob{fHost, fName, BMessenger(this)};
			thread_id t = spawn_thread(PairThread, "hue_pair", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgPairDone: {
			const char* key = "";
			msg->FindString("key", &key);
			if (key[0] == '\0') {
				if (fStatus != nullptr)
					fStatus->SetText("Pulsante non premuto o bridge irraggiungibile. Riprova.");
				return;
			}
			fKey = key;
			SaveKey(fHost, fKey);
			BuildLights(nullptr);
			StartListing();
			return;
		}
		case kMsgRefresh:
			if (fStatus != nullptr) fStatus->SetText("Aggiorno...");
			StartListing();
			return;
		case kMsgLightsReady:
			BuildLights(msg);
			return;
		case kMsgToggle: {
			const char* id = ""; msg->FindString("id", &id);
			bool on = false; msg->FindBool("on", &on);
			SetJob* job = new SetJob{fHost, fKey, id, "on", on, 0, BMessenger(this)};
			thread_id t = spawn_thread(SetThread, "hue_set", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgBright: {
			const char* id = ""; msg->FindString("id", &id);
			// BControl::Invoke attaches the slider's current value under "be:value".
			int32 v = 50;
			msg->FindInt32("be:value", &v);
			SetJob* job = new SetJob{fHost, fKey, id, "bri", false, (int)v, BMessenger(this)};
			thread_id t = spawn_thread(SetThread, "hue_set", B_NORMAL_PRIORITY, job);
			if (t < 0) delete job; else resume_thread(t);
			return;
		}
		case kMsgCmdDone: {
			bool ok = false; msg->FindBool("ok", &ok);
			if (!ok && fStatus != nullptr)
				fStatus->SetText("Comando non riuscito.");
			else
				StartListing(); // reflect the new state
			return;
		}
	}
	BWindow::MessageReceived(msg);
}

// --------------------------------------------------------------------------- app
class HueApp : public BApplication {
public:
	HueApp(const std::string& host, const std::string& name)
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
			(new HueWindow(host.String(), name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			(new BAlert("Philips Hue",
				"Nessun bridge. Apri Hue dal vicinato WON, o passa host=<ip>.", "Chiudi"))->Go();
			Quit();
			return;
		}
		(new HueWindow(fHost, fName))->Show();
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

#ifndef HUE_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string a(argv[i]);
		if (a.compare(0, 5, "host=") == 0) host = a.substr(5);
		else if (a.compare(0, 5, "name=") == 0) name = a.substr(5);
	}
	HueApp app(host, name);
	app.Run();
	return 0;
}
#endif
