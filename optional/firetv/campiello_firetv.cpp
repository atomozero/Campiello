// campiello_firetv.cpp
//
// The Campiello Fire TV remote: a realistic on-screen remote for an Amazon Fire TV, using the
// validated REST client (FireTVRemote). It is the first real device add-on (docs/DEVICE_ADDONS.md):
// launched with the device on the command line (host=<ip> [name=<label>]), it shows a remote whose
// buttons drive the TV over the network. If the device is not paired yet it walks a PIN pairing.
//
// OPTIONAL, Haiku: links libbe, OpenSSL, and the network kit. Separate campiello_firetv package;
// the MIT core does not depend on it. End-user strings are Italian (working agreement rule 4).
//
//   g++ -std=c++17 campiello_firetv.cpp FireTVRemote.cpp -lbe -lssl -lcrypto -lnetwork

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <OS.h>
#include <Messenger.h>
#include <Node.h>
#include <Point.h>
#include <Polygon.h>
#include <Region.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <View.h>
#include <Window.h>

#include <fs_attr.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "FireTVRemote.h"

using namespace campiello::firetv;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language, per the working
// agreement); catalogs under data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "FireTV"

static const char* const kSignature = "application/x-vnd.Campiello-firetv";

// Messages.
static const uint32 kMsgCmdDone  = 'fcmd'; // worker -> UI: a command finished (ok flag + label)
static const uint32 kMsgPairShow = 'fpsh'; // "Mostra PIN sulla TV"
static const uint32 kMsgPairOk   = 'fpok'; // "Conferma PIN"
static const uint32 kMsgOpenKeymap    = 'fkmo'; // RemoteView -> window: open the key-mapping GUI
static const uint32 kMsgKeymapChanged = 'fkmc'; // KeyMapView -> window: the map changed, reload it
static const uint32 kMsgKeymapClosed  = 'fkmx'; // KeyMapWindow -> window: it was closed

// ---------------------------------------------------------------------------
// Per-host token store: <settings>/Campiello/firetv_tokens, "host\ttoken" lines. Tokens are the
// device's client token (less sensitive than a password); kept owner-only. A later step can move
// this to the encrypted secret store the SMB helper uses.
static std::string TokenPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) != B_OK)
		return "";
	std::string dir = std::string(settings) + "/Campiello";
	::mkdir(dir.c_str(), 0755);
	return dir + "/firetv_tokens";
}

static std::string LoadToken(const std::string& host)
{
	std::string path = TokenPath();
	if (path.empty())
		return "";
	FILE* f = std::fopen(path.c_str(), "r");
	if (f == nullptr)
		return "";
	std::string prefix = host + "\t";
	char line[512];
	std::string token;
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		if (s.compare(0, prefix.size(), prefix) == 0) {
			token = s.substr(prefix.size());
			break;
		}
	}
	std::fclose(f);
	return token;
}

static void SaveToken(const std::string& host, const std::string& token)
{
	std::string path = TokenPath();
	if (path.empty() || host.empty() || token.empty())
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
	keep.push_back(prefix + token);
	FILE* out = std::fopen(path.c_str(), "w");
	if (out == nullptr)
		return;
	::chmod(path.c_str(), 0600);
	for (const std::string& s : keep)
		std::fprintf(out, "%s\n", s.c_str());
	std::fclose(out);
}

// A command to run on a worker thread: type + arg, described for the status line.
struct Cmd {
	std::string kind;  // "nav" | "media" | "app" | "wake" | "text"
	std::string arg;
	std::string scan;  // media scan direction, if any
	std::string label; // for the status line
};

// Vector icons drawn on the buttons (no font glyphs, which may be missing).
enum IconType { kIconPower, kIconBack, kIconHome, kIconMenu, kIconRewind, kIconPlay, kIconForward };

// ---------------------------------------------------------------------------
// Keyboard shortcuts. Mappable actions (stable id + Italian label + the command each sends), a
// key-per-action table persisted to <settings>/Campiello/firetv_keymap, and a key-name helper.
struct MapAction { const char* id; const char* label; Cmd cmd; };

// Labels are marked for collection here and translated where they are drawn (B_TRANSLATE at use).
static const MapAction kMapActions[] = {
	{"power",   B_TRANSLATE_MARK("Accensione"),      {"wake",  "",           "",        "Accensione"}},
	{"up",      B_TRANSLATE_MARK("Su"),              {"nav",   "dpad_up",    "",        "Su"}},
	{"down",    B_TRANSLATE_MARK("Giu"),             {"nav",   "dpad_down",  "",        "Giu"}},
	{"left",    B_TRANSLATE_MARK("Sinistra"),        {"nav",   "dpad_left",  "",        "Sinistra"}},
	{"right",   B_TRANSLATE_MARK("Destra"),          {"nav",   "dpad_right", "",        "Destra"}},
	{"select",  B_TRANSLATE_MARK("OK"),              {"nav",   "select",     "",        "OK"}},
	{"back",    B_TRANSLATE_MARK("Indietro"),        {"nav",   "back",       "",        "Indietro"}},
	{"home",    B_TRANSLATE_MARK("Home"),            {"nav",   "home",       "",        "Home"}},
	{"menu",    B_TRANSLATE_MARK("Menu"),            {"nav",   "menu",       "",        "Menu"}},
	{"play",    B_TRANSLATE_MARK("Play/Pausa"),      {"media", "play",       "",        "Play/Pausa"}},
	{"rewind",  B_TRANSLATE_MARK("Indietro veloce"), {"media", "scan",       "back",    "Indietro veloce"}},
	{"forward", B_TRANSLATE_MARK("Avanti veloce"),   {"media", "scan",       "forward", "Avanti veloce"}},
};
static const int kNumMapActions = (int)(sizeof(kMapActions) / sizeof(kMapActions[0]));

