// campiello_vicinato.cpp
//
// The Vicinato (network neighborhood) companion app, a.k.a. WON (World O' Networking): an advanced
// browser of every service found on the LAN, live. It groups devices by category, filters as you
// type, shows a details pane, checks each device's live reachability (a TCP port-check with a
// latency reading), and offers per-device actions and a right-click menu. Opening a device runs its
// ~/WON shortcut, so the matching add-on (or the SMB/SFTP helper) launches with the full metadata.
// It carries no userlandfs unmount hazard (docs/NEIGHBORHOOD.md, Option A).
//
// The look follows the sibling Sotoportego app: a dark slate header with a logo tile, and pastel
// status pills from the same palette. Haiku-only (links libbe). End-user strings are Italian.

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <CheckBox.h>
#include <Clipboard.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Node.h>
#include <OS.h>
#include <OutlineListView.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <Roster.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../bricola/mdns/MdnsRadar.h"
#include "NetworkDirectory.h"
#include "../bricola/mdns/RadarLabels.h"
#include "ShareFolder.h"
#include "SmbHostFinder.h"
#include "NetIntel.h"
#include "OldSchoolDemo.h"
#include "WorldIcon.h"

using namespace campiello::vicinato;
using campiello::bricola::mdns::MdnsRadar;
using campiello::bricola::mdns::RadarSnapshot;

static const char* const kSignature = "application/x-vnd.Campiello-won";
static const char* const kSmbHelperSig = "application/x-vnd.Campiello-smb-mount";
static const char* const kSftpHelperSig = "application/x-vnd.Campiello-mount";

static const uint32 kMsgTick        = 'tick';
static const uint32 kMsgStatusTick  = 'stik';
static const uint32 kMsgStatusReady = 'srdy';
static const uint32 kMsgInvoke      = 'invk';
static const uint32 kMsgSelect      = 'sel_';
static const uint32 kMsgFilter      = 'filt';
static const uint32 kMsgOpen        = 'a_op';
static const uint32 kMsgCopy        = 'a_cp';
static const uint32 kMsgWeb         = 'a_we';
static const uint32 kMsgInfo        = 'a_in';
static const uint32 kMsgContext     = 'a_cx';
static const uint32 kMsgOnlyOnline  = 'olon';
static const uint32 kMsgRefreshNow  = 'rnow';
static const uint32 kMsgInspect     = 'insp';
static const uint32 kMsgAddDevice   = 'addd';
static const uint32 kMsgAddDeviceOk = 'addk';
static const uint32 kMsgRemoveDevice= 'rmvd';
static const uint32 kMsgCopyAll     = 'cpal';
static const uint32 kMsgSsh          = 'sssh'; // open a Terminal running ssh to the selected host
static const uint32 kMsgRdp          = 'srdp'; // open an RDP client to the selected host
static const uint32 kMsgWake         = 'wake'; // Wake-on-LAN the selected device
static const uint32 kMsgEasterEgg    = 'egg!'; // ten clicks on the header globe open the demo
static const uint32 kMsgIntelTick    = 'itik'; // periodic LAN-intel enrichment pass
static const uint32 kMsgIntelReady   = 'irdy'; // enrichment results from the worker thread
static const uint32 kMsgAutostart    = 'auto'; // toggle "open the window at system startup"

// --------------------------------------------------------------------------- palette (Sotoportego)
static const rgb_color kHeaderBg       = {40, 50, 65, 255};
static const rgb_color kHeaderTitle    = {245, 245, 245, 255};
static const rgb_color kHeaderSubtitle = {180, 195, 210, 255};
static const rgb_color kLogoFill       = {90, 155, 213, 255};
static const rgb_color kDotOnline      = {90, 200, 120, 255};
static const rgb_color kDotIdle        = {160, 160, 160, 255};

// Pill tints (good/okay/bad/unknown), matching MetricPill.
static const rgb_color kGoodFill = {220, 245, 225, 255}, kGoodText = {25, 110, 60, 255};
static const rgb_color kOkayFill = {255, 240, 200, 255}, kOkayText = {130, 90, 10, 255};
static const rgb_color kBadFill  = {250, 220, 220, 255}, kBadText  = {150, 35, 35, 255};
static const rgb_color kUnkFill  = {230, 230, 230, 255}, kUnkText  = {110, 110, 110, 255};

enum PillTier { kGood, kOkay, kBad, kUnknown };

static PillTier TierForPing(int ms)
{
	if (ms <= 0) return kUnknown;
	if (ms < 50) return kGood;
	if (ms < 150) return kOkay;
	return kBad;
}

// Draw a rounded status pill whose right edge sits at rightX, vertically centred at cy. Returns the
// pill's left x, so callers can lay out to its left.
static float DrawPill(BView* v, float rightX, float cy, const char* text, PillTier tier)
{
	rgb_color fill, txt;
	switch (tier) {
		case kGood: fill = kGoodFill; txt = kGoodText; break;
		case kOkay: fill = kOkayFill; txt = kOkayText; break;
		case kBad:  fill = kBadFill;  txt = kBadText;  break;
		default:    fill = kUnkFill;  txt = kUnkText;  break;
	}
	BFont font(be_bold_font);
	font.SetSize(10.0f);
	font_height fh;
	font.GetHeight(&fh);
	float tw = font.StringWidth(text);
	float h = fh.ascent + fh.descent + 4;
	float w = tw + 14;
	BRect pill(rightX - w, cy - h / 2, rightX, cy + h / 2);
	v->SetHighColor(fill);
	v->FillRoundRect(pill, h / 2, h / 2);
	v->SetFont(&font);
	v->SetLowColor(fill);
	v->SetHighColor(txt);
	v->SetDrawingMode(B_OP_OVER);
	v->DrawString(text, BPoint(pill.left + 7, cy + (fh.ascent - fh.descent) / 2));
	v->SetFont(be_plain_font);
	return pill.left;
}

// --------------------------------------------------------------------------- live status
enum ReachState { kUnknownState, kOnline, kOffline };
struct DeviceStatus { ReachState state = kUnknownState; int latencyMs = -1; };

// A TCP port-check with a timeout, returning latency in ms (>=1) or -1 (unreachable / no port).
static int TcpPing(const std::string& host, int port, int timeoutMs)
{
	if (port <= 0 || host.empty())
		return -1;
	char portStr[16];
	std::snprintf(portStr, sizeof(portStr), "%d", port);
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || res == nullptr)
		return -1;
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) { freeaddrinfo(res); return -1; }
	fcntl(fd, F_SETFL, O_NONBLOCK);

	bigtime_t start = system_time();
	int result = -1;
	int rc = connect(fd, res->ai_addr, res->ai_addrlen);
	if (rc == 0) {
		result = 1; // connected immediately
	} else {
		fd_set wset;
		FD_ZERO(&wset);
		FD_SET(fd, &wset);
		struct timeval tv;
		tv.tv_sec = timeoutMs / 1000;
		tv.tv_usec = (timeoutMs % 1000) * 1000;
		if (select(fd + 1, nullptr, &wset, nullptr, &tv) > 0) {
			int err = 0;
			socklen_t len = sizeof(err);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
				int ms = (int)((system_time() - start) / 1000);
				result = ms < 1 ? 1 : ms;
			}
		}
	}
	close(fd);
	freeaddrinfo(res);
	return result;
}

struct PingTarget { std::string id, host; int port; };
struct StatusJob { std::vector<PingTarget> targets; BMessenger reply; };

