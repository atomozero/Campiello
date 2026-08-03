// campiello_mount.cpp
//
// The Campiello connect helper (M1): a small GUI that collects an SFTP host, user, and
// credentials, verifies them (validating the login and pinning the host key on first use with
// a one-tap prompt), mounts the host as a userlandfs volume, and opens it in Tracker. The mount
// is read-only by default; a checkbox opts in to a read-write mount (the SFTP backend implements
// the write path). This is the piece that removes the terminal step from M1's experience gate.
//
// Haiku-only. End-user strings are Italian (working agreement rule 4). The connect blocks the
// window thread briefly during the SSH handshake; a worker thread is a later refinement.

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Font.h>
#include <GridLayout.h>
#include <GridView.h>
#include <LayoutBuilder.h>
#include <LayoutItem.h>
#include <Roster.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include <fs_volume.h>
#include <sys/stat.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "../backend/SftpBackend.h"
#include "../backend/SftpKnownHosts.h"
#include "../fuse/SftpMount.h"

using namespace campiello::fondamenta;

static const char* const kSignature = "application/x-vnd.Campiello-mount";

static const uint32 kMsgConnect = 'conn';

namespace {

// <settings>/Campiello/sftp_known_hosts, creating the Campiello directory if absent.
std::string KnownHostsPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) == B_OK) {
		std::string dir = std::string(settings) + "/Campiello";
		::mkdir(dir.c_str(), 0755);
		return dir + "/sftp_known_hosts";
	}
	return "sftp_known_hosts";
}

// Create a directory and any missing parents (like mkdir -p).
void MakeDirs(const std::string& path)
{
	std::string acc;
	size_t i = 0;
	while (i < path.size()) {
		size_t slash = path.find('/', i);
		if (slash == std::string::npos)
			slash = path.size();
		acc = path.substr(0, slash);
		if (!acc.empty())
			::mkdir(acc.c_str(), 0755);
		i = slash + 1;
	}
}

// A disk name usable as a top-level mount point: keep letters, digits, and a few safe
// punctuation marks, fold everything else (slashes, spaces, dots) to '-'. So "192.168.1.5"
// stays legible and "My Server" becomes "My-Server".
std::string SanitizeDiskName(const std::string& in)
{
	std::string out;
	for (char c : in) {
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
		out += ok ? c : '-';
	}
	while (!out.empty() && out.front() == '.')
		out.erase(out.begin()); // no leading dots (hidden entry)
	return out;
}

} // namespace

class ConnectWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	explicit ConnectWindow(const SftpConfig& initial = SftpConfig());
	void MessageReceived(BMessage* message) override;

private:
	void DoConnect();
	void SetStatus(const char* text);

	BTextControl* fHost;
	BTextControl* fPort;
	BTextControl* fUser;
	BTextControl* fPassword;
	BTextControl* fKey;
	BTextControl* fRemotePath;
	BTextControl* fMountPoint;
	BCheckBox*    fWritable;
	BStringView*  fStatus;
};

// The classic "reveal password" eye: a small clickable view next to a password field that toggles
// its hidden-typing mode, drawing an open eye when the text is visible and a struck-through eye when
// it is masked. Self-contained (toggles on click, no message needed).
class EyeToggle : public BView {
public:
	explicit EyeToggle(BTextControl* target)
		: BView("eye", B_WILL_DRAW), fTarget(target)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetToolTip("Mostra o nascondi la password");
	}

	BSize MinSize() override { return BSize(26, 20); }
	BSize MaxSize() override { return MinSize(); }
	BSize PreferredSize() override { return MinSize(); }

	void MouseDown(BPoint) override
	{
		fRevealed = !fRevealed;
		fTarget->TextView()->HideTyping(!fRevealed);
		// Re-set the text so the existing characters re-render under the new masking mode.
		BString current = fTarget->Text();
		fTarget->SetText(current.String());
		Invalidate();
	}

	void Draw(BRect) override
	{
		BRect b = Bounds();
		BPoint c(b.Width() / 2, b.Height() / 2);
		rgb_color line = {90, 100, 115, 255};
		SetHighColor(line);
		SetPenSize(1.6f);
		StrokeEllipse(c, 9, 5.5f);                 // eye outline (almond approximated by an ellipse)
		SetHighColor(fRevealed ? (rgb_color){70, 120, 190, 255} : line);
		FillEllipse(c, 3.0f, 3.0f);                // iris/pupil
		if (!fRevealed) {                          // masked: strike it through
			SetHighColor(line);
			SetPenSize(1.8f);
			StrokeLine(BPoint(c.x - 10, c.y + 7), BPoint(c.x + 10, c.y - 7));
		}
	}

