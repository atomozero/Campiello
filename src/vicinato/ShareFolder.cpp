// ShareFolder.cpp
//
// See ShareFolder.h.

#include "ShareFolder.h"

#include "HandlerRegistry.h"

#include <cstring>
#include <set>
#include <vector>

#include <sys/stat.h>

#include <fs_attr.h>

#include <Bitmap.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <MimeType.h>
#include <Node.h>
#include <NodeInfo.h>
#include <String.h>
#include <View.h>

namespace campiello {
namespace vicinato {

namespace {

const char* const kShareType    = "application/x-vnd.Campiello-smb-share";
const char* const kBookmarkType = "application/x-vnd.Be-bookmark"; // opens in the browser
const char* const kHelperSig    = "application/x-vnd.Campiello-smb-mount";
const char* const kAttrServer   = "CAMPIELLO:server";
const char* const kAttrShare    = "CAMPIELLO:share";
const char* const kAttrName     = "CAMPIELLO:name";
const char* const kAttrHost     = "CAMPIELLO:host"; // device shortcut: address a handler connects to
const char* const kAttrType     = "CAMPIELLO:type"; // device shortcut: raw DNS-SD service type
const char* const kAttrPort     = "CAMPIELLO:port"; // device shortcut: SRV port (int32)
const char* const kAttrWon      = "CAMPIELLO:won"; // marks a shortcut we created, safe to prune
const char* const kAttrUrl      = "META:url";      // Haiku bookmark URL attribute

// Draw a Windows-share icon: a blue folder with a small white network glyph.
void DrawShareIcon(BView* v, float s)
{
	v->SetHighColor(B_TRANSPARENT_COLOR);
	v->FillRect(v->Bounds());
	v->SetDrawingMode(B_OP_ALPHA);

	rgb_color blue = {0x00, 0x78, 0xd7, 0xff};
	rgb_color dark = {0x00, 0x5a, 0xa6, 0xff};
	rgb_color white = {0xff, 0xff, 0xff, 0xff};

	v->SetHighColor(dark);
	v->FillRoundRect(BRect(s * 0.10f, s * 0.16f, s * 0.48f, s * 0.34f), s * 0.06f, s * 0.06f);
	v->SetHighColor(blue);
	v->FillRoundRect(BRect(s * 0.08f, s * 0.26f, s * 0.92f, s * 0.86f), s * 0.08f, s * 0.08f);

	v->SetHighColor(white);
	float r = s * 0.05f;
	BPoint top(s * 0.50f, s * 0.42f);
	BPoint left(s * 0.34f, s * 0.68f);
	BPoint right(s * 0.66f, s * 0.68f);
	v->SetPenSize(s * 0.03f);
	v->StrokeLine(top, left);
	v->StrokeLine(top, right);
	v->StrokeLine(left, right);
	v->FillEllipse(top, r, r);
	v->FillEllipse(left, r, r);
	v->FillEllipse(right, r, r);
	v->Sync();
}

BBitmap* MakeIcon(float size, color_space cs)
{
	BRect r(0, 0, size - 1, size - 1);
	BBitmap* bmp = new BBitmap(r, cs, true);
	BView* view = new BView(r, "icon", 0, 0);
	bmp->AddChild(view);
	bmp->Lock();
	DrawShareIcon(view, size);
	bmp->Unlock();
	bmp->RemoveChild(view);
	delete view;
	return bmp;
}

// Set the type's icon at both sizes, trying the colour spaces SetIcon accepts. Best effort.
void SetTypeIcon(BMimeType& mime)
{
	for (color_space cs : {B_RGBA32, B_CMAP8}) {
		BBitmap* large = MakeIcon(32, cs);
		BBitmap* mini = MakeIcon(16, cs);
		status_t l = mime.SetIcon(large, B_LARGE_ICON);
		status_t m = mime.SetIcon(mini, B_MINI_ICON);
		delete large;
		delete mini;
		if (l == B_OK && m == B_OK)
			return;
	}
}

// A distinct colour and one-letter glyph per kind, matching the WON list badges (kept in step with
// campiello_vicinato.cpp's KindColor/KindGlyph). Home draws a little house instead of a letter.
rgb_color KindColor(ServiceKind k)
{
	switch (k) {
		case ServiceKind::Campiello: return (rgb_color){0x2a, 0x6f, 0xdb, 0xff};
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
		case ServiceKind::Smb:       return "W";
		case ServiceKind::Sftp:      return "S";
		case ServiceKind::Web:       return "@";
		case ServiceKind::Printer:   return "P";
		case ServiceKind::Media:     return "M";
		default:                     return "?";
	}
}

// Draw a coloured rounded tile with a distinctive white pictogram per kind (a window for Windows
// shares, a play triangle for media, a house for home devices, a printer, a globe for web, a key
// for SSH), so a WON icon reads at a glance as "this kind of device". These are our own MIT vector
// drawings (no third-party icon assets). Kinds without a pictogram fall back to the letter glyph.
void DrawServiceIcon(BView* v, float s, ServiceKind kind)
{
	v->SetHighColor(B_TRANSPARENT_COLOR);
	v->FillRect(v->Bounds());
	v->SetDrawingMode(B_OP_ALPHA);

	rgb_color tile = KindColor(kind);
	v->SetHighColor(tile);
	v->FillRoundRect(BRect(s * 0.06f, s * 0.06f, s * 0.94f, s * 0.94f), s * 0.18f, s * 0.18f);

	const rgb_color white = {0xff, 0xff, 0xff, 0xff};
	v->SetHighColor(white);
	v->SetPenSize(s * 0.05f);

	switch (kind) {
		case ServiceKind::Smb: {
			// A four-pane window (evokes Windows).
			float x0 = s * 0.30f, y0 = s * 0.30f, x1 = s * 0.70f, y1 = s * 0.70f;
			float mx = (x0 + x1) / 2, my = (y0 + y1) / 2, g = s * 0.04f;
			v->FillRect(BRect(x0, y0, mx - g, my - g));
			v->FillRect(BRect(mx + g, y0, x1, my - g));
			v->FillRect(BRect(x0, my + g, mx - g, y1));
			v->FillRect(BRect(mx + g, my + g, x1, y1));
			break;
		}
		case ServiceKind::Media: {
			BPoint tri[3] = {BPoint(s * 0.41f, s * 0.32f), BPoint(s * 0.41f, s * 0.68f),
				BPoint(s * 0.71f, s * 0.50f)};
			v->FillPolygon(tri, 3);
			break;
		}
		case ServiceKind::Home: {
			float bodyTop = s * 0.50f, bodyBottom = s * 0.72f, bodyL = s * 0.38f, bodyR = s * 0.62f;
			v->FillRect(BRect(bodyL, bodyTop, bodyR, bodyBottom));
			BPoint roof[3] = {BPoint(s * 0.32f, bodyTop), BPoint(s * 0.50f, s * 0.30f),
				BPoint(s * 0.68f, bodyTop)};
			v->FillPolygon(roof, 3);
			break;
		}
		case ServiceKind::Printer: {
			v->FillRoundRect(BRect(s * 0.24f, s * 0.40f, s * 0.76f, s * 0.62f),
				s * 0.05f, s * 0.05f);                                                  // body
			v->SetHighColor(tile);
			v->FillRect(BRect(s * 0.33f, s * 0.44f, s * 0.67f, s * 0.49f));             // output slot
			v->SetHighColor(white);
			v->FillRect(BRect(s * 0.36f, s * 0.63f, s * 0.64f, s * 0.77f));             // printout below
			break;
		}
		case ServiceKind::Web: {
			BRect c(s * 0.30f, s * 0.30f, s * 0.70f, s * 0.70f);
			v->StrokeEllipse(c);
			v->StrokeEllipse(BRect(s * 0.43f, s * 0.30f, s * 0.57f, s * 0.70f));        // meridian
			v->StrokeLine(BPoint(s * 0.30f, s * 0.50f), BPoint(s * 0.70f, s * 0.50f));  // equator
			break;
		}
		case ServiceKind::Sftp: {
			v->StrokeEllipse(BRect(s * 0.30f, s * 0.39f, s * 0.46f, s * 0.55f));        // key bow
			v->StrokeLine(BPoint(s * 0.45f, s * 0.47f), BPoint(s * 0.70f, s * 0.47f));  // shaft
			v->StrokeLine(BPoint(s * 0.63f, s * 0.47f), BPoint(s * 0.63f, s * 0.56f));  // tooth
			v->StrokeLine(BPoint(s * 0.70f, s * 0.47f), BPoint(s * 0.70f, s * 0.58f));  // tip tooth
			break;
		}
		default: {
			BFont bold(be_bold_font);
			bold.SetSize(s * 0.5f);
			v->SetFont(&bold);
			const char* g = KindGlyph(kind);
			font_height fh;
			v->GetFontHeight(&fh);
			float gw = v->StringWidth(g);
			v->DrawString(g, BPoint(s * 0.5f - gw / 2, s * 0.5f + (fh.ascent - fh.descent) / 2));
			break;
		}
	}
	v->Sync();
}

BBitmap* MakeServiceIcon(float size, color_space cs, ServiceKind kind)
{
	BRect r(0, 0, size - 1, size - 1);
	BBitmap* bmp = new BBitmap(r, cs, true);
	BView* view = new BView(r, "icon", 0, 0);
	bmp->AddChild(view);
	bmp->Lock();
	DrawServiceIcon(view, size, kind);
	bmp->Unlock();
	bmp->RemoveChild(view);
	delete view;
	return bmp;
}

// The settings file name for a kind's user-supplied icon.
const char* KindIconName(ServiceKind k)
{
	switch (k) {
		case ServiceKind::Campiello: return "campiello";
		case ServiceKind::Smb:       return "smb";
		case ServiceKind::Sftp:      return "sftp";
		case ServiceKind::Home:      return "home";
		case ServiceKind::Web:       return "web";
		case ServiceKind::Printer:   return "printer";
		case ServiceKind::Media:     return "media";
		default:                     return "other";
	}
}

// If the user dropped their own HVIF icon at <settings>/Campiello/icons/<kind>.hvif, set it as the
// node's vector icon and return true. This lets a user assign any icon (e.g. one they downloaded)
// per kind ON THEIR machine, without Campiello shipping third-party icon assets: the MIT package
// carries only our own drawn pictograms as defaults. (docs/NEIGHBORHOOD.md)
bool SetUserIcon(BNode& node, ServiceKind kind)
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, settings, sizeof(settings)) != B_OK)
		return false;
	std::string path = std::string(settings) + "/Campiello/icons/" + KindIconName(kind) + ".hvif";
	BFile file(path.c_str(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;
	off_t size = 0;
	if (file.GetSize(&size) != B_OK || size <= 0 || size > 256 * 1024)
		return false;
	std::vector<uint8> data(static_cast<size_t>(size));
	if (file.Read(data.data(), data.size()) != static_cast<ssize_t>(data.size()))
		return false;
	BNodeInfo info(&node);
	return info.SetIcon(data.data(), data.size()) == B_OK; // vector-icon overload (BEOS:ICON)
}

// Set a per-file icon so each shortcut shows its kind's mark regardless of its MIME type: the
// user's own HVIF if present, else our drawn pictogram.
void SetFileIcon(BNode& node, ServiceKind kind)
{
	if (SetUserIcon(node, kind))
		return;
	BNodeInfo info(&node);
	for (color_space cs : {B_RGBA32, B_CMAP8}) {
		BBitmap* large = MakeServiceIcon(32, cs, kind);
		BBitmap* mini = MakeServiceIcon(16, cs, kind);
		status_t l = info.SetIcon(large, B_LARGE_ICON);
		status_t m = info.SetIcon(mini, B_MINI_ICON);
		delete large;
		delete mini;
		if (l == B_OK && m == B_OK)
			return;
	}
}

std::string SanitizeFileName(const std::string& in)
{
	std::string out = in;
	for (char& c : out)
		if (c == '/' || c == '\0')
			c = '-';
	if (out.empty())
		out = "condivisione";
	return out;
}

// Mark a file as a Campiello-created WON shortcut, so pruning can tell ours from the user's files.
void MarkOurs(BNode& node)
{
	int32 one = 1;
	node.WriteAttr(kAttrWon, B_INT32_TYPE, 0, &one, sizeof(one));
}

// A Windows share: double-click opens the SMB connect helper (which mounts it).
void WriteSmbShare(const std::string& path, const NetworkService& svc)
{
	BFile file(path.c_str(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;
	BString note("Condivisione Windows.\nDoppio clic per connetterti.\nServer: ");
	note << svc.host.c_str() << "\n";
	file.Write(note.String(), note.Length());
	BString server(svc.host.c_str());
	BString name(svc.label.c_str());
	file.WriteAttrString(kAttrServer, &server);
	file.WriteAttrString(kAttrName, &name);
	BNodeInfo info(&file);
	info.SetType(kShareType);
	MarkOurs(file);
	SetFileIcon(file, ServiceKind::Smb);
}

// A web service: a Haiku bookmark that opens the device's page in the default browser.
void WriteBookmark(const std::string& path, const NetworkService& svc)
{
	BFile file(path.c_str(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;
	std::string scheme = (svc.port == 443) ? "https" : "http";
	BString url((scheme + "://" + svc.host).c_str());
	if (svc.port != 0 && svc.port != 80 && svc.port != 443)
		url << ":" << (int32)svc.port;
	url << "/";
	file.WriteAttrString(kAttrUrl, &url);
	BNodeInfo info(&file);
	info.SetType(kBookmarkType);
	MarkOurs(file);
	SetFileIcon(file, svc.kind);
}

// Any other device (printer, media, home, peer...): an info card. Double-click shows its details.
void WriteInfo(const std::string& path, const NetworkService& svc)
{
	BFile file(path.c_str(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;
	BString note("Dispositivo di rete rilevato da Campiello.\n\n");
	note << "Nome: " << svc.label.c_str() << "\n";
	if (!svc.category.empty())
		note << "Tipo: " << svc.category.c_str() << "\n";
	note << "Indirizzo: " << svc.host.c_str();
	if (svc.port != 0)
		note << ":" << (int32)svc.port;
	note << "\n";
	file.Write(note.String(), note.Length());
	BNodeInfo info(&file);
	info.SetType("text/plain");
	MarkOurs(file);
	SetFileIcon(file, svc.kind);
}

// A device with an installed handler add-on (a Fire TV remote, Hue bridge...): a shortcut whose
// per-file preferred app is the handler's signature, so a double-click launches that add-on with
// the device. The add-on reads CAMPIELLO:host/name/type from the node (campiello_firetv does).
void WriteDeviceShortcut(const std::string& path, const NetworkService& svc,
	const DeviceHandler& handler)
{
	BFile file(path.c_str(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;
	BString note(handler.name.c_str());
	note << ".\nDoppio clic per aprire.\nDispositivo: " << svc.host.c_str() << "\n";
	file.Write(note.String(), note.Length());
	BString host(svc.host.c_str());
	BString name(svc.label.c_str());
	BString type(svc.serviceType.c_str());
	file.WriteAttrString(kAttrHost, &host);
	file.WriteAttrString(kAttrName, &name);
	file.WriteAttrString(kAttrType, &type);
	if (svc.port != 0) {
		int32 port = svc.port; // the SRV port, so an add-on connects without re-querying mDNS
		file.WriteAttr(kAttrPort, B_INT32_TYPE, 0, &port, sizeof(port));
	}
	// The discovered mDNS TXT pairs, as CAMPIELLO:txt.<key> attributes, so an add-on can read the
	// device's advertised metadata without re-querying mDNS (e.g. HomeKit accessory category/state).
	for (const std::pair<std::string, std::string>& kv : svc.txt) {
		if (kv.first.empty() || kv.first.size() > 64)
			continue;
		BString attr("CAMPIELLO:txt.");
		attr << kv.first.c_str();
		BString value(kv.second.c_str());
		file.WriteAttrString(attr.String(), &value);
	}
	BNodeInfo info(&file);
	info.SetPreferredApp(handler.signature.c_str()); // per-file: Tracker launches this on open
	MarkOurs(file);
	SetFileIcon(file, svc.kind);
}

// Write the right kind of shortcut for a service. A matching device handler wins over the generic
// info card, but SMB shares keep their mount helper (the SMB backend is not a device add-on).
void WriteServiceFile(const std::string& path, const NetworkService& svc, const HandlerRegistry& reg)
{
	const DeviceHandler* handler = reg.DefaultHandler(svc);
	if (svc.backend == BackendKind::Smb)
		WriteSmbShare(path, svc);
	else if (handler != nullptr)
		WriteDeviceShortcut(path, svc, *handler);
	else if (svc.kind == ServiceKind::Web)
		WriteBookmark(path, svc);
	else
		WriteInfo(path, svc);
}

// Load every installed device-handler manifest: system add-ons plus the user's own. Missing dirs
// are fine (best-effort), so this works whether or not any add-on package is installed.
HandlerRegistry LoadHandlers()
{
	HandlerRegistry reg;
	char buf[1024];
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, -1, false, buf, sizeof(buf)) == B_OK)
		reg.LoadFromDir(std::string(buf) + "/campiello/handlers");
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, buf, sizeof(buf)) == B_OK)
		reg.LoadFromDir(std::string(buf) + "/Campiello/handlers");
	return reg;
}

} // namespace

void RegisterShareType()
{
	BMimeType mime(kShareType);
	if (!mime.IsInstalled())
		mime.Install();
	mime.SetShortDescription("Condivisione di rete Campiello");
	mime.SetPreferredApp(kHelperSig);
	SetTypeIcon(mime);
}

std::string DefaultShareFolder()
{
	char home[1024];
	if (find_directory(B_USER_DIRECTORY, -1, false, home, sizeof(home)) != B_OK)
		return "";
	std::string dir = std::string(home) + "/WON";
	::mkdir(dir.c_str(), 0755);
	return dir;
}

void SyncShareFolder(const std::string& folderPath, const std::vector<NetworkService>& services)
{
	if (folderPath.empty())
		return;
	::mkdir(folderPath.c_str(), 0755);

	// Load device-handler add-ons once, so a discovered Fire TV (etc.) becomes a shortcut that
	// launches its remote instead of a plain info card.
	HandlerRegistry handlers = LoadHandlers();

	// Write a shortcut for every discovered service (SMB share, device add-on, web bookmark, or info
	// card), so the WON folder mirrors the whole network neighborhood. Collect the desired file names.
	std::set<std::string> desired;
	for (const NetworkService& svc : services) {
		std::string name = SanitizeFileName(svc.label);
		if (!desired.insert(name).second)
			continue; // two services with the same label: keep the first, skip the duplicate
		WriteServiceFile(folderPath + "/" + name, svc, handlers);
	}

	// Remove shortcuts WE created (marked with kAttrWon) for services no longer present. Never touch
	// the user's own files, nor the live mount directories (which lack the marker).
	BDirectory dir(folderPath.c_str());
	if (dir.InitCheck() != B_OK)
		return;
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK || desired.find(name) != desired.end())
			continue;
		BNode node(&entry);
		attr_info ai;
		if (node.GetAttrInfo(kAttrWon, &ai) == B_OK) // ours to prune
			entry.Remove();
	}
}

} // namespace vicinato
} // namespace campiello