static int32 StatusThread(void* arg)
{
	StatusJob* job = static_cast<StatusJob*>(arg);
	BMessage m(kMsgStatusReady);
	for (const PingTarget& t : job->targets) {
		int ms = TcpPing(t.host, t.port, 800);
		m.AddString("id", t.id.c_str());
		m.AddInt32("latency", ms);
	}
	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

// --------------------------------------------------------------------------- LAN intel (NetIntel)
// A background enrichment pass: read the ARP table (IP->MAC), ask each SMB host for its NetBIOS
// name, and run one SSDP/UPnP sweep. All blocking network work, off the window thread. The results
// go back to the window, which folds them into the service list and the details pane.
struct IntelJob {
	std::vector<std::pair<std::string, std::string>> targets; // (id, host) needing a MAC
	std::vector<std::string>                         smbIps;   // SMB host IPs to name via NetBIOS
	BMessenger                                       reply;
};

static bool IsDottedIp(const std::string& h)
{
	struct in_addr a;
	return !h.empty() && inet_pton(AF_INET, h.c_str(), &a) == 1;
}

static int32 IntelThread(void* arg)
{
	IntelJob* job = static_cast<IntelJob*>(arg);
	BMessage m(kMsgIntelReady);

	// 1. ARP: resolve a MAC for every target host that is a dotted IP.
	std::map<std::string, std::string> arp = campiello::vicinato::ReadArpCache();
	for (const auto& t : job->targets) {
		if (!IsDottedIp(t.second))
			continue;
		auto it = arp.find(t.second);
		if (it == arp.end())
			continue;
		m.AddString("mac_ip", t.second.c_str());
		m.AddString("mac", it->second.c_str());
	}

	// 2. NetBIOS: name each SMB host that answers on UDP 137.
	for (const std::string& ip : job->smbIps) {
		std::string name, workgroup;
		if (campiello::vicinato::QueryNetBiosName(ip, 700, name, workgroup)) {
			m.AddString("nb_ip", ip.c_str());
			m.AddString("nb_name", name.c_str());
			m.AddString("nb_wg", workgroup.c_str());
		}
	}

	// 3. SSDP/UPnP: one M-SEARCH burst.
	auto ssdp = campiello::vicinato::DiscoverSsdp(1500);
	for (const auto& kv : ssdp) {
		m.AddString("ssdp_ip", kv.first.c_str());
		m.AddString("ssdp_type", campiello::vicinato::InferSsdpType(kv.second).c_str());
		m.AddString("ssdp_server", kv.second.server.c_str());
	}

	job->reply.SendMessage(&m);
	delete job;
	return 0;
}

namespace {

const char* KindLabel(ServiceKind k)
{
	switch (k) {
		case ServiceKind::Campiello: return "Campiello";
		case ServiceKind::Computer:  return "Computer";
		case ServiceKind::Smb:       return "Windows";
		case ServiceKind::Sftp:      return "SSH/SFTP";
		case ServiceKind::Home:      return "Casa";
		case ServiceKind::Web:       return "Web";
		case ServiceKind::Printer:   return "Stampante";
		case ServiceKind::Media:     return "Media";
		default:                     return "Altro";
	}
}

rgb_color KindColor(ServiceKind k)
{
	switch (k) {
		case ServiceKind::Campiello: return (rgb_color){0x2a, 0x6f, 0xdb, 0xff};
		case ServiceKind::Computer:  return (rgb_color){0x45, 0x5a, 0x74, 0xff};
		case ServiceKind::Smb:       return (rgb_color){0x00, 0x78, 0xd7, 0xff};
		case ServiceKind::Sftp:      return (rgb_color){0x2e, 0x8b, 0x57, 0xff};
		case ServiceKind::Home:      return (rgb_color){0xd3, 0x8b, 0x2d, 0xff};
		case ServiceKind::Web:       return (rgb_color){0xe8, 0x7a, 0x00, 0xff};
		case ServiceKind::Printer:   return (rgb_color){0x70, 0x70, 0x70, 0xff};
		case ServiceKind::Media:     return (rgb_color){0x8e, 0x44, 0xad, 0xff};
		default:                     return (rgb_color){0x99, 0x99, 0x99, 0xff};
	}
}

const char* KindGlyph(ServiceKind k)
{
	switch (k) {
		case ServiceKind::Campiello: return "C";
		case ServiceKind::Computer:  return "PC";
		case ServiceKind::Smb:       return "W";
		case ServiceKind::Sftp:      return "S";
		case ServiceKind::Home:      return "H";
		case ServiceKind::Web:       return "@";
		case ServiceKind::Printer:   return "P";
		case ServiceKind::Media:     return "M";
		default:                     return "?";
	}
}

std::string Lower(const std::string& s)
{
	std::string o = s;
	for (char& c : o) c = (char)tolower((unsigned char)c);
	return o;
}

std::string ShortcutName(const std::string& label)
{
	std::string out = label;
	for (char& c : out)
		if (c == '/' || c == '\0')
			c = '-';
	return out.empty() ? std::string("condivisione") : out;
}

// Map a discovered service to an icon base name (a file "<name>.hvif"): by service type first, then
// by kind. The full set of names is documented in docs/ICONS.md.
std::string IconBaseName(const NetworkService& s)
{
	std::string t = s.serviceType;
	size_t dot = t.find(".local");
	if (dot != std::string::npos)
		t = t.substr(0, dot);
	static const struct { const char* type; const char* name; } kMap[] = {
		{"_campiello._tcp", "campiello"},
		{"_smb._tcp", "smb"}, {"_sftp-ssh._tcp", "ssh"}, {"_ssh._tcp", "ssh"},
		{"_ftp._tcp", "ftp"}, {"_webdav._tcp", "webdav"}, {"_webdavs._tcp", "webdav"},
		{"_nfs._tcp", "nfs"}, {"_afpovertcp._tcp", "afp"},
		{"_hue._tcp", "hue"}, {"_hap._tcp", "homekit"},
		{"_matter._tcp", "matter"}, {"_matterc._udp", "matter"}, {"_matterd._udp", "matter"},
		{"_sleap._tcp", "lutron"},
		{"_airplay._tcp", "airplay"}, {"_raop._tcp", "airplay"},
		{"_googlecast._tcp", "cast"}, {"_spotify-connect._tcp", "spotify"},
		{"_amzn-wplay._tcp", "firetv"}, {"_amzn-alexa._tcp", "alexa"}, {"_daap._tcp", "daap"},
		{"_ipp._tcp", "printer"}, {"_ipps._tcp", "printer"}, {"_printer._tcp", "printer"},
		{"_pdl-datastream._tcp", "printer"}, {"_uscan._tcp", "scanner"}, {"_scanner._tcp", "scanner"},
		{"_http._tcp", "web"}, {"_https._tcp", "web"}, {"_rfb._tcp", "vnc"},
		{"_workstation._tcp", "computer"}, {"_device-info._tcp", "computer"},
		{"_companion-link._tcp", "computer"},
	};
	for (const auto& m : kMap)
		if (t == m.type)
			return m.name;
	switch (s.kind) {
		case ServiceKind::Campiello: return "campiello";
		case ServiceKind::Computer:  return "computer";
		case ServiceKind::Smb:       return "smb";
		case ServiceKind::Sftp:      return "ssh";
		case ServiceKind::Home:      return "home";
		case ServiceKind::Web:       return "web";
		case ServiceKind::Printer:   return "printer";
		case ServiceKind::Media:     return "media";
		default:                     return "other";
	}
}

// Render "<name>.hvif" (user dir first, then the installed system dir) into a bitmap; null if absent.
BBitmap* LoadHvifIcon(const std::string& name, float size)
{
	std::string paths[2];
	char buf[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, buf, sizeof(buf)) == B_OK)
		paths[0] = std::string(buf) + "/Campiello/icons/" + name + ".hvif";
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, -1, false, buf, sizeof(buf)) == B_OK)
		paths[1] = std::string(buf) + "/campiello/icons/" + name + ".hvif";
	for (const std::string& p : paths) {
		if (p.empty())
			continue;
		BFile f(p.c_str(), B_READ_ONLY);
		if (f.InitCheck() != B_OK)
			continue;
		off_t len = 0;
		if (f.GetSize(&len) != B_OK || len <= 0 || len > 256 * 1024)
			continue;
		std::vector<uint8> data((size_t)len);
		if (f.Read(data.data(), (size_t)len) != (ssize_t)len)
			continue;
		BBitmap* bmp = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
		if (bmp->InitCheck() != B_OK) { delete bmp; continue; }
		if (BIconUtils::GetVectorIcon(data.data(), (size_t)len, bmp) != B_OK) { delete bmp; continue; }
		return bmp;
	}
	return nullptr;
}

// The dark slate header with a logo tile, title, subtitle and an overall status dot (Sotoportego).
class HeaderView : public BView {
public:
	HeaderView()
		: BView("header", B_WILL_DRAW | B_SUPPORTS_LAYOUT | B_FULL_UPDATE_ON_RESIZE),
		  fSubtitle("Ricerca in corso...")
	{
		SetViewColor(kHeaderBg);
		SetLowColor(kHeaderBg);
	}

	~HeaderView() override { delete fLogo; }

	void SetSubtitle(const char* s) { fSubtitle = s; Invalidate(); }
	void SetOnline(bool any) { fAnyOnline = any; Invalidate(); }

	BSize MinSize() override { return BSize(240, 64); }
	BSize MaxSize() override { return BSize(B_SIZE_UNLIMITED, 64); }
	BSize PreferredSize() override { return BSize(360, 64); }

	// Ten clicks on the globe tile summon the old-school demo (a wink to the demoscene).
	void MouseDown(BPoint where) override
	{
		BRect tile(14, 12, 54, 52);
		if (!tile.Contains(where)) {
			fLogoClicks = 0;
			return;
		}
		if (++fLogoClicks >= 10) {
			fLogoClicks = 0;
			if (Window() != nullptr)
				Window()->PostMessage(kMsgEasterEgg);
		}
	}

	void Draw(BRect) override
	{
		SetHighColor(kHeaderBg);
		FillRect(Bounds());

		// Logo tile: the Campiello world/globe, the same icon shown in the Deskbar and Tracker,
		// rendered once from the embedded HVIF. Falls back to a blue tile if rendering fails.
		BRect tile(14, 12, 54, 52);
		EnsureLogo();
		if (fLogo != nullptr) {
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			DrawBitmap(fLogo, fLogo->Bounds(), tile);
			SetDrawingMode(B_OP_COPY);
		} else {
			SetHighColor(kLogoFill);
			FillRoundRect(tile, 8, 8);
		}

		// Overall status dot at the tile's bottom-right.
		SetHighColor(fAnyOnline ? kDotOnline : kDotIdle);
		FillEllipse(BPoint(tile.right - 2, tile.bottom - 2), 4.5f, 4.5f);
		SetHighColor(kHeaderBg);
		SetPenSize(1.5f);
		StrokeEllipse(BPoint(tile.right - 2, tile.bottom - 2), 4.5f, 4.5f);

		SetDrawingMode(B_OP_OVER);
		BFont titleFont(be_bold_font);
		titleFont.SetSize(19.0f);
		SetFont(&titleFont);
		SetHighColor(kHeaderTitle);
		float wonW = StringWidth("Campiello WON");
		DrawString("Campiello WON", BPoint(68, 30));

		BFont small(be_plain_font);
		small.SetSize(11.0f);
		SetFont(&small);
		SetHighColor(kHeaderSubtitle);
		DrawString("World O' Networking", BPoint(68 + wonW + 12, 30));

		DrawString(fSubtitle.String(), BPoint(68, 49));
		SetFont(be_plain_font);
	}

private:
	void EnsureLogo()
	{
		if (fLogo != nullptr || fLogoTried)
			return;
		fLogoTried = true;
		BBitmap* bmp = new BBitmap(BRect(0, 0, 39, 39), B_RGBA32);
		if (bmp->InitCheck() == B_OK
			&& BIconUtils::GetVectorIcon(kWorldIconHVIF, kWorldIconHVIFLen, bmp) == B_OK) {
			fLogo = bmp;
		} else {
			delete bmp;
		}
	}

	BString  fSubtitle;
	bool     fAnyOnline = false;
	BBitmap* fLogo = nullptr;
	bool     fLogoTried = false;
	int      fLogoClicks = 0;
};

class GroupItem : public BListItem {
public:
	GroupItem(std::string text, ServiceKind kind) : BListItem(0, true), fText(std::move(text)),
		fKind(kind) {}

	void Update(BView* owner, const BFont* font) override
	{
		BListItem::Update(owner, font);
		font_height fh;
		font->GetHeight(&fh);
		SetHeight(fh.ascent + fh.descent + 10);
	}

	void DrawItem(BView* owner, BRect frame, bool) override
	{
		owner->SetLowColor(tint_color(ui_color(B_LIST_BACKGROUND_COLOR), B_DARKEN_1_TINT));
		owner->FillRect(frame, B_SOLID_LOW);
		owner->SetHighColor(KindColor(fKind));
		owner->FillRect(BRect(frame.left, frame.top, frame.left + 3, frame.bottom));
		BFont bold(be_bold_font);
		owner->SetFont(&bold);
		owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
		font_height fh;
		owner->GetFontHeight(&fh);
		owner->DrawString(fText.c_str(),
			BPoint(frame.left + 12, frame.top + (frame.Height() + fh.ascent - fh.descent) / 2));
		owner->SetFont(be_plain_font);
	}

private:
	std::string fText;
	ServiceKind fKind;
};