private:
	BTextControl* fTarget;
	bool          fRevealed = false;
};

// A grid view of label:field rows, so every label right-aligns in one column and every field
// aligns in the next (the Haiku idiom for a tidy form). Wrapped in a labeled BBox. If `eyeField`
// is one of the fields, a reveal-password eye is placed in a third column on that row.
static BBox* FieldBox(const char* label, const std::vector<BTextControl*>& fields,
	BTextControl* eyeField = nullptr)
{
	BGridView* grid = new BGridView(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING);
	BGridLayout* layout = grid->GridLayout();
	for (int32 row = 0; row < (int32)fields.size(); ++row) {
		layout->AddItem(fields[row]->CreateLabelLayoutItem(), 0, row);
		layout->AddItem(fields[row]->CreateTextViewLayoutItem(), 1, row);
		if (eyeField != nullptr && fields[row] == eyeField)
			layout->AddView(new EyeToggle(eyeField), 2, row);
	}
	layout->SetInsets(B_USE_DEFAULT_SPACING);

	BBox* box = new BBox("box");
	box->SetLabel(label);
	box->AddChild(grid);
	return box;
}

ConnectWindow::ConnectWindow(const SftpConfig& initial)
	:
	BWindow(BRect(100, 100, 480, 400), "Campiello",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS)
{
	// Fields prefilled from the launch arguments (the Vicinato passes the picked host).
	std::string portText = initial.port != 0 ? std::to_string(initial.port) : "22";
	fHost = new BTextControl("host", "Host:", initial.host.c_str(), nullptr);
	fPort = new BTextControl("port", "Porta:", portText.c_str(), nullptr);
	fUser = new BTextControl("user", "Utente:", initial.user.c_str(), nullptr);
	fPassword = new BTextControl("password", "Password:", "", nullptr);
	fPassword->TextView()->HideTyping(true);
	fKey = new BTextControl("key", "Chiave privata:", initial.privateKeyPath.c_str(), nullptr);
	fRemotePath = new BTextControl("path", "Cartella remota:",
		initial.basePath.empty() ? "." : initial.basePath.c_str(), nullptr);
	// A disk NAME, not a path: the volume mounts at /<name> so it shows on the Desktop like the
	// other disks. Left empty, it defaults to the host.
	fMountPoint = new BTextControl("mount", "Nome disco:", "", nullptr);

	BStringView* title = new BStringView("title", "Connetti a un host SFTP");
	BFont titleFont(be_bold_font);
	titleFont.SetSize(titleFont.Size() * 1.4f);
	title->SetFont(&titleFont);

	BStringView* subtitle = new BStringView("subtitle",
		"Il server appare come disco sul Desktop.");

	// Write access is an explicit opt-in: the default is a safe read-only mount, and ticking this
	// box mounts the volume read-write so you can also save and delete on the server.
	fWritable = new BCheckBox("writable",
		"Consenti la scrittura (monta in lettura e scrittura)", nullptr);

	BButton* connect = new BButton("connect", "Connetti", new BMessage(kMsgConnect));
	fStatus = new BStringView("status", "");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(subtitle)
		.Add(FieldBox("Server", {fHost, fPort, fUser, fPassword, fKey}, fPassword))
		.Add(FieldBox("Volume", {fRemotePath, fMountPoint}))
		.Add(fWritable)
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(connect)
		.End();

	SetDefaultButton(connect);
	CenterOnScreen();
}

void ConnectWindow::SetStatus(const char* text)
{
	fStatus->SetText(text);
}

void ConnectWindow::MessageReceived(BMessage* message)
{
	if (message->what == kMsgConnect) {
		DoConnect();
		return;
	}
	BWindow::MessageReceived(message);
}