static std::string KeymapPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) != B_OK)
		return "";
	std::string dir = std::string(settings) + "/Campiello";
	::mkdir(dir.c_str(), 0755);
	return dir + "/firetv_keymap";
}

struct KeyMap {
	uint8 keys[kNumMapActions]; // key byte per action, 0 = unassigned

	KeyMap() { Defaults(); }

	int Index(const std::string& id) const
	{
		for (int i = 0; i < kNumMapActions; ++i)
			if (id == kMapActions[i].id)
				return i;
		return -1;
	}
	void Set(const std::string& id, uint8 b) { int i = Index(id); if (i >= 0) keys[i] = b; }

	void Defaults()
	{
		for (int i = 0; i < kNumMapActions; ++i)
			keys[i] = 0;
		Set("power", 'p');
		Set("up", B_UP_ARROW);   Set("down", B_DOWN_ARROW);
		Set("left", B_LEFT_ARROW); Set("right", B_RIGHT_ARROW);
		Set("select", B_ENTER);  Set("back", B_BACKSPACE);
		Set("home", 'h');        Set("menu", 'm');
		Set("play", B_SPACE);    Set("rewind", ',');  Set("forward", '.');
	}

	// The action index a key byte triggers, or -1. A byte may be bound to at most one action:
	// assigning it elsewhere clears the previous binding (see the mapping GUI).
	int ActionForKey(uint8 b) const
	{
		if (b == 0)
			return -1;
		for (int i = 0; i < kNumMapActions; ++i)
			if (keys[i] == b)
				return i;
		return -1;
	}

	void Load()
	{
		Defaults();
		std::string path = KeymapPath();
		if (path.empty())
			return;
		FILE* f = std::fopen(path.c_str(), "r");
		if (f == nullptr)
			return; // no saved file: keep defaults
		for (int i = 0; i < kNumMapActions; ++i)
			keys[i] = 0; // a saved file is authoritative
		char line[128], id[64];
		int code;
		while (std::fgets(line, sizeof(line), f) != nullptr)
			if (std::sscanf(line, "%63s %d", id, &code) == 2 && code > 0 && code < 256)
				Set(id, (uint8)code);
		std::fclose(f);
	}

	void Save() const
	{
		std::string path = KeymapPath();
		if (path.empty())
			return;
		FILE* f = std::fopen(path.c_str(), "w");
		if (f == nullptr)
			return;
		::chmod(path.c_str(), 0600);
		for (int i = 0; i < kNumMapActions; ++i)
			std::fprintf(f, "%s %d\n", kMapActions[i].id, (int)keys[i]);
		std::fclose(f);
	}
};

// A short human name for a key byte, for the mapping GUI.
static std::string KeyName(uint8 b)
{
	switch (b) {
		case 0:             return B_TRANSLATE("(nessuno)");   // unassigned
		case B_UP_ARROW:    return B_TRANSLATE("Su \xE2\x86\x91");
		case B_DOWN_ARROW:  return B_TRANSLATE("Giu \xE2\x86\x93");
		case B_LEFT_ARROW:  return B_TRANSLATE("\xE2\x86\x90 Sin");
		case B_RIGHT_ARROW: return B_TRANSLATE("Des \xE2\x86\x92");
		case B_ENTER:       return B_TRANSLATE("Invio");
		case B_SPACE:       return B_TRANSLATE("Spazio");
		case B_BACKSPACE:   return B_TRANSLATE("Backspace");
		case B_ESCAPE:      return B_TRANSLATE("Esc");
		case B_TAB:         return B_TRANSLATE("Tab");
		case B_DELETE:      return B_TRANSLATE("Canc");
		case B_HOME:        return B_TRANSLATE("Home");
		case B_END:         return B_TRANSLATE("Fine");
		case B_PAGE_UP:     return B_TRANSLATE("PgSu");
		case B_PAGE_DOWN:   return B_TRANSLATE("PgGiu");
	}
	if (b >= 0x21 && b < 0x7f) {
		char c = (char)b;
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		return std::string(1, c);
	}
	return "?";
}

// ---------------------------------------------------------------------------
class RemoteView : public BView {
public:
	RemoteView(const std::string& host, const std::string& name, const std::string& token);