// A device row with a kind badge, label + address, a live-status pill, and a login padlock. It reads
// the status from a map owned by the window (keyed by service id).
class ServiceListItem : public BListItem {
public:
	ServiceListItem(std::string text, ServiceKind kind, bool locked, int index, std::string id,
		const std::map<std::string, DeviceStatus>* status, std::string iconName,
		const std::map<std::string, BBitmap*>* icons)
		: BListItem(1), fText(std::move(text)), fKind(kind), fLocked(locked), fIndex(index),
		  fId(std::move(id)), fStatus(status), fIconName(std::move(iconName)), fIcons(icons) {}

	int Index() const { return fIndex; }

	void Update(BView* owner, const BFont* font) override
	{
		BListItem::Update(owner, font);
		font_height fh;
		font->GetHeight(&fh);
		float h = fh.ascent + fh.descent + fh.leading + 12;
		SetHeight(h < 28 ? 28 : h);
	}

	void DrawItem(BView* owner, BRect frame, bool complete) override
	{
		const bool selected = IsSelected();
		if (selected) {
			owner->SetLowColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
			owner->FillRect(frame, B_SOLID_LOW);
		} else if (complete) {
			owner->SetLowColor(owner->ViewColor());
			owner->FillRect(frame, B_SOLID_LOW);
		}

		const float pad = 5;
		const float side = frame.Height() - 2 * pad;
		BRect badge(frame.left + pad, frame.top + pad, frame.left + pad + side, frame.top + pad + side);

		// A real HVIF icon for this service, if one is installed; otherwise the coloured letter badge.
		BBitmap* icon = nullptr;
		if (fIcons != nullptr) {
			auto it = fIcons->find(fIconName);
			if (it != fIcons->end())
				icon = it->second;
		}
		if (icon != nullptr) {
			drawing_mode dm = owner->DrawingMode();
			owner->SetDrawingMode(B_OP_ALPHA);
			owner->DrawBitmap(icon, icon->Bounds(), badge);
			owner->SetDrawingMode(dm);
		} else {
			owner->SetHighColor(KindColor(fKind));
			owner->FillRoundRect(badge, 4, 4);
			owner->SetHighColor(255, 255, 255);
			if (fKind == ServiceKind::Home) {
				float cx = badge.left + side / 2;
				float bodyTop = badge.top + side * 0.46f, bodyBottom = badge.top + side * 0.74f;
				float bodyL = badge.left + side * 0.34f, bodyR = badge.left + side * 0.66f;
				owner->FillRect(BRect(bodyL, bodyTop, bodyR, bodyBottom));
				BPoint roof[3] = {BPoint(badge.left + side * 0.28f, bodyTop),
					BPoint(cx, badge.top + side * 0.28f), BPoint(badge.left + side * 0.72f, bodyTop)};
				owner->FillPolygon(roof, 3);
			} else {
				BFont bold(be_bold_font);
				owner->SetFont(&bold);
				const char* glyph = KindGlyph(fKind);
				font_height bh;
				owner->GetFontHeight(&bh);
				float gw = owner->StringWidth(glyph);
				owner->DrawString(glyph, BPoint(badge.left + (side - gw) / 2,
					badge.top + (side + bh.ascent - bh.descent) / 2));
				owner->SetFont(be_plain_font);
			}
		}

		float cy = frame.top + frame.Height() / 2;

		// Live-status pill on the right (green/amber online with latency, red offline).
		float rightEdge = frame.right - 8;
		float pillLeft = rightEdge;
		if (fStatus != nullptr) {
			auto it = fStatus->find(fId);
			if (it != fStatus->end() && it->second.state != kUnknownState) {
				char label[24];
				PillTier tier;
				if (it->second.state == kOnline) {
					if (it->second.latencyMs > 0) {
						std::snprintf(label, sizeof(label), "%d ms", it->second.latencyMs);
						tier = TierForPing(it->second.latencyMs);
					} else {
						// Reachable on the LAN (ARP) but no service port answered: no latency to show.
						std::snprintf(label, sizeof(label), "in rete");
						tier = kGood;
					}
				} else {
					std::snprintf(label, sizeof(label), "offline");
					tier = kBad;
				}
				pillLeft = DrawPill(owner, rightEdge, cy, label, tier);
			}
		}

		if (fLocked) {
			const float ls = 9;
			float lx = pillLeft - ls - 10, ly = cy - ls / 2 + 1;
			owner->SetHighColor(selected ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
				: ui_color(B_LIST_ITEM_TEXT_COLOR));
			owner->FillRect(BRect(lx, ly, lx + ls, ly + ls * 0.75f));
			owner->StrokeArc(BRect(lx + 1.5f, ly - ls * 0.55f, lx + ls - 1.5f, ly + ls * 0.35f), 0, 180);
			pillLeft = lx;
		}

		owner->SetHighColor(selected ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR));
		font_height ph;
		owner->GetFontHeight(&ph);
		float ty = cy + (ph.ascent - ph.descent) / 2;
		// clip the label so it does not run under the pill
		BRect clip(badge.right + 8, frame.top, pillLeft - 6, frame.bottom);
		owner->ConstrainClippingRegion(new BRegion(clip));
		owner->DrawString(fText.c_str(), BPoint(badge.right + 8, ty));
		owner->ConstrainClippingRegion(nullptr);
	}

private:
	std::string fText;
	ServiceKind fKind;
	bool        fLocked;
	int         fIndex;
	std::string fId;
	const std::map<std::string, DeviceStatus>* fStatus;
	std::string fIconName;
	const std::map<std::string, BBitmap*>* fIcons;
};

class DeviceOutlineView : public BOutlineListView {
public:
	DeviceOutlineView(const char* name, BMessage* invoke)
		: BOutlineListView(name, B_SINGLE_SELECTION_LIST) { SetInvocationMessage(invoke); }

	void MouseDown(BPoint where) override
	{
		uint32 buttons = 0;
		if (Window() != nullptr && Window()->CurrentMessage() != nullptr)
			Window()->CurrentMessage()->FindInt32("buttons", (int32*)&buttons);
		int32 idx = IndexOf(where);
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0 && idx >= 0) {
			Select(idx);
			BMessage msg(kMsgContext);
			msg.AddPoint("screen_where", ConvertToScreen(where));
			if (Window() != nullptr)
				Window()->PostMessage(&msg);
			return;
		}
		BOutlineListView::MouseDown(where);
	}
};

} // namespace

// A small read-only window showing a device's raw mDNS records (SRV/TXT/A), for troubleshooting.
class InspectorWindow : public BWindow {
public:
	InspectorWindow(const char* title, const std::string& text)
		: BWindow(BRect(0, 0, 440, 400), title, B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		  fText(text)
	{
		BTextView* tv = new BTextView("t");
		tv->MakeEditable(false);
		tv->MakeSelectable(true);
		BFont mono(be_fixed_font);
		tv->SetFontAndColor(&mono);
		tv->SetText(text.c_str());
		tv->SetExplicitMinSize(BSize(400, 300));
		BScrollView* sc = new BScrollView("sc", tv, 0, false, true);
		BButton* copy = new BButton("copy", "Copia tutto", new BMessage(kMsgCopyAll));
		BButton* close = new BButton("close", "Chiudi", new BMessage(B_QUIT_REQUESTED));
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(sc)
			.AddGroup(B_HORIZONTAL).Add(copy).AddGlue().Add(close).End()
		.End();
		CenterOnScreen();
	}

	void MessageReceived(BMessage* m) override
	{
		if (m->what == kMsgCopyAll) {
			if (be_clipboard->Lock()) {
				be_clipboard->Clear();
				BMessage* data = be_clipboard->Data();
				if (data != nullptr) {
					data->AddData("text/plain", B_MIME_TYPE, fText.data(), fText.size());
					be_clipboard->Commit();
				}
				be_clipboard->Unlock();
			}
			return;
		}
		BWindow::MessageReceived(m);
	}

private:
	std::string fText;
};

// A form to add a device by address when mDNS does not see it. On confirm it posts kMsgAddDeviceOk
// (name/host/port) to the main window.
class AddDeviceWindow : public BWindow {
public:
	explicit AddDeviceWindow(BMessenger target)
		: BWindow(BRect(0, 0, 320, 160), "Aggiungi dispositivo", B_TITLED_WINDOW,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		  fTarget(target)
	{
		fName = new BTextControl("Nome:", "", nullptr);
		fHost = new BTextControl("Indirizzo:", "", nullptr);
		fPort = new BTextControl("Porta:", "", nullptr);
		BButton* ok = new BButton("ok", "Aggiungi", new BMessage('okk_'));
		BButton* cancel = new BButton("cancel", "Annulla", new BMessage(B_QUIT_REQUESTED));
		ok->MakeDefault(true);
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fName)
			.Add(fHost)
			.Add(fPort)
			.AddGroup(B_HORIZONTAL).AddGlue().Add(cancel).Add(ok).End()
		.End();
		CenterOnScreen();
	}

	void MessageReceived(BMessage* m) override
	{
		if (m->what == 'okk_') {
			std::string host = fHost->Text();
			if (host.empty()) {
				(new BAlert("Aggiungi", "Inserisci un indirizzo (host o IP).", "OK"))->Go();
				return;
			}
			BMessage out(kMsgAddDeviceOk);
			out.AddString("name", fName->Text());
			out.AddString("host", host.c_str());
			out.AddInt32("port", atoi(fPort->Text()));
			fTarget.SendMessage(&out);
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		BWindow::MessageReceived(m);
	}

private:
	BMessenger    fTarget;
	BTextControl* fName;
	BTextControl* fHost;
	BTextControl* fPort;
};

// The details panel: a wrapping device-name title, a coloured status line, and label/value rows with
// alternating (zebra) backgrounds. Minor rows live in collapsible groups the user can fold by
// clicking the group header. Scrolls inside its BScrollView. Content is set with SetEmpty() or the
// SetHeader()/AddField()/AddGroup()/AddGroupField()/Commit() sequence.
class DetailTable : public BView {
public:
	DetailTable()
		: BView("detail", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewColor(255, 255, 255);
		fTitleFont = *be_bold_font;
		fTitleFont.SetSize(fTitleFont.Size() * 1.25f);
	}

	BSize MinSize() override { return BSize(250, 200); }
	BSize MaxSize() override { return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED); }