void ConnectWindow::DoConnect()
{
	SftpConfig config;
	config.host = fHost->Text();
	config.port = static_cast<uint16_t>(std::atoi(fPort->Text()));
	if (config.port == 0)
		config.port = 22;
	config.user = fUser->Text();
	config.password = fPassword->Text();
	config.privateKeyPath = fKey->Text();
	config.basePath = fRemotePath->Text();

	// Mount at a top-level path /<name> so Tracker shows it as a disk on the Desktop (volumes
	// mounted deep inside another volume do not get a Desktop icon). The name defaults to the
	// host.
	std::string diskName = SanitizeDiskName(fMountPoint->Text());
	if (diskName.empty())
		diskName = SanitizeDiskName(config.host);
	if (diskName.empty())
		diskName = "sftp";
	std::string mountPoint = "/" + diskName;

	if (config.host.empty() || config.user.empty()) {
		SetStatus("Inserisci almeno host e utente.");
		return;
	}
	// A password reaches the mount add-on through a whitespace-split parameter string, so a
	// space in it would break the mount. Steer the user to a key file in that case.
	if (config.privateKeyPath.empty()
		&& config.password.find(' ') != std::string::npos) {
		SetStatus("La password contiene spazi: usa una chiave privata.");
		return;
	}

	SetStatus("Connessione in corso...");

	std::string knownHostsPath = KnownHostsPath();
	SftpKnownHosts knownHosts;
	knownHosts.LoadFromFile(knownHostsPath);

	// Host-key trust-on-first-use with a one-tap prompt (the interactive half the headless
	// mount add-on cannot do). The friendly host label is the user's own input, shown as typed.
	SftpBackend::HostKeyDecision decision =
		[&](HostKeyStatus status, const std::vector<uint8_t>&) {
			const char* text = (status == HostKeyStatus::kUnknown)
				? "Host mai visto prima. Vuoi fidarti di questa macchina e ricordarla?"
				: "ATTENZIONE: la chiave di questo host e' CAMBIATA (possibile "
				  "impersonificazione). Fidarsi comunque?";
			BAlert* alert = new BAlert("Campiello", text, "Annulla", "Fidati", nullptr,
				B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			return alert->Go() == 1; // button index 1 = "Fidati"
		};

	SftpBackend backend;
	BackendStatus s = backend.Connect(config, knownHosts, knownHostsPath, decision);
	if (s != BackendStatus::kOk) {
		std::string msg = "Connessione fallita: ";
		msg += (backend.Error() != nullptr) ? backend.Error() : "errore sconosciuto";
		SetStatus(msg.c_str());
		return;
	}
	backend.Disconnect(); // the mount add-on opens its own session

	MakeDirs(mountPoint);
	std::string params = BuildMountParameters(config);
	// Read-only by default; the checkbox opts in to a read-write mount (no B_MOUNT_READ_ONLY, so
	// the kernel lets write-intent opens through to the backend's SFTP write path).
	bool writable = fWritable->Value() == B_CONTROL_ON;
	uint32 mountFlags = writable ? 0 : B_MOUNT_READ_ONLY;
	dev_t dev = fs_mount_volume(mountPoint.c_str(), nullptr, "userlandfs",
		mountFlags, params.c_str());
	if (dev < 0) {
		SetStatus("Montaggio fallito. Il pacchetto Campiello e' installato?");
		return;
	}

	// Open the freshly mounted folder in Tracker.
	BEntry entry(mountPoint.c_str());
	entry_ref ref;
	if (entry.GetRef(&ref) == B_OK)
		be_roster->Launch(&ref);

	std::string ok = "Montato come disco \"" + diskName + "\" ("
		+ (writable ? "lettura e scrittura" : "sola lettura") + "): lo trovi sul Desktop.";
	SetStatus(ok.c_str());
}

int main(int argc, char** argv)
{
	BApplication app(kSignature);
	// Prefill from launch arguments (host=..., as the Vicinato passes them); reuses the mount
	// parser, ignoring its ok flag since a partial prefill is fine here.
	SftpMount mount = ParseSftpMount(argc, argv);
	ConnectWindow* window = new ConnectWindow(mount.config);
	window->Show();
	app.Run();
	return 0;
}