	void Draw(BRect update) override;
	void MouseDown(BPoint where) override;
	void KeyDown(const char* bytes, int32 numBytes) override;
	void AttachedToWindow() override { MakeFocus(true); }
	void WindowActivated(bool active) override { if (active) MakeFocus(true); }

	void Send(const Cmd& cmd); // spawn a worker to run cmd against the device
	void SetStatus(const std::string& s) { fStatus = s; Invalidate(); }
	void ReloadKeymap() { fKeymap.Load(); } // after the mapping GUI edits it
	const std::string& Host() const { return fHost; }
	const std::string& Token() const { return fToken; }
	void SetToken(const std::string& t) { fToken = t; }

private:
	// Layout constants (the view is a fixed size).
	static constexpr float kW = 240;
	static constexpr float kH = 592; // room for the status line + the "Tasti rapidi" button
	float CX() const { return kW / 2; }

	struct Hit {
		enum Shape { kCircle, kRect } shape;
		BPoint c;      // circle centre
		float  r = 0;  // circle radius
		BRect  rect;   // rect
		Cmd    cmd;
	};
	void AddCircle(float x, float y, float r, const Cmd& cmd);
	void AddRect(BRect rc, const Cmd& cmd);
	void Layout();

	void DrawRoundButton(BPoint c, float r, IconType icon, rgb_color fill);
	void DrawIcon(BPoint c, float s, IconType icon);       // white vector icon centred at c
	void DrawArrow(BPoint c, float size, int dir); // 0 up 1 right 2 down 3 left

	std::string fHost;
	std::string fName;
	std::string fToken;
	std::string fStatus;
	std::vector<Hit> fHits;
	BPoint fNavCenter;
	float  fNavOuter = 92;
	float  fNavInner = 38;
	std::vector<std::pair<BRect, Cmd>> fApps; // app pills (drawn with labels)
	BRect  fKeymapButton;                     // "Tasti rapidi" button
	KeyMap fKeymap;
};

RemoteView::RemoteView(const std::string& host, const std::string& name, const std::string& token)
	: BView(BRect(0, 0, kW - 1, kH - 1), "remote", B_FOLLOW_ALL, B_WILL_DRAW),
	  fHost(host), fName(name), fToken(token)
{
	SetViewColor(28, 28, 32);
	fKeymap.Load();
	Layout();
}

void RemoteView::AddCircle(float x, float y, float r, const Cmd& cmd)
{
	Hit h;
	h.shape = Hit::kCircle;
	h.c = BPoint(x, y);
	h.r = r;
	h.cmd = cmd;
	fHits.push_back(h);
}

void RemoteView::AddRect(BRect rc, const Cmd& cmd)
{
	Hit h;
	h.shape = Hit::kRect;
	h.rect = rc;
	h.cmd = cmd;
	fHits.push_back(h);
}

void RemoteView::Layout()
{
	fHits.clear();
	fApps.clear();

	// Power (top).
	AddCircle(CX(), 62, 20, {"wake", "", "", "Accensione"});

	// Navigation ring: 4 directions (handled specially in MouseDown by angle) + centre Select.
	fNavCenter = BPoint(CX(), 200);
	AddCircle(fNavCenter.x, fNavCenter.y, fNavInner, {"nav", "select", "", "OK"});

	// Back / Home / Menu.
	float row1 = 320;
	AddCircle(CX() - 72, row1, 24, {"nav", "back", "", "Indietro"});
	AddCircle(CX(),      row1, 24, {"nav", "home", "", "Home"});
	AddCircle(CX() + 72, row1, 24, {"nav", "menu", "", "Menu"});

	// Playback: Rewind / Play-Pause / Forward.
	float row2 = 392;
	AddCircle(CX() - 72, row2, 24, {"media", "scan", "back", "Indietro veloce"});
	AddCircle(CX(),      row2, 26, {"media", "play", "", "Play/Pausa"});
	AddCircle(CX() + 72, row2, 24, {"media", "scan", "forward", "Avanti veloce"});

	// App pills (two rows of two).
	struct App { const char* label; const char* pkg; };
	App apps[4] = {
		{"prime video", "com.amazon.avod"},
		{"Netflix",     "com.netflix.ninja"},
		{"YouTube",     "com.amazon.firetv.youtube"},
		{"RaiPlay",     "it.rai.raiplay"},
	};
	float ax0 = 20, aw = (kW - 40 - 12) / 2, ah = 34, ay = 448, gap = 12;
	for (int i = 0; i < 4; ++i) {
		float x = ax0 + (i % 2) * (aw + gap);
		float y = ay + (i / 2) * (ah + gap);
		BRect rc(x, y, x + aw, y + ah);
		Cmd cmd{"app", apps[i].pkg, "", std::string("Apri ") + apps[i].label};
		fApps.push_back({rc, cmd});
		AddRect(rc, cmd);
	}

	// "Tasti rapidi" button: opens the key-mapping GUI (kind "ui" is intercepted in MouseDown).
	fKeymapButton = BRect(CX() - 82, 556, CX() + 82, 582);
	AddRect(fKeymapButton, {"ui", "keymap", "", "Tasti rapidi"});
}