	void SetEmpty(const char* msg)
	{
		fRows.clear(); fGroups.clear(); fTitle.clear(); fStatus.clear();
		fEmpty = msg;
		Relayout(); Invalidate();
	}
	void SetHeader(const std::string& title, const std::string& status, rgb_color statusColor)
	{
		fRows.clear(); fGroups.clear(); fEmpty.clear();
		fTitle = title; fStatus = status; fStatusColor = statusColor;
	}
	void AddField(const std::string& label, const std::string& value)
	{
		fRows.push_back({0, -1, label, value});
	}
	// Returns a group id for AddGroupField. Remembers a user's fold choice across selections.
	int AddGroup(const std::string& title, bool defaultCollapsed = false)
	{
		bool collapsed = defaultCollapsed;
		auto it = fCollapse.find(title);
		if (it != fCollapse.end())
			collapsed = it->second;
		fGroups.push_back({title, collapsed});
		return (int)fGroups.size() - 1;
	}
	void AddGroupField(int group, const std::string& label, const std::string& value)
	{
		fRows.push_back({1, group, label, value});
	}
	void Commit() { Relayout(); Invalidate(); }

	void FrameResized(float w, float h) override
	{
		BView::FrameResized(w, h);
		Relayout(); Invalidate();
	}

	void MouseDown(BPoint where) override
	{
		for (const auto& hit : fHeaderHits) {
			if (hit.first.Contains(where)) {
				bool& c = fGroups[hit.second].collapsed;
				c = !c;
				fCollapse[fGroups[hit.second].title] = c;
				Relayout(); Invalidate();
				return;
			}
		}
	}

	void Draw(BRect) override
	{
		SetHighColor(255, 255, 255);
		FillRect(Bounds());
		if (!fEmpty.empty()) {
			SetFont(be_plain_font);
			SetHighColor(130, 135, 145);
			font_height fh; be_plain_font->GetHeight(&fh);
			DrawString(fEmpty.c_str(), BPoint(10, 14 + fh.ascent));
			return;
		}
		font_height ph; be_plain_font->GetHeight(&ph);
		float lineH = ceilf(ph.ascent + ph.descent + ph.leading);
		rgb_color zebraA = {255, 255, 255, 255};
		rgb_color zebraB = {244, 246, 249, 255};
		rgb_color headerBg = {225, 230, 237, 255};
		rgb_color labelCol = {70, 80, 95, 255};
		rgb_color valueCol = {40, 44, 52, 255};

		for (const DrawItem& d : fDraw) {
			if (d.kind == 2) { // title (wrapping)
				SetFont(&fTitleFont);
				SetHighColor(30, 36, 46);
				font_height th; fTitleFont.GetHeight(&th);
				float ty = d.top + th.ascent;
				float tstep = ceilf(th.ascent + th.descent + th.leading);
				for (const std::string& ln : d.vlines) {
					DrawString(ln.c_str(), BPoint(kPad, ty));
					ty += tstep;
				}
			} else if (d.kind == 3) { // status
				SetFont(be_plain_font);
				SetHighColor(d.color);
				DrawString(d.label.c_str(), BPoint(kPad, d.top + ph.ascent));
			} else if (d.kind == 1) { // group header
				SetHighColor(headerBg);
				FillRect(BRect(0, d.top, Bounds().right, d.top + d.h - 1));
				SetHighColor(150, 160, 172);
				// disclosure triangle
				float cy = d.top + d.h / 2;
				BPoint t1, t2, t3;
				if (d.collapsed) { t1.Set(kPad, cy - 4); t2.Set(kPad, cy + 4); t3.Set(kPad + 6, cy); }
				else { t1.Set(kPad, cy - 3); t2.Set(kPad + 8, cy - 3); t3.Set(kPad + 4, cy + 4); }
				FillTriangle(t1, t2, t3);
				SetFont(be_bold_font);
				SetHighColor(60, 70, 84);
				DrawString(d.label.c_str(), BPoint(kPad + 14, d.top + (d.h - lineH) / 2 + ph.ascent));
			} else { // field row (major or group), zebra background
				SetHighColor((d.zebra & 1) ? zebraB : zebraA);
				FillRect(BRect(0, d.top, Bounds().right, d.top + d.h - 1));
				SetFont(be_bold_font);
				SetHighColor(labelCol);
				DrawString(d.label.c_str(), BPoint(kPad + (d.group ? 14 : 0), d.top + kVPad + ph.ascent));
				SetFont(be_plain_font);
				SetHighColor(valueCol);
				float vy = d.top + kVPad + ph.ascent;
				for (const std::string& ln : d.vlines) {
					DrawString(ln.c_str(), BPoint(fValueX, vy));
					vy += lineH;
				}
			}
		}
	}

private:
	struct Row { int type; int group; std::string label, value; }; // type 0 major, 1 group field
	struct Group { std::string title; bool collapsed; };
	struct DrawItem {
		int kind; float top, h;      // kind: 0 field, 1 group header, 2 title, 3 status
		int group;                   // 0/1: is this a group field (indent)?
		std::string label;
		std::vector<std::string> vlines;
		rgb_color color;
		bool collapsed;
		int zebra;
	};

	std::vector<std::string> Wrap(BFont& font, const std::string& s, float maxW) const
	{
		std::vector<std::string> out;
		if (s.empty()) { out.push_back(""); return out; }
		std::string line;
		size_t i = 0;
		while (i < s.size()) {
			size_t sp = s.find(' ', i);
			std::string word = s.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
			std::string cand = line.empty() ? word : line + " " + word;
			if (font.StringWidth(cand.c_str()) <= maxW || line.empty()) {
				// hard-break a single word too wide for the column
				if (line.empty() && font.StringWidth(word.c_str()) > maxW) {
					std::string chunk;
					for (char c : word) {
						std::string t = chunk + c;
						if (font.StringWidth(t.c_str()) > maxW && !chunk.empty()) {
							out.push_back(chunk); chunk = c;
						} else chunk = t;
					}
					line = chunk;
				} else {
					line = cand;
				}
			} else {
				out.push_back(line); line = word;
			}
			if (sp == std::string::npos) break;
			i = sp + 1;
		}
		if (!line.empty() || out.empty()) out.push_back(line);
		return out;
	}

	void Relayout()
	{
		fDraw.clear();
		fHeaderHits.clear();
		float width = Bounds().Width();
		if (width < 60) width = 260;
		if (!fEmpty.empty()) { fContentHeight = 40; UpdateScrollBar(); return; }

		font_height ph; be_plain_font->GetHeight(&ph);
		float lineH = ceilf(ph.ascent + ph.descent + ph.leading);

		// Value column start: widest label (bold) capped to 42% of the width.
		BFont bold(be_bold_font);
		float labelW = 0;
		for (const Row& r : fRows)
			labelW = std::max(labelW, bold.StringWidth((r.label + ":").c_str()));
		float cap = width * 0.42f;
		if (labelW > cap) labelW = cap;
		fValueX = kPad + labelW + 10;
		float valMaxW = width - fValueX - kPad;
		if (valMaxW < 40) valMaxW = 40;

		float y = kTopPad;

		// Title (wrapping).
		if (!fTitle.empty()) {
			font_height th; fTitleFont.GetHeight(&th);
			float tstep = ceilf(th.ascent + th.descent + th.leading);
			BFont tf = fTitleFont;
			std::vector<std::string> lines = Wrap(tf, fTitle, width - 2 * kPad);
			DrawItem d; d.kind = 2; d.top = y; d.vlines = lines;
			d.h = lines.size() * tstep + 4;
			fDraw.push_back(d);
			y += d.h;
		}
		// Status.
		if (!fStatus.empty()) {
			DrawItem d; d.kind = 3; d.top = y; d.label = fStatus; d.color = fStatusColor;
			d.h = lineH + 6;
			fDraw.push_back(d);
			y += d.h;
		}
		y += 4;

		int zebra = 0;
		BFont plain(be_plain_font);
		auto emitField = [&](const Row& r) {
			DrawItem d; d.kind = 0; d.top = y; d.group = r.group == -1 ? 0 : 1;
			d.label = r.label + ":";
			d.vlines = Wrap(plain, r.value, valMaxW);
			d.zebra = zebra++;
			int nl = (int)d.vlines.size(); if (nl < 1) nl = 1;
			d.h = nl * lineH + 2 * kVPad;
			fDraw.push_back(d);
			y += d.h;
		};

		// Major fields (group == -1) first.
		for (const Row& r : fRows)
			if (r.type == 0)
				emitField(r);

		// Then each group: a clickable header, then its fields when expanded.
		for (int g = 0; g < (int)fGroups.size(); ++g) {
			bool any = false;
			for (const Row& r : fRows) if (r.type == 1 && r.group == g) { any = true; break; }
			if (!any) continue;
			DrawItem h; h.kind = 1; h.top = y; h.h = lineH + 8; h.label = fGroups[g].title;
			h.collapsed = fGroups[g].collapsed; h.group = g;
			fDraw.push_back(h);
			fHeaderHits.push_back({BRect(0, y, Bounds().right, y + h.h - 1), g});
			y += h.h;
			if (!fGroups[g].collapsed)
				for (const Row& r : fRows)
					if (r.type == 1 && r.group == g)
						emitField(r);
		}

		fContentHeight = y + kTopPad;
		UpdateScrollBar();
	}

	void UpdateScrollBar()
	{
		BScrollBar* sb = ScrollBar(B_VERTICAL);
		if (sb == nullptr) return;
		float vp = Bounds().Height();
		float range = fContentHeight - vp;
		if (range < 0) range = 0;
		sb->SetRange(0, range);
		sb->SetProportion(fContentHeight > 0 ? vp / fContentHeight : 1.0f);
		sb->SetSteps(16, vp > 0 ? vp : 200);
		if (range == 0)
			ScrollTo(0, 0);
	}

	static const float kPad;
	static const float kVPad;
	static const float kTopPad;

	std::vector<Row>    fRows;
	std::vector<Group>  fGroups;
	std::string         fTitle, fStatus, fEmpty;
	rgb_color           fStatusColor = {0, 0, 0, 255};
	BFont               fTitleFont;
	std::map<std::string, bool> fCollapse; // persisted fold state by group title
	float               fValueX = 90;
	float               fContentHeight = 0;
	std::vector<DrawItem> fDraw;
	std::vector<std::pair<BRect, int>> fHeaderHits;
};

const float DetailTable::kPad = 10;
const float DetailTable::kVPad = 5;
const float DetailTable::kTopPad = 8;


class VicinatoWindow : public BWindow {
public:
	VicinatoWindow();
	~VicinatoWindow() override;
	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void Refresh();
	void RebuildList();
	void UpdateDetails();
	void PopulateDetails(const NetworkService& svc, const std::string& statusText,
		rgb_color statusColor);
	void StartStatusCheck();
	void ApplyStatus(BMessage* msg);
	const NetworkService* Selected() const;
	void OpenService(const NetworkService& svc);
	void OpenWebUi(const NetworkService& svc);
	void CopyAddress(const NetworkService& svc);
	void InspectSelected();
	std::string InspectorText(const NetworkService& svc) const;
	void AddManual(BMessage* msg);
	void RemoveSelectedManual();
	void LoadManual();
	void SaveManual();
	std::string DetailsText(const NetworkService& svc) const;
	void ShowContextMenu(BPoint screenWhere);
	void StartIntel();
	void ApplyIntel(BMessage* msg);
	void ApplyEnrichment(std::vector<NetworkService>& hood) const;
	void WakeSelected();
	void OpenSshTerminal(const NetworkService& svc);
	static bool SshApplies(const NetworkService& svc);
	void OpenRdp(const NetworkService& svc);
	static bool RdpApplies(const NetworkService& svc);
	std::string MacFor(const NetworkService& svc) const;
	void LoadMacs();
	void SaveMacs();
	std::string AutostartLinkPath() const; // ~/config/settings/boot/launch/campiello_won
	std::string SelfAppPath() const;       // this executable's path (symlink target)
	bool IsAutostartEnabled() const;
	void SetAutostart(bool on);

	MdnsRadar                    fRadar;
	SmbHostFinder                fSmbFinder;
	HeaderView*                  fHeader;
	DeviceOutlineView*           fList;
	BTextControl*                fFilter;
	BStringView*                 fStatus;
	DetailTable*                 fDetail;
	BButton*                     fOpenBtn;
	BButton*                     fCopyBtn;
	BButton*                     fWebBtn;
	BButton*                     fInfoBtn;
	BButton*                     fInspectBtn;
	BCheckBox*                   fOnlyOnline;
	BMenuItem*                   fAutostartItem = nullptr;
	bool                         fOnlyOnlineOn = false;
	std::vector<NetworkService>  fManual; // devices added by hand (persisted)
	BMessageRunner*              fRunner = nullptr;
	BMessageRunner*              fStatusRunner = nullptr;
	std::vector<NetworkService>  fServices;
	std::map<std::string, DeviceStatus> fStatusMap;
	std::map<std::string, BBitmap*> fIconCache; // rendered HVIF icons by base name (nullptr = none)
	bool                         fPinging = false;
	std::string                  fSignature;
	std::string                  fShareFolder;

	// LAN-intel enrichment (NetIntel). The maps survive a Refresh and are re-applied each rebuild;
	// fSsdp is folded in like fManual. fMacByIp is persisted so Wake-on-LAN works after a device
	// has gone to sleep (and dropped out of the ARP table).
	campiello::vicinato::OuiDatabase fOui;
	bool                             fOuiTried = false;
	std::map<std::string, std::string> fMacByIp;     // learned IP -> MAC (persisted)
	std::map<std::string, std::string> fNetbiosByIp; // learned IP -> NetBIOS name
	std::vector<NetworkService>      fSsdp;           // UPnP/SSDP devices, merged into the list
	BMessageRunner*                  fIntelRunner = nullptr;
	bool                             fIntelBusy = false;
};

VicinatoWindow::VicinatoWindow()
	:
	BWindow(BRect(100, 100, 780, 640), "Campiello WON",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fHeader = new HeaderView();

	fFilter = new BTextControl("filter", "Cerca:", "", nullptr);
	fFilter->SetModificationMessage(new BMessage(kMsgFilter));

	fStatus = new BStringView("status", "Ricerca in corso...");

	fList = new DeviceOutlineView("list", new BMessage(kMsgInvoke));
	fList->SetSelectionMessage(new BMessage(kMsgSelect));
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	scroll->SetExplicitMinSize(BSize(320, 320));

	fDetail = new DetailTable();
	fDetail->SetEmpty("Seleziona un dispositivo");
	BScrollView* detailScroll = new BScrollView("dscroll", fDetail, 0, false, true);
	detailScroll->SetExplicitMinSize(BSize(250, 240));

	// Toolbar quick actions: they act on the selected device.
	fOpenBtn = new BButton("open", "Apri", new BMessage(kMsgOpen));
	fCopyBtn = new BButton("copy", "Copia IP", new BMessage(kMsgCopy));
	fWebBtn  = new BButton("web", "Web UI", new BMessage(kMsgWeb));
	fInfoBtn = new BButton("info", "Dettagli", new BMessage(kMsgInfo));
	fInspectBtn = new BButton("insp", "Ispeziona", new BMessage(kMsgInspect));
	fOpenBtn->SetEnabled(false);
	fCopyBtn->SetEnabled(false);
	fWebBtn->SetEnabled(false);
	fInfoBtn->SetEnabled(false);
	fInspectBtn->SetEnabled(false);
	BButton* add = new BButton("add", "Aggiungi...", new BMessage(kMsgAddDevice));
	BButton* refresh = new BButton("refresh", "Aggiorna", new BMessage(kMsgRefreshNow));
	fOnlyOnline = new BCheckBox("oo", "Solo online", new BMessage(kMsgOnlyOnline));

	BView* details = new BView("details", 0);
	BLayoutBuilder::Group<>(details, B_VERTICAL, B_USE_SMALL_SPACING)
		.Add(detailScroll);

	BView* body = new BView("body", 0);
	BLayoutBuilder::Group<>(body, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fOpenBtn)
			.Add(fCopyBtn)
			.Add(fWebBtn)
			.Add(fInfoBtn)
			.Add(fInspectBtn)
			.AddGlue()
			.Add(add)
			.Add(fOnlyOnline)
			.Add(refresh)
		.End()
		.Add(fFilter)
		.AddSplit(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(scroll, 2.0f)
			.Add(details, 1.4f)
		.End()
		.Add(fStatus);

	// A small menu bar with the one preference: whether this window opens by itself at login. The
	// Deskbar presence (the daemon + peer replicant) is unaffected; only the window auto-open is.
	BMenuBar* menuBar = new BMenuBar("menubar");
	BMenu* optionsMenu = new BMenu("Opzioni");
	fAutostartItem = new BMenuItem("Apri all'avvio del sistema", new BMessage(kMsgAutostart));
	fAutostartItem->SetMarked(IsAutostartEnabled());
	optionsMenu->AddItem(fAutostartItem);
	menuBar->AddItem(optionsMenu);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(fHeader)
		.Add(body);

	CenterOnScreen();

	if (!fRadar.Start(nullptr)) {
		fStatus->SetText("Impossibile aprire il socket di rete.");
		fHeader->SetSubtitle("Rete non disponibile");
	} else {
		fSmbFinder.Start();
		RegisterShareType();
		fShareFolder = DefaultShareFolder();
		LoadManual();
		LoadMacs();
		BMessage tick(kMsgTick);
		fRunner = new BMessageRunner(BMessenger(this), &tick, 2000000LL);
		BMessage stik(kMsgStatusTick);
		fStatusRunner = new BMessageRunner(BMessenger(this), &stik, 6000000LL);
		BMessage itik(kMsgIntelTick);
		fIntelRunner = new BMessageRunner(BMessenger(this), &itik, 45000000LL);
		Refresh();
		StartIntel(); // one enrichment pass shortly after launch
	}
}

VicinatoWindow::~VicinatoWindow()
{
	delete fRunner;
	delete fStatusRunner;
	delete fIntelRunner;
	for (auto& kv : fIconCache)
		delete kv.second;
}

const NetworkService* VicinatoWindow::Selected() const
{
	int32 sel = fList->CurrentSelection();
	if (sel < 0)
		return nullptr;
	ServiceListItem* item = dynamic_cast<ServiceListItem*>(fList->ItemAt(sel));
	if (item == nullptr || item->Index() < 0 || item->Index() >= (int)fServices.size())
		return nullptr;
	return &fServices[item->Index()];
}

void VicinatoWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTick:
			Refresh();
			return;
		case kMsgStatusTick:
			StartStatusCheck();
			return;
		case kMsgStatusReady:
			ApplyStatus(message);
			return;
		case kMsgIntelTick:
			StartIntel();
			return;
		case kMsgIntelReady:
			ApplyIntel(message);
			return;
		case kMsgWake:
			WakeSelected();
			return;
		case kMsgEasterEgg:
			CreateOldSchoolDemoWindow()->Show();
			return;
		case kMsgAutostart: {
			bool enable = !IsAutostartEnabled();
			SetAutostart(enable);
			if (fAutostartItem != nullptr)
				fAutostartItem->SetMarked(IsAutostartEnabled());
			fStatus->SetText(enable ? "Si aprira' all'avvio del sistema"
				: "Non si aprira' piu' all'avvio (l'icona resta sulla Deskbar)");
			return;
		}
		case kMsgSsh: {
			const NetworkService* svc = Selected();
			if (svc != nullptr) OpenSshTerminal(*svc);
			return;
		}
		case kMsgRdp: {
			const NetworkService* svc = Selected();
			if (svc != nullptr) OpenRdp(*svc);
			return;
		}
		case kMsgFilter:
			RebuildList();
			return;
		case kMsgOnlyOnline:
			fOnlyOnlineOn = (fOnlyOnline->Value() == B_CONTROL_ON);
			RebuildList();
			return;
		case kMsgRefreshNow:
			StartStatusCheck();
			StartIntel();
			RebuildList();
			return;
		case kMsgSelect:
			UpdateDetails();
			return;
		case kMsgInvoke:
		case kMsgOpen: {
			const NetworkService* svc = Selected();
			if (svc != nullptr) OpenService(*svc);
			return;
		}
		case kMsgCopy: {
			const NetworkService* svc = Selected();
			if (svc != nullptr) CopyAddress(*svc);
			return;
		}
		case kMsgWeb: {
			const NetworkService* svc = Selected();
			if (svc != nullptr) OpenWebUi(*svc);
			return;
		}
		case kMsgInfo: {
			const NetworkService* svc = Selected();
			if (svc != nullptr)
				(new BAlert("Dettagli", (svc->label + "\n\n" + DetailsText(*svc)).c_str(), "OK"))->Go();
			return;
		}
		case kMsgInspect:
			InspectSelected();
			return;
		case kMsgAddDevice:
			(new AddDeviceWindow(BMessenger(this)))->Show();
			return;
		case kMsgAddDeviceOk:
			AddManual(message);
			return;
		case kMsgRemoveDevice:
			RemoveSelectedManual();
			return;
		case kMsgContext: {
			BPoint where;
			if (message->FindPoint("screen_where", &where) == B_OK)
				ShowContextMenu(where);
			return;
		}
	}
	BWindow::MessageReceived(message);
}