void RemoteView::DrawArrow(BPoint c, float size, int dir)
{
	BPoint p[3];
	if (dir == 0) { p[0] = BPoint(c.x, c.y - size); p[1] = BPoint(c.x - size, c.y + size * 0.7f); p[2] = BPoint(c.x + size, c.y + size * 0.7f); }
	else if (dir == 2) { p[0] = BPoint(c.x, c.y + size); p[1] = BPoint(c.x - size, c.y - size * 0.7f); p[2] = BPoint(c.x + size, c.y - size * 0.7f); }
	else if (dir == 1) { p[0] = BPoint(c.x + size, c.y); p[1] = BPoint(c.x - size * 0.7f, c.y - size); p[2] = BPoint(c.x - size * 0.7f, c.y + size); }
	else { p[0] = BPoint(c.x - size, c.y); p[1] = BPoint(c.x + size * 0.7f, c.y - size); p[2] = BPoint(c.x + size * 0.7f, c.y + size); }
	BPolygon poly(p, 3);
	FillPolygon(&poly);
}

void RemoteView::DrawRoundButton(BPoint c, float r, IconType icon, rgb_color fill)
{
	// A clean button: a dark rim, then the flat fill, then the icon.
	SetHighColor(18, 18, 22);
	FillEllipse(c, r + 1.5f, r + 1.5f);
	SetHighColor(fill);
	FillEllipse(c, r, r);
	DrawIcon(c, r, icon);
}

void RemoteView::DrawIcon(BPoint c, float r, IconType icon)
{
	SetHighColor(238, 238, 243);
	SetPenSize(std::max(1.5f, r * 0.16f));
	float s = r * 0.55f; // icon half-extent
	switch (icon) {
		case kIconPower:
			StrokeArc(c, s, s, 110, 320); // ring with a gap at the top
			StrokeLine(BPoint(c.x, c.y - s * 1.15f), BPoint(c.x, c.y - s * 0.15f));
			break;
		case kIconBack: {
			SetPenSize(std::max(1.5f, r * 0.14f));
			StrokeArc(c, s, s, 40, 250); // a C-shape
			float a = 40 * (float)M_PI / 180.0f;
			BPoint tip(c.x + s * std::cos(a), c.y - s * std::sin(a));
			BPoint p[3] = {tip, BPoint(tip.x - s * 0.55f, tip.y - s * 0.2f),
				BPoint(tip.x - s * 0.05f, tip.y + s * 0.55f)};
			BPolygon head(p, 3);
			FillPolygon(&head);
			break;
		}
		case kIconHome: {
			BPoint roof[3] = {BPoint(c.x - s, c.y - s * 0.05f), BPoint(c.x, c.y - s),
				BPoint(c.x + s, c.y - s * 0.05f)};
			BPolygon rp(roof, 3);
			FillPolygon(&rp);
			FillRect(BRect(c.x - s * 0.62f, c.y - s * 0.05f, c.x + s * 0.62f, c.y + s * 0.8f));
			SetHighColor(48, 48, 56);
			FillRect(BRect(c.x - s * 0.2f, c.y + s * 0.22f, c.x + s * 0.2f, c.y + s * 0.8f));
			break;
		}
		case kIconMenu:
			for (int i = -1; i <= 1; ++i) {
				float y = c.y + i * s * 0.58f;
				FillRoundRect(BRect(c.x - s, y - r * 0.09f, c.x + s, y + r * 0.09f), 2, 2);
			}
			break;
		case kIconRewind:
		case kIconForward: {
			float dir = (icon == kIconForward) ? 1.0f : -1.0f;
			for (int i = 0; i < 2; ++i) {
				float ox = c.x + dir * (i - 0.5f) * s * 0.95f;
				BPoint tri[3] = {BPoint(ox + dir * s * 0.5f, c.y),
					BPoint(ox - dir * s * 0.45f, c.y - s * 0.7f),
					BPoint(ox - dir * s * 0.45f, c.y + s * 0.7f)};
				BPolygon p(tri, 3);
				FillPolygon(&p);
			}
			break;
		}
		case kIconPlay: {
			BPoint tri[3] = {BPoint(c.x - s * 0.45f, c.y - s * 0.8f),
				BPoint(c.x - s * 0.45f, c.y + s * 0.8f), BPoint(c.x + s * 0.8f, c.y)};
			BPolygon p(tri, 3);
			FillPolygon(&p);
			break;
		}
	}
}