bool VicinatoWindow::QuitRequested()
{
	delete fRunner;      fRunner = nullptr;
	delete fStatusRunner; fStatusRunner = nullptr;
	delete fIntelRunner; fIntelRunner = nullptr;
	fRadar.Stop();
	fSmbFinder.Stop();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

void VicinatoWindow::Refresh()
{
	RadarSnapshot snap = fRadar.Snapshot();
	std::vector<NetworkService> hood = BuildNeighborhood(snap);
	hood = MergeSmbHosts(std::move(hood), fSmbFinder.Hosts());

	// Fold in devices added by hand, unless mDNS/SMB already surfaced that address.
	for (const NetworkService& m : fManual) {
		bool dup = false;
		for (const NetworkService& s : hood)
			if (s.host == m.host) { dup = true; break; }
		if (!dup)
			hood.push_back(m);
	}

	// Fold in SSDP/UPnP devices (from the intel pass), unless mDNS/SMB already covers that address.
	for (const NetworkService& u : fSsdp) {
		bool dup = false;
		for (const NetworkService& s : hood)
			if (s.host == u.host) { dup = true; break; }
		if (!dup)
			hood.push_back(u);
	}

	// Decorate with learned MAC/vendor/NetBIOS before hashing, so enrichment triggers exactly one
	// rebuild when it arrives and then leaves the signature stable.
	ApplyEnrichment(hood);

	std::string sig;
	for (const NetworkService& s : hood)
		sig += s.id + "|" + s.label + "|" + s.mac + ";";
	bool changed = (sig != fSignature) || fList->FullListCountItems() == 0;

	if (!changed)
		return;
	fSignature = sig;
	fServices = hood;
	SyncShareFolder(fShareFolder, fServices);
	RebuildList();
	StartStatusCheck(); // fresh reachability for the new set
}

void VicinatoWindow::StartStatusCheck()
{
	if (fPinging || fServices.empty())
		return;
	StatusJob* job = new StatusJob();
	job->reply = BMessenger(this);
	for (const NetworkService& s : fServices) {
		int port = s.port;
		if (port == 0 && s.backend == BackendKind::Smb)
			port = 445;
		job->targets.push_back({s.id, s.host, port});
	}
	thread_id t = spawn_thread(StatusThread, "won_ping", B_LOW_PRIORITY, job);
	if (t < 0) { delete job; return; }
	fPinging = true;
	resume_thread(t);
}

void VicinatoWindow::ApplyStatus(BMessage* msg)
{
	fPinging = false;
	const char* id;
	int online = 0, total = 0;
	for (int32 i = 0; msg->FindString("id", i, &id) == B_OK; ++i) {
		int32 latency = -1;
		msg->FindInt32("latency", i, &latency);
		DeviceStatus st;
		if (latency > 0) {
			st.state = kOnline; st.latencyMs = latency; ++online;
		} else {
			// A closed service port does not mean the host is down: if its address is in the ARP
			// table it is reachable on the LAN (e.g. a router whose admin port is filtered but whose
			// wifi we are using). Treat ARP presence as online, with no measured latency (0).
			std::string host;
			for (const NetworkService& s : fServices)
				if (s.id == id) { host = s.host; break; }
			if (!host.empty() && fMacByIp.find(host) != fMacByIp.end()) {
				st.state = kOnline; st.latencyMs = 0; ++online;
			} else {
				st.state = kOffline; st.latencyMs = -1;
			}
		}
		fStatusMap[id] = st;
		++total;
	}
	fList->Invalidate();
	UpdateDetails();
	char sub[96];
	std::snprintf(sub, sizeof(sub), "%d dispositivi - %d online", total, online);
	fHeader->SetSubtitle(sub);
	fHeader->SetOnline(online > 0);
}

void VicinatoWindow::RebuildList()
{
	std::string selId;
	if (const NetworkService* s = Selected())
		selId = s->id;

	fList->MakeEmpty();

	std::string filter = Lower(fFilter->Text());
	const ServiceKind order[] = {ServiceKind::Campiello, ServiceKind::Computer, ServiceKind::Home,
		ServiceKind::Media, ServiceKind::Smb, ServiceKind::Sftp, ServiceKind::Printer,
		ServiceKind::Web, ServiceKind::Other};

	int shown = 0;
	ServiceListItem* toSelect = nullptr;
	ServiceListItem* firstItem = nullptr;
	for (ServiceKind kind : order) {
		std::vector<int> members;
		for (int i = 0; i < (int)fServices.size(); ++i) {
			const NetworkService& s = fServices[i];
			if (s.kind != kind)
				continue;
			if (!filter.empty()) {
				std::string hay = Lower(s.label + " " + s.host + " " + s.serviceType + " " + s.category);
				if (hay.find(filter) == std::string::npos)
					continue;
			}
			if (fOnlyOnlineOn) {
				auto st = fStatusMap.find(s.id);
				if (st == fStatusMap.end() || st->second.state != kOnline)
					continue;
			}
			members.push_back(i);
		}
		if (members.empty())
			continue;

		char header[64];
		std::snprintf(header, sizeof(header), "%s (%zu)", KindLabel(kind), members.size());
		GroupItem* group = new GroupItem(header, kind);
		fList->AddItem(group);
		for (int i : members) {
			const NetworkService& s = fServices[i];
			std::string row = s.label;
			if (!s.host.empty() && s.label.find(s.host) == std::string::npos)
				row += "  -  " + s.host;
			std::string iconName = IconBaseName(s);
			if (fIconCache.find(iconName) == fIconCache.end())
				fIconCache[iconName] = LoadHvifIcon(iconName, 24); // nullptr if no file installed
			ServiceListItem* item = new ServiceListItem(row, s.kind, s.auth != AuthKind::None, i,
				s.id, &fStatusMap, iconName, &fIconCache);
			fList->AddUnder(item, group);
			if (firstItem == nullptr)
				firstItem = item;
			if (!selId.empty() && s.id == selId)
				toSelect = item;
			++shown;
		}
	}

	if (toSelect == nullptr)
		toSelect = firstItem; // select the first device so the details pane is never empty
	if (toSelect != nullptr)
		fList->Select(fList->IndexOf(toSelect));
	UpdateDetails();

	char st[128];
	std::snprintf(st, sizeof(st), "%zu servizi in rete", fServices.size());
	fStatus->SetText(st);
	if (shown == 0)
		fDetail->SetEmpty(filter.empty() ? "Nessun dispositivo trovato"
			: "Nessun risultato per il filtro");
}

std::string VicinatoWindow::DetailsText(const NetworkService& svc) const
{
	std::string info;
	std::string kind = KindLabel(svc.kind);
	info += "Tipo: " + kind;
	if (!svc.category.empty() && svc.category != "Altro" && Lower(svc.category) != Lower(kind))
		info += " - " + svc.category;
	info += "\n";
	if (!svc.host.empty()) {
		info += "Indirizzo: " + svc.host;
		if (svc.port != 0)
			info += ":" + std::to_string(svc.port);
		info += "\n";
	}
	if (!svc.vendor.empty())
		info += "Produttore: " + svc.vendor + "\n";
	if (!svc.mac.empty())
		info += "MAC: " + svc.mac + "\n";
	info += "Servizio: " + svc.serviceType + "\n";
	if (!svc.txt.empty())
		info += "\n";
	int shown = 0;
	for (const auto& kv : svc.txt) {
		if (shown++ >= 12)
			break;
		std::string key = campiello::bricola::mdns::TxtKeyLabel(svc.serviceType, kv.first);
		std::string val = campiello::bricola::mdns::DecodeTxtValue(svc.serviceType, kv.first, kv.second);
		info += "  " + key + (val.empty() ? "" : " = " + val) + "\n";
	}
	return info;
}

// Fill the details table: a wrapping name title, a coloured status line, the major fields, and two
// collapsible groups (network technical details, and the decoded mDNS TXT).
void VicinatoWindow::PopulateDetails(const NetworkService& svc, const std::string& statusText,
	rgb_color statusColor)
{
	fDetail->SetHeader(svc.label, statusText, statusColor);

	std::string typ = KindLabel(svc.kind);
	if (!svc.category.empty() && svc.category != "Altro" && Lower(svc.category) != Lower(typ))
		typ += " - " + svc.category;
	fDetail->AddField("Tipo", typ);

	if (!svc.host.empty()) {
		std::string addr = svc.host;
		if (svc.port != 0)
			addr += ":" + std::to_string(svc.port);
		fDetail->AddField("Indirizzo", addr);
	}
	if (!svc.vendor.empty())
		fDetail->AddField("Produttore", svc.vendor);

	// Minor technical details, collapsible (expanded by default).
	int net = fDetail->AddGroup("Dettagli di rete", false);
	if (!svc.mac.empty())
		fDetail->AddGroupField(net, "MAC", svc.mac);
	fDetail->AddGroupField(net, "Servizio", svc.serviceType);

	// The decoded mDNS TXT, collapsible (collapsed by default when there are many entries).
	if (!svc.txt.empty()) {
		int txtGroup = fDetail->AddGroup("Informazioni sul servizio", svc.txt.size() > 5);
		int shown = 0;
		for (const auto& kv : svc.txt) {
			if (shown++ >= 20)
				break;
			std::string key = campiello::bricola::mdns::TxtKeyLabel(svc.serviceType, kv.first);
			std::string val = campiello::bricola::mdns::DecodeTxtValue(svc.serviceType, kv.first, kv.second);
			fDetail->AddGroupField(txtGroup, key, val);
		}
	}
	fDetail->Commit();
}

void VicinatoWindow::UpdateDetails()
{
	const NetworkService* svc = Selected();
	if (svc == nullptr) {
		fDetail->SetEmpty("Nessun dispositivo selezionato");
		fOpenBtn->SetEnabled(false);
		fCopyBtn->SetEnabled(false);
		fWebBtn->SetEnabled(false);
		fInfoBtn->SetEnabled(false);
		fInspectBtn->SetEnabled(false);
		return;
	}

	std::string statusText;
	rgb_color statusColor;
	auto it = fStatusMap.find(svc->id);
	if (it == fStatusMap.end() || it->second.state == kUnknownState) {
		statusText = "Stato: verifica..."; statusColor = kUnkText;
	} else if (it->second.state == kOnline) {
		char s[64];
		if (it->second.latencyMs > 0)
			std::snprintf(s, sizeof(s), "Stato: online (%d ms)", it->second.latencyMs);
		else
			std::snprintf(s, sizeof(s), "Stato: online (raggiungibile in rete)");
		statusText = s; statusColor = kGoodText;
	} else {
		statusText = "Stato: offline"; statusColor = kBadText;
	}
	PopulateDetails(*svc, statusText, statusColor);

	fOpenBtn->SetEnabled(true);
	fCopyBtn->SetEnabled(!svc->host.empty());
	fInfoBtn->SetEnabled(true);
	fInspectBtn->SetEnabled(true);
	bool web = svc->kind == ServiceKind::Web || svc->kind == ServiceKind::Printer
		|| svc->kind == ServiceKind::Home;
	fWebBtn->SetEnabled(web && !svc->host.empty());
}

void VicinatoWindow::OpenService(const NetworkService& svc)
{
	// A plain computer has no file backend: the obvious action is to open a login shell.
	if (svc.kind == ServiceKind::Computer) {
		OpenSshTerminal(svc);
		return;
	}
	if (!fShareFolder.empty()) {
		std::string path = fShareFolder + "/" + ShortcutName(svc.label);
		entry_ref ref;
		if (get_ref_for_path(path.c_str(), &ref) == B_OK) {
			status_t rc = be_roster->Launch(&ref);
			if (rc == B_OK || rc == B_ALREADY_RUNNING)
				return;
		}
	}
	const char* sig = nullptr;
	std::string arg;
	if (svc.backend == BackendKind::Smb)       { sig = kSmbHelperSig;  arg = "server=" + svc.host; }
	else if (svc.backend == BackendKind::Sftp) { sig = kSftpHelperSig; arg = "host=" + svc.host; }
	if (sig != nullptr) {
		const char* args[] = {arg.c_str()};
		status_t rc = be_roster->Launch(sig, 1, args);
		if (rc == B_OK || rc == B_ALREADY_RUNNING)
			return;
	}
	(new BAlert("WON", (svc.label + "\n\n" + DetailsText(svc)).c_str(), "OK"))->Go();
}

void VicinatoWindow::OpenWebUi(const NetworkService& svc)
{
	if (svc.host.empty())
		return;
	std::string scheme = (svc.port == 443) ? "https" : "http";
	std::string url = scheme + "://" + svc.host;
	if (svc.port != 0 && svc.port != 80 && svc.port != 443)
		url += ":" + std::to_string(svc.port);
	const char* args[] = {url.c_str()};
	if (be_roster->Launch("application/x-vnd.Be-URL.http", 1, args) != B_OK
		&& be_roster->Launch("application/x-vnd.Be-URL.https", 1, args) != B_OK)
		(new BAlert("WON", ("Nessun browser per aprire:\n" + url).c_str(), "OK"))->Go();
}

void VicinatoWindow::CopyAddress(const NetworkService& svc)
{
	std::string addr = svc.host;
	if (svc.port != 0)
		addr += ":" + std::to_string(svc.port);
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();
		BMessage* data = be_clipboard->Data();
		if (data != nullptr) {
			data->AddData("text/plain", B_MIME_TYPE, addr.data(), addr.size());
			be_clipboard->Commit();
		}
		be_clipboard->Unlock();
		fStatus->SetText(("Copiato: " + addr).c_str());
	}
}