void RemoteView::Draw(BRect)
{
	// Remote body.
	SetHighColor(40, 40, 46);
	FillRoundRect(BRect(6, 6, kW - 7, 553), 26, 26);

	BFont bold(be_bold_font);
	SetFont(&bold);

	// Title.
	SetHighColor(235, 235, 240);
	SetFontSize(15);
	std::string title = fName.empty() ? "Fire TV" : fName;
	if (title.size() > 20)
		title = title.substr(0, 19) + "\xE2\x80\xA6";
	float tw = StringWidth(title.c_str());
	DrawString(title.c_str(), BPoint(CX() - tw / 2, 24));

	// Power.
	DrawRoundButton(BPoint(CX(), 62), 20, kIconPower, (rgb_color){0xd8, 0x3a, 0x34, 0xff});

	// Navigation ring (raised, with a dark groove around the OK centre).
	SetHighColor(16, 16, 20);
	FillEllipse(fNavCenter, fNavOuter + 2, fNavOuter + 2); // outer rim
	SetHighColor(62, 62, 70);
	FillEllipse(fNavCenter, fNavOuter, fNavOuter);         // ring face
	SetHighColor(38, 38, 44);
	FillEllipse(fNavCenter, fNavInner + 9, fNavInner + 9); // groove
	// direction arrows
	SetHighColor(216, 216, 224);
	float ar = fNavOuter - 22;
	DrawArrow(BPoint(fNavCenter.x, fNavCenter.y - ar), 9, 0);
	DrawArrow(BPoint(fNavCenter.x, fNavCenter.y + ar), 9, 2);
	DrawArrow(BPoint(fNavCenter.x + ar, fNavCenter.y), 9, 1);
	DrawArrow(BPoint(fNavCenter.x - ar, fNavCenter.y), 9, 3);
	// OK centre (raised).
	SetHighColor(16, 16, 20);
	FillEllipse(fNavCenter, fNavInner + 1.5f, fNavInner + 1.5f);
	SetHighColor(96, 100, 112);
	FillEllipse(fNavCenter, fNavInner, fNavInner);
	SetHighColor(240, 240, 245);
	SetFontSize(16);
	float ow = StringWidth("OK");
	DrawString("OK", BPoint(fNavCenter.x - ow / 2, fNavCenter.y + 6));

	// Back / Home / Menu (row 1).
	rgb_color btn = {74, 74, 84, 255};
	DrawRoundButton(BPoint(CX() - 72, 320), 24, kIconBack, btn);
	DrawRoundButton(BPoint(CX(),      320), 24, kIconHome, btn);
	DrawRoundButton(BPoint(CX() + 72, 320), 24, kIconMenu, btn);

	// Playback (row 2).
	DrawRoundButton(BPoint(CX() - 72, 392), 24, kIconRewind, btn);
	DrawRoundButton(BPoint(CX(),      392), 26, kIconPlay, (rgb_color){0x2e, 0x8b, 0x57, 0xff});
	DrawRoundButton(BPoint(CX() + 72, 392), 24, kIconForward, btn);

	// App pills with brand accents (order: Prime, Netflix, YouTube, RaiPlay).
	rgb_color brand[4] = {
		{0x00, 0xa8, 0xe1, 0xff}, // Prime Video blue
		{0xe5, 0x09, 0x14, 0xff}, // Netflix red
		{0xff, 0x00, 0x00, 0xff}, // YouTube red
		{0x1a, 0x9e, 0x8f, 0xff}, // RaiPlay teal
	};
	SetFontSize(12);
	int idx = 0;
	for (auto& a : fApps) {
		rgb_color bc = brand[idx % 4];
		SetHighColor((uint8)(bc.red * 0.32f) + 22, (uint8)(bc.green * 0.32f) + 22,
			(uint8)(bc.blue * 0.32f) + 22);
		FillRoundRect(a.first, 9, 9);
		SetHighColor(bc);
		FillRoundRect(BRect(a.first.left + 3, a.first.top + 6, a.first.left + 8,
			a.first.bottom - 6), 2, 2); // colour accent bar
		SetHighColor(232, 232, 238);
		std::string lbl = a.second.label.substr(std::string("Apri ").size());
		float w = StringWidth(lbl.c_str());
		DrawString(lbl.c_str(), BPoint(a.first.left + 14 + (a.first.Width() - 14 - w) / 2,
			a.first.top + a.first.Height() / 2 + 4));
		idx++;
	}

	// Status line.
	SetHighColor(150, 150, 160);
	SetFontSize(11);
	std::string st = fStatus.empty()
		? (fToken.empty() ? std::string(B_TRANSLATE("Non accoppiato")) : fHost) : fStatus;
	float sw = StringWidth(st.c_str());
	DrawString(st.c_str(), BPoint(CX() - sw / 2, 545));

	// "Tasti rapidi" button.
	SetHighColor(44, 44, 52);
	FillRoundRect(fKeymapButton, 6, 6);
	SetHighColor(70, 70, 82);
	StrokeRoundRect(fKeymapButton, 6, 6);
	SetHighColor(220, 220, 228);
	SetFontSize(12);
	std::string kl = std::string("\xE2\x8C\xA8 ") + B_TRANSLATE("Tasti rapidi"); // keyboard glyph
	float kw = StringWidth(kl.c_str());
	DrawString(kl.c_str(), BPoint(CX() - kw / 2, fKeymapButton.top + 18));
}