void VicinatoWindow::ShowContextMenu(BPoint screenWhere)
{
	const NetworkService* svc = Selected();
	if (svc == nullptr)
		return;
	BPopUpMenu* menu = new BPopUpMenu("ctx", false, false);
	menu->AddItem(new BMenuItem("Apri", new BMessage(kMsgOpen)));
	menu->AddItem(new BMenuItem("Copia indirizzo", new BMessage(kMsgCopy)));
	if (svc->kind == ServiceKind::Web || svc->kind == ServiceKind::Printer
		|| svc->kind == ServiceKind::Home)
		menu->AddItem(new BMenuItem("Apri interfaccia web", new BMessage(kMsgWeb)));
	if (SshApplies(*svc) && !svc->host.empty())
		menu->AddItem(new BMenuItem("Apri terminale SSH", new BMessage(kMsgSsh)));
	if (RdpApplies(*svc) && !svc->host.empty())
		menu->AddItem(new BMenuItem("Apri desktop remoto (RDP)", new BMessage(kMsgRdp)));
	if (!MacFor(*svc).empty()) {
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Accendi (Wake-on-LAN)", new BMessage(kMsgWake)));
	}
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Dettagli...", new BMessage(kMsgInfo)));
	menu->AddItem(new BMenuItem("Ispeziona record mDNS...", new BMessage(kMsgInspect)));
	if (svc->id.compare(0, 7, "manual:") == 0) {
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Rimuovi dispositivo", new BMessage(kMsgRemoveDevice)));
	}
	menu->SetTargetForItems(this);
	menu->Go(screenWhere, true, true, true);
}

// ------------------------------------------------------------------------------ LAN intel wiring

// The best-known MAC for a service: the one already on the service, else a learned/persisted one.
std::string VicinatoWindow::MacFor(const NetworkService& svc) const
{
	if (!svc.mac.empty())
		return svc.mac;
	auto it = fMacByIp.find(svc.host);
	return it == fMacByIp.end() ? std::string() : it->second;
}

// Decorate services with learned MAC, vendor (from the OUI database) and NetBIOS name. Never
// invents data: only touches services whose host is a dotted IP we have a record for.
void VicinatoWindow::ApplyEnrichment(std::vector<NetworkService>& hood) const
{
	for (NetworkService& s : hood) {
		auto mit = fMacByIp.find(s.host);
		if (mit != fMacByIp.end()) {
			s.mac = mit->second;
			if (!fOui.Empty()) {
				std::string v = fOui.Lookup(s.mac);
				if (!v.empty())
					s.vendor = v;
			}
		}
		// Give SMB hosts a real computer name when NetBIOS answered and we only had the IP.
		auto nit = fNetbiosByIp.find(s.host);
		if (nit != fNetbiosByIp.end() && !nit->second.empty()
			&& (s.label == s.host || s.label.empty()))
			s.label = nit->second;
	}
}

void VicinatoWindow::StartIntel()
{
	if (fIntelBusy || fServices.empty())
		return;
	IntelJob* job = new IntelJob();
	job->reply = BMessenger(this);
	for (const NetworkService& s : fServices) {
		if (!s.host.empty())
			job->targets.push_back({s.id, s.host});
		if (s.backend == BackendKind::Smb && IsDottedIp(s.host))
			job->smbIps.push_back(s.host);
	}
	thread_id t = spawn_thread(IntelThread, "won_intel", B_LOW_PRIORITY, job);
	if (t < 0) { delete job; return; }
	fIntelBusy = true;
	resume_thread(t);
}

void VicinatoWindow::ApplyIntel(BMessage* msg)
{
	fIntelBusy = false;

	// Load the OUI database once, lazily, if the user has dropped an oui.txt into a data dir.
	if (!fOuiTried) {
		fOuiTried = true;
		std::string ouiPath = campiello::vicinato::FindOuiFile();
		if (!ouiPath.empty())
			fOui.LoadFromFile(ouiPath);
	}

	bool learnedMac = false;
	const char* ip;
	const char* val;
	for (int32 i = 0; msg->FindString("mac_ip", i, &ip) == B_OK
			&& msg->FindString("mac", i, &val) == B_OK; ++i) {
		if (fMacByIp[ip] != val) { fMacByIp[ip] = val; learnedMac = true; }
	}
	for (int32 i = 0; msg->FindString("nb_ip", i, &ip) == B_OK
			&& msg->FindString("nb_name", i, &val) == B_OK; ++i) {
		fNetbiosByIp[ip] = val;
	}

	// Rebuild the SSDP/UPnP set from this pass.
	fSsdp.clear();
	const char* stype;
	const char* server;
	for (int32 i = 0; msg->FindString("ssdp_ip", i, &ip) == B_OK; ++i) {
		if (msg->FindString("ssdp_type", i, &stype) != B_OK)
			continue;
		NetworkService s;
		s.id = std::string("ssdp:") + ip;
		s.host = ip;
		s.label = stype;
		s.serviceType = "ssdp/upnp";
		s.category = "UPnP";
		std::string lt = Lower(stype);
		if (lt.find("tv") != std::string::npos || lt.find("sonos") != std::string::npos
			|| lt.find("dlna") != std::string::npos || lt.find("multimediale") != std::string::npos
			|| lt.find("lettore") != std::string::npos)
			s.kind = ServiceKind::Media;
		else if (lt.find("stampante") != std::string::npos)
			s.kind = ServiceKind::Printer;
		else
			s.kind = ServiceKind::Other;
		if (msg->FindString("ssdp_server", i, &server) == B_OK && server[0] != '\0')
			s.txt.push_back({"server", server});
		fSsdp.push_back(s);
	}

	if (learnedMac)
		SaveMacs();
	Refresh(); // folds the new SSDP set and re-applies enrichment (rebuilds only if it changed)
}

void VicinatoWindow::WakeSelected()
{
	const NetworkService* svc = Selected();
	if (svc == nullptr)
		return;
	std::string mac = MacFor(*svc);
	if (mac.empty()) {
		(new BAlert("WON", "Indirizzo MAC non disponibile per questo dispositivo.\n\n"
			"Il MAC si apprende quando il dispositivo e' online e nella tabella ARP.",
			"OK"))->Go();
		return;
	}
	bool ok = campiello::vicinato::SendWakeOnLan(mac); // limited broadcast
	std::string bcast = campiello::vicinato::DirectedBroadcast(svc->host);
	if (!bcast.empty())
		ok = campiello::vicinato::SendWakeOnLan(mac, bcast) || ok;
	if (ok)
		fStatus->SetText(("Magic packet inviato a " + mac).c_str());
	else
		fStatus->SetText("Invio del magic packet non riuscito.");
}

// SSH makes sense for computers and login hosts, not for lights, printers or media receivers.
bool VicinatoWindow::SshApplies(const NetworkService& svc)
{
	if (svc.serviceType.find("_ssh") != std::string::npos
		|| svc.serviceType.find("_sftp-ssh") != std::string::npos)
		return true;
	switch (svc.kind) {
		case ServiceKind::Sftp:
		case ServiceKind::Campiello:
		case ServiceKind::Computer:
		case ServiceKind::Smb:
			return true;
		default:
			return false;
	}
}