void RemoteView::MouseDown(BPoint where)
{
	// Navigation ring: inside the outer circle but outside the OK centre -> direction by angle.
	float dx = where.x - fNavCenter.x;
	float dy = where.y - fNavCenter.y;
	float dist = std::sqrt(dx * dx + dy * dy);
	if (dist <= fNavOuter && dist > fNavInner) {
		const char* action;
		if (std::fabs(dx) > std::fabs(dy))
			action = dx > 0 ? "dpad_right" : "dpad_left";
		else
			action = dy > 0 ? "dpad_down" : "dpad_up";
		Send({"nav", action, "", action});
		return;
	}
	// Everything else: the registered circle / rect hits.
	for (const Hit& h : fHits) {
		bool inside = false;
		if (h.shape == Hit::kCircle) {
			float ex = where.x - h.c.x, ey = where.y - h.c.y;
			inside = ex * ex + ey * ey <= h.r * h.r;
		} else {
			inside = h.rect.Contains(where);
		}
		if (inside) {
			if (h.cmd.kind == "ui") {          // not a device command: a UI action
				if (h.cmd.arg == "keymap" && Window() != nullptr)
					Window()->PostMessage(kMsgOpenKeymap);
			} else {
				Send(h.cmd);
			}
			return;
		}
	}
}

void RemoteView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes >= 1) {
		int idx = fKeymap.ActionForKey((uint8)bytes[0]);
		if (idx >= 0) {
			Send(kMapActions[idx].cmd);
			return;
		}
	}
	BView::KeyDown(bytes, numBytes);
}

// Worker: run one command against the device, post the result back.
struct SendJob {
	std::string host;
	std::string token;
	Cmd         cmd;
	BMessenger  reply;
};

static int32 SendThread(void* arg)
{
	SendJob* job = static_cast<SendJob*>(arg);
	FireTVRemote tv(job->host, job->token);
	bool ok = false;
	const Cmd& c = job->cmd;
	if (c.kind == "wake")       ok = tv.Wake();
	else if (c.kind == "nav")   ok = tv.Nav(c.arg);
	else if (c.kind == "media") ok = tv.Media(c.arg, c.scan);
	else if (c.kind == "app")   ok = tv.Launch(c.arg);
	else if (c.kind == "text")  ok = tv.Text(c.arg);

	BMessage msg(kMsgCmdDone);
	msg.AddBool("ok", ok);
	msg.AddString("label", c.label.c_str());
	job->reply.SendMessage(&msg);
	delete job;
	return 0;
}

void RemoteView::Send(const Cmd& cmd)
{
	SetStatus(std::string(B_TRANSLATE(cmd.label.c_str())) + "\xE2\x80\xA6");
	SendJob* job = new SendJob{fHost, fToken, cmd, BMessenger(this)};
	thread_id t = spawn_thread(SendThread, "firetv_cmd", B_NORMAL_PRIORITY, job);
	if (t < 0) {
		delete job;
		SetStatus("Errore invio comando");
		return;
	}
	resume_thread(t);
}

// ---------------------------------------------------------------------------
// The key-mapping GUI: a custom-drawn list of the remote's actions and the key bound to each. Click
// an action to arm it, then press a key to bind it; the change is saved and the remote reloads.
class KeyMapView : public BView {
public:
	explicit KeyMapView(BMessenger target)
		: BView(BRect(0, 0, kMW - 1, kMH - 1), "keymap", B_FOLLOW_ALL, B_WILL_DRAW),
		  fTarget(target)
	{
		SetViewColor(32, 32, 38);
		fKeymap.Load();
		fReset = BRect(18, kMH - 40, 148, kMH - 14);
		fClose = BRect(kMW - 148, kMH - 40, kMW - 18, kMH - 14);
	}

	void AttachedToWindow() override { MakeFocus(true); }
	void WindowActivated(bool active) override { if (active) MakeFocus(true); }

	void Draw(BRect) override
	{
		SetHighColor(32, 32, 38);
		FillRect(Bounds());
		SetFontSize(16);
		SetHighColor(235, 235, 240);
		BFont bold(be_bold_font);
		bold.SetSize(16);
		SetFont(&bold);
		DrawString(B_TRANSLATE("Tasti rapidi"), BPoint(18, 30));
		SetFont(be_plain_font);
		SetFontSize(11);
		SetHighColor(155, 155, 165);
		DrawString(B_TRANSLATE("Clic su un comando, poi premi un tasto."), BPoint(18, 50));
		// Help line: how the shortcuts are used.
		SetHighColor(130, 130, 142);
		DrawString(B_TRANSLATE("I tasti pilotano la TV col telecomando in primo piano."),
			BPoint(18, 68));

		for (int i = 0; i < kNumMapActions; ++i) {
			float y = kRowsTop + i * kRowH;
			if (i % 2 == 0) {
				SetHighColor(38, 38, 45);
				FillRect(BRect(8, y, kMW - 8, y + kRowH - 2));
			}
			SetHighColor(220, 220, 228);
			SetFontSize(13);
			DrawString(B_TRANSLATE(kMapActions[i].label), BPoint(18, y + kRowH * 0.66f));

			BRect box(160, y + 4, kMW - 18, y + kRowH - 6);
			bool armed = (i == fArmed);
			SetHighColor(armed ? rgb_color{0x2e, 0x6f, 0xdb, 0xff} : rgb_color{54, 54, 64, 255});
			FillRoundRect(box, 5, 5);
			SetHighColor(240, 240, 246);
			SetFontSize(12);
			std::string kt = armed ? B_TRANSLATE("premi un tasto...") : KeyName(fKeymap.keys[i]);
			float w = StringWidth(kt.c_str());
			DrawString(kt.c_str(), BPoint(box.left + (box.Width() - w) / 2, y + kRowH * 0.66f));
		}

		DrawButton(fReset, B_TRANSLATE("Predefiniti"));
		DrawButton(fClose, B_TRANSLATE("Chiudi"));
	}