// Open a Haiku Terminal running `ssh <host>`. Terminal is B_MULTIPLE_LAUNCH, so each call opens a
// fresh window; the trailing arguments are the command it runs (Terminal usage: [OPTION] [SHELL]).
void VicinatoWindow::OpenSshTerminal(const NetworkService& svc)
{
	if (svc.host.empty())
		return;
	// If the service carries a username hint (some SFTP TXT records do), honour it as user@host.
	std::string target = svc.host;
	for (const auto& kv : svc.txt) {
		if (kv.first == "u" || kv.first == "user" || kv.first == "username") {
			if (!kv.second.empty())
				target = kv.second + "@" + svc.host;
			break;
		}
	}
	std::string title = "SSH " + svc.host;
	const char* argv[] = {"-t", title.c_str(), "/bin/ssh", target.c_str()};
	status_t rc = be_roster->Launch("application/x-vnd.Haiku-Terminal", 4,
		const_cast<char**>(argv));
	if (rc != B_OK && rc != B_ALREADY_RUNNING)
		(new BAlert("WON", "Impossibile aprire il Terminal per la sessione SSH.", "OK"))->Go();
	else
		fStatus->SetText(("Terminale SSH verso " + svc.host).c_str());
}

// RDP is a Windows thing: offer it for Windows shares, plain computers, and anything advertising an
// _rdp service.
bool VicinatoWindow::RdpApplies(const NetworkService& svc)
{
	if (svc.serviceType.find("_rdp") != std::string::npos)
		return true;
	return svc.kind == ServiceKind::Smb || svc.kind == ServiceKind::Computer;
}

namespace {
// Locate an installed RDP client. `remmina` takes a URI (-c rdp://host); the FreeRDP family takes
// /v:host. Returns false when nothing is installed.
bool FindRdpClient(std::string& path, bool& remminaStyle)
{
	const char* dirs[] = {"/boot/system/bin", "/bin", "/boot/system/non-packaged/bin"};
	const struct { const char* name; bool remmina; } cands[] = {
		{"xfreerdp", false}, {"xfreerdp3", false}, {"sdl-freerdp", false},
		{"sdl-freerdp3", false}, {"wlfreerdp", false}, {"remmina", true}};
	for (const auto& c : cands)
		for (const char* d : dirs) {
			std::string p = std::string(d) + "/" + c.name;
			if (access(p.c_str(), X_OK) == 0) { path = p; remminaStyle = c.remmina; return true; }
		}
	return false;
}
} // namespace

// Open a Windows desktop with the installed RDP client, wrapped in a Terminal so its logs are
// visible (and errors are not swallowed). Gentle message when no client is installed.
void VicinatoWindow::OpenRdp(const NetworkService& svc)
{
	if (svc.host.empty())
		return;
	std::string clientPath;
	bool remminaStyle = false;
	if (!FindRdpClient(clientPath, remminaStyle)) {
		(new BAlert("WON", "Nessun client RDP installato.\n\nInstalla \"freerdp\" o \"remmina\" da "
			"HaikuDepot per aprire i desktop remoti Windows.", "OK"))->Go();
		return;
	}
	std::string title = "RDP " + svc.host;
	std::string uri = "rdp://" + svc.host;
	std::string vflag = "/v:" + svc.host;
	std::vector<const char*> argv;
	argv.push_back("-t");
	argv.push_back(title.c_str());
	argv.push_back(clientPath.c_str());
	if (remminaStyle) {
		argv.push_back("-c");
		argv.push_back(uri.c_str());
	} else {
		argv.push_back(vflag.c_str());
	}
	status_t rc = be_roster->Launch("application/x-vnd.Haiku-Terminal", (int)argv.size(),
		const_cast<char**>(argv.data()));
	if (rc != B_OK && rc != B_ALREADY_RUNNING)
		(new BAlert("WON", "Impossibile avviare il client RDP.", "OK"))->Go();
	else
		fStatus->SetText(("Desktop remoto verso " + svc.host).c_str());
}

static std::string MacsPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) != B_OK)
		return "";
	std::string dir = std::string(settings) + "/Campiello";
	::mkdir(dir.c_str(), 0755);
	return dir + "/won_macs";
}

void VicinatoWindow::LoadMacs()
{
	fMacByIp.clear();
	std::string path = MacsPath();
	if (path.empty())
		return;
	FILE* f = fopen(path.c_str(), "r");
	if (f == nullptr)
		return;
	char line[256];
	while (fgets(line, sizeof(line), f) != nullptr) {
		char ip[64], mac[64];
		if (std::sscanf(line, "%63s %63s", ip, mac) == 2)
			fMacByIp[ip] = mac;
	}
	fclose(f);
}

void VicinatoWindow::SaveMacs()
{
	std::string path = MacsPath();
	if (path.empty())
		return;
	FILE* f = fopen(path.c_str(), "w");
	if (f == nullptr)
		return;
	for (const auto& kv : fMacByIp)
		fprintf(f, "%s\t%s\n", kv.first.c_str(), kv.second.c_str());
	fclose(f);
}

// The user autostart entry: a symlink in ~/config/settings/boot/launch/ that Haiku launches at
// login. Its presence is the whole toggle; the daemon (Deskbar icon) starts independently.
std::string VicinatoWindow::AutostartLinkPath() const
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) != B_OK)
		return "";
	return std::string(settings) + "/boot/launch/campiello_won";
}

std::string VicinatoWindow::SelfAppPath() const
{
	app_info info;
	if (be_app->GetAppInfo(&info) != B_OK)
		return "";
	BEntry entry(&info.ref);
	BPath path;
	if (entry.GetPath(&path) != B_OK)
		return "";
	return path.Path();
}

bool VicinatoWindow::IsAutostartEnabled() const
{
	std::string link = AutostartLinkPath();
	if (link.empty())
		return false;
	struct stat st;
	return lstat(link.c_str(), &st) == 0; // the symlink exists (even if it dangles)
}

void VicinatoWindow::SetAutostart(bool on)
{
	std::string link = AutostartLinkPath();
	if (link.empty())
		return;
	if (on) {
		// Make sure ~/config/settings/boot/launch exists, then link this app there.
		std::string launchDir = link.substr(0, link.rfind('/'));
		std::string bootDir = launchDir.substr(0, launchDir.rfind('/'));
		::mkdir(bootDir.c_str(), 0755);
		::mkdir(launchDir.c_str(), 0755);
		std::string self = SelfAppPath();
		if (self.empty())
			return;
		::unlink(link.c_str()); // replace any stale link
		::symlink(self.c_str(), link.c_str());
	} else {
		::unlink(link.c_str());
	}
}

// The raw mDNS records for a device (SRV/TXT/A), undecoded, for the inspector window.
std::string VicinatoWindow::InspectorText(const NetworkService& svc) const
{
	std::string t;
	t += "Istanza: " + svc.label + "\n";
	t += "Tipo servizio: " + (svc.serviceType.empty() ? std::string("(nessuno)") : svc.serviceType)
		+ "\n";
	t += "Indirizzo (A): " + (svc.host.empty() ? std::string("(non risolto)") : svc.host) + "\n";
	t += "Porta (SRV): " + (svc.port ? std::to_string(svc.port) : std::string("(nessuna)")) + "\n";
	t += "Chiave interna: " + svc.id + "\n";
	t += "\nTXT grezzi (" + std::to_string(svc.txt.size()) + "):\n";
	if (svc.txt.empty())
		t += "  (nessun record TXT)\n";
	for (const auto& kv : svc.txt)
		t += "  " + kv.first + (kv.second.empty() ? "" : "=" + kv.second) + "\n";
	return t;
}

void VicinatoWindow::InspectSelected()
{
	const NetworkService* svc = Selected();
	if (svc == nullptr)
		return;
	std::string title = "Record mDNS - " + svc->label;
	(new InspectorWindow(title.c_str(), InspectorText(*svc)))->Show();
}

static std::string ManualPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) != B_OK)
		return "";
	std::string dir = std::string(settings) + "/Campiello";
	::mkdir(dir.c_str(), 0755);
	return dir + "/won_manual";
}

static NetworkService MakeManual(const std::string& host, int port, const std::string& name)
{
	NetworkService s;
	s.id = "manual:" + host + ":" + std::to_string(port);
	s.label = name.empty() ? host : name;
	s.host = host;
	s.port = (uint16_t)port;
	s.serviceType = "manuale";
	s.category = "Manuale";
	s.kind = (port == 80 || port == 443 || port == 8080 || port == 8443)
		? ServiceKind::Web : ServiceKind::Other;
	s.auth = AuthKind::None;
	s.backend = BackendKind::None;
	s.browsable = false;
	return s;
}

void VicinatoWindow::LoadManual()
{
	fManual.clear();
	std::string path = ManualPath();
	if (path.empty())
		return;
	FILE* f = std::fopen(path.c_str(), "r");
	if (f == nullptr)
		return;
	char line[512];
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		size_t t1 = s.find('\t');
		if (t1 == std::string::npos)
			continue;
		size_t t2 = s.find('\t', t1 + 1);
		std::string host = s.substr(0, t1);
		std::string portStr = s.substr(t1 + 1, (t2 == std::string::npos) ? std::string::npos : t2 - t1 - 1);
		std::string name = (t2 == std::string::npos) ? "" : s.substr(t2 + 1);
		if (!host.empty())
			fManual.push_back(MakeManual(host, std::atoi(portStr.c_str()), name));
	}
	std::fclose(f);
}

void VicinatoWindow::SaveManual()
{
	std::string path = ManualPath();
	if (path.empty())
		return;
	FILE* f = std::fopen(path.c_str(), "w");
	if (f == nullptr)
		return;
	for (const NetworkService& s : fManual)
		std::fprintf(f, "%s\t%d\t%s\n", s.host.c_str(), (int)s.port, s.label.c_str());
	std::fclose(f);
}

void VicinatoWindow::AddManual(BMessage* msg)
{
	const char* name = ""; msg->FindString("name", &name);
	const char* host = ""; msg->FindString("host", &host);
	int32 port = 0; msg->FindInt32("port", &port);
	if (host[0] == '\0')
		return;
	NetworkService s = MakeManual(host, port, name);
	// replace an existing manual entry at the same id, else append
	for (NetworkService& m : fManual)
		if (m.id == s.id) { m = s; SaveManual(); fSignature.clear(); Refresh(); return; }
	fManual.push_back(s);
	SaveManual();
	fSignature.clear(); // force a rebuild
	Refresh();
	StartStatusCheck();
}

void VicinatoWindow::RemoveSelectedManual()
{
	const NetworkService* svc = Selected();
	if (svc == nullptr || svc->id.compare(0, 7, "manual:") != 0)
		return;
	std::string id = svc->id;
	for (size_t i = 0; i < fManual.size(); ++i)
		if (fManual[i].id == id) { fManual.erase(fManual.begin() + i); break; }
	SaveManual();
	fSignature.clear();
	Refresh();
}

int main()
{
	BApplication app(kSignature);
	VicinatoWindow* window = new VicinatoWindow();
	window->Show();
	app.Run();
	return 0;
}