	void MouseDown(BPoint where) override
	{
		if (fReset.Contains(where)) {
			fKeymap.Defaults();
			fKeymap.Save();
			fTarget.SendMessage(kMsgKeymapChanged);
			fArmed = -1;
			Invalidate();
			return;
		}
		if (fClose.Contains(where)) {
			if (Window() != nullptr)
				Window()->PostMessage(B_QUIT_REQUESTED);
			return;
		}
		for (int i = 0; i < kNumMapActions; ++i) {
			float y = kRowsTop + i * kRowH;
			if (where.y >= y && where.y < y + kRowH) {
				fArmed = i;
				MakeFocus(true);
				Invalidate();
				return;
			}
		}
	}

	void KeyDown(const char* bytes, int32 numBytes) override
	{
		if (fArmed >= 0 && numBytes >= 1) {
			uint8 b = (uint8)bytes[0];
			for (int j = 0; j < kNumMapActions; ++j) // a key binds to at most one action
				if (fKeymap.keys[j] == b)
					fKeymap.keys[j] = 0;
			fKeymap.keys[fArmed] = b;
			fKeymap.Save();
			fTarget.SendMessage(kMsgKeymapChanged);
			fArmed = -1;
			Invalidate();
			return;
		}
		BView::KeyDown(bytes, numBytes);
	}

private:
	static constexpr float kMW = 300;
	static constexpr float kRowH = 34;
	static constexpr float kRowsTop = 82; // below the title, hint, and help lines
	static constexpr float kMH = kRowsTop + kNumMapActions * kRowH + 54;

	void DrawButton(BRect r, const char* label)
	{
		SetHighColor(50, 50, 60);
		FillRoundRect(r, 6, 6);
		SetHighColor(78, 78, 92);
		StrokeRoundRect(r, 6, 6);
		SetHighColor(225, 225, 232);
		SetFontSize(12);
		float w = StringWidth(label);
		DrawString(label, BPoint(r.left + (r.Width() - w) / 2, r.top + 18));
	}

	BMessenger fTarget;
	KeyMap     fKeymap;
	int        fArmed = -1;
	BRect      fReset;
	BRect      fClose;
};

class KeyMapWindow : public BWindow {
public:
	explicit KeyMapWindow(BMessenger target)
		: BWindow(BRect(0, 0, 300, 544), B_TRANSLATE("Tasti rapidi"),
			B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		  fTarget(target)
	{
		KeyMapView* view = new KeyMapView(target);
		BLayoutBuilder::Group<>(this, B_VERTICAL, 0).SetInsets(0).Add(view).End();
		view->MakeFocus(true);
		CenterOnScreen();
	}

	bool QuitRequested() override
	{
		fTarget.SendMessage(kMsgKeymapClosed);
		return true;
	}

private:
	BMessenger fTarget;
};

// ---------------------------------------------------------------------------
class RemoteWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	RemoteWindow(const std::string& host, const std::string& name);
	void MessageReceived(BMessage* msg) override;

private:
	void BuildPairing();
	void BuildRemote();

	std::string    fHost;
	std::string    fName;
	RemoteView*    fRemote = nullptr;
	BTextControl*  fPin = nullptr;
	BStringView*   fPairStatus = nullptr;
	KeyMapWindow*  fKeymapWindow = nullptr;
};

RemoteWindow::RemoteWindow(const std::string& host, const std::string& name)
	: BWindow(BRect(120, 120, 120 + 240, 120 + 592), "Fire TV",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	  fHost(host), fName(name)
{
	std::string token = LoadToken(host);
	if (token.empty())
		BuildPairing();
	else
		BuildRemote();
	CenterOnScreen();
}

void RemoteWindow::BuildRemote()
{
	fRemote = new RemoteView(fHost, fName, LoadToken(fHost));
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(fRemote)
		.End();
	// Wake the device on open so the first press lands.
	fRemote->Send({"wake", "", "", "Accensione"});
}

void RemoteWindow::BuildPairing()
{
	BStringView* title = new BStringView("t", B_TRANSLATE("Accoppia il Fire TV"));
	BFont f(be_bold_font);
	f.SetSize(f.Size() * 1.3f);
	title->SetFont(&f);
	BStringView* info = new BStringView("i",
		B_TRANSLATE("1) Premi \"Mostra PIN\": sulla TV appare un codice."));
	BStringView* info2 = new BStringView("i2", B_TRANSLATE("2) Scrivilo qui e premi \"Conferma\"."));
	fPin = new BTextControl("pin", B_TRANSLATE("PIN:"), "", nullptr);
	fPairStatus = new BStringView("ps", fHost.c_str());
	BButton* show = new BButton("show", B_TRANSLATE("Mostra PIN sulla TV"),
		new BMessage(kMsgPairShow));
	BButton* ok = new BButton("ok", B_TRANSLATE("Conferma"), new BMessage(kMsgPairOk));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(info)
		.Add(info2)
		.Add(fPin)
		.Add(fPairStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(show)
			.AddGlue()
			.Add(ok)
		.End()
	.End();
}

void RemoteWindow::MessageReceived(BMessage* msg)
{
	if (msg->what == kMsgCmdDone) {
		bool ok = false;
		msg->FindBool("ok", &ok);
		const char* label = "";
		msg->FindString("label", &label);
		if (fRemote != nullptr) {
			std::string l = B_TRANSLATE(label);
			fRemote->SetStatus(ok ? (l + " \xE2\x9C\x93") : (l + B_TRANSLATE(" (fallito)")));
		}
		return;
	}
	if (msg->what == kMsgOpenKeymap) {
		if (fKeymapWindow != nullptr) {
			fKeymapWindow->Activate();
			return;
		}
		fKeymapWindow = new KeyMapWindow(BMessenger(this));
		fKeymapWindow->Show();
		return;
	}
	if (msg->what == kMsgKeymapChanged) {
		if (fRemote != nullptr)
			fRemote->ReloadKeymap();
		return;
	}
	if (msg->what == kMsgKeymapClosed) {
		fKeymapWindow = nullptr; // the window quit itself
		return;
	}
	if (msg->what == kMsgPairShow) {
		FireTVRemote tv(fHost);
		tv.Wake();
		snooze(400000);
		bool ok = tv.RequestPin("Campiello");
		fPairStatus->SetText(ok ? B_TRANSLATE("PIN mostrato sulla TV: scrivilo qui.")
			: B_TRANSLATE("Impossibile mostrare il PIN (TV raggiungibile?)."));
		return;
	}
	if (msg->what == kMsgPairOk) {
		FireTVRemote tv(fHost);
		std::string token = tv.VerifyPin(fPin->Text());
		if (token.empty()) {
			fPairStatus->SetText(B_TRANSLATE("PIN errato o scaduto. Riprova."));
			return;
		}
		SaveToken(fHost, token);
		// Rebuild as the remote.
		while (BView* c = ChildAt(0)) {
			RemoveChild(c);
			delete c;
		}
		BuildRemote();
		return;
	}
	BWindow::MessageReceived(msg);
}

// ---------------------------------------------------------------------------
class FireTVApp : public BApplication {
public:
	FireTVApp(const std::string& host, const std::string& name)
		: BApplication(kSignature), fHost(host), fName(name) {}

	// Launched by double-clicking a Fire TV device in the WON neighborhood: Tracker opens the
	// device shortcut, whose CAMPIELLO:host/name attributes carry the device. Dispatched before
	// ReadyToRun, so fShown short-circuits the "no device" path below.
	void RefsReceived(BMessage* msg) override
	{
		entry_ref ref;
		for (int32 i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++) {
			BNode node(&ref);
			if (node.InitCheck() != B_OK)
				continue;
			BString host, name;
			ReadAttr(node, "CAMPIELLO:host", host);
			ReadAttr(node, "CAMPIELLO:name", name);
			if (host.Length() == 0)
				continue;
			(new RemoteWindow(host.String(), name.String()))->Show();
			fShown = true;
		}
	}

	void ReadyToRun() override
	{
		if (fShown)
			return;
		if (fHost.empty()) {
			BAlert* a = new BAlert("Fire TV",
				B_TRANSLATE("Nessun dispositivo. Avvia il telecomando dal vicinato WON, "
					"o passa host=<ip>."),
				B_TRANSLATE("Chiudi"));
			a->Go();
			Quit();
			return;
		}
		(new RemoteWindow(fHost, fName))->Show();
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

// Decode a percent-encoded launch value (matches the WON DeviceLaunch encoder).
static std::string DecodeArg(const std::string& v)
{
	auto nyb = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	};
	std::string o;
	for (size_t i = 0; i < v.size(); ++i) {
		if (v[i] == '%' && i + 2 < v.size()) {
			int hi = nyb(v[i + 1]), lo = nyb(v[i + 2]);
			if (hi >= 0 && lo >= 0) { o.push_back((char)((hi << 4) | lo)); i += 2; continue; }
		}
		o.push_back(v[i]);
	}
	return o;
}

#ifndef FIRETV_NO_MAIN
int main(int argc, char** argv)
{
	std::string host, name;
	for (int i = 1; i < argc; ++i) {
		std::string t = argv[i];
		size_t eq = t.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = t.substr(0, eq), val = DecodeArg(t.substr(eq + 1));
		if (key == "host")
			host = val;
		else if (key == "name")
			name = val;
	}
	FireTVApp app(host, name);
	app.Run();
	return 0;
}
#endif // FIRETV_NO_MAIN
