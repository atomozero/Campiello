// campiello_smb_mount.cpp
//
// The Campiello SMB connect helper: a small GUI that collects a Windows share (server, share,
// user, password, optional domain), verifies the credentials, mounts the share as a read-only
// userlandfs volume via the campiello_smb add-on, and opens it in Tracker. The SMB counterpart
// to campiello_mount, minus the SSH host-key step (SMB authenticates with user/password/domain,
// there is nothing to pin).
//
// Haiku-only, OPTIONAL: links libsmb2 (LGPL-2.1) to validate the login before mounting. Ships in
// the separate campiello_smb package. End-user strings are Italian (working agreement rule 4).
// The blocking work (share enumeration, connect, mount) runs on worker threads so the dialog never
// freezes; when a password is remembered the dialog connects on its own (one tap).

#include <Alert.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Entry.h>
#include <Font.h>
#include <GridLayout.h>
#include <GridView.h>
#include <Messenger.h>
#include <OS.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <Node.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include <FindDirectory.h>
#include <fs_info.h>
#include <fs_volume.h>
#include <sys/stat.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "SmbBackend.h"
#include "SmbMount.h"

using namespace campiello::fondamenta;

// Haiku Locale Kit: user-facing strings go through B_TRANSLATE so they can be localized. The source
// strings are Italian (the default when no catalog matches the user's language); catalogs under
// data/locale/catalogs/<signature>/ translate them (en.catalog ships).
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SMB"

static const char* const kSignature = "application/x-vnd.Campiello-smb-mount";

static const uint32 kMsgConnect      = 'conn';
static const uint32 kMsgEnum         = 'enum';
static const uint32 kMsgAdvanced     = 'advn';
static const uint32 kMsgSharesReady  = 'shrd'; // background enum -> UI: the share list (or error)
static const uint32 kMsgConnectDone  = 'cdon'; // background connect+mount -> UI: the outcome
static const uint32 kMsgUnmount      = 'unmt'; // "Smonta" button (with a KDL confirmation)
static const uint32 kMsgOpenVolume   = 'opnv'; // "Apri" the already-mounted volume in Tracker

// Attributes on a WON share-shortcut file (see the WON folder writer). Double-clicking such a
// file opens this helper via its MIME type's preferred app, and we prefill from these.
static const char* const kAttrServer = "CAMPIELLO:server";
static const char* const kAttrShare  = "CAMPIELLO:share";
static const char* const kAttrUser   = "CAMPIELLO:user";

namespace {

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

// The WON folder (~/WON), where the network shortcuts live and where shares are mounted so the
// folder doubles as the live network neighborhood. Empty if the home directory can't be found.
std::string WonFolder()
{
	char home[B_PATH_NAME_LENGTH];
	if (find_directory(B_USER_DIRECTORY, -1, false, home, sizeof(home)) != B_OK)
		return "";
	return std::string(home) + "/WON";
}

// A disk name usable as a top-level mount point: keep letters, digits, and a few safe
// punctuation marks, fold everything else to '-', so the volume shows on the Desktop.
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

// Where the last-used SMB login is remembered (user and domain only, never the password), so a
// repeat connect needs only the password: <settings>/Campiello/smb_recent.
std::string RecentPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) == B_OK) {
		std::string dir = std::string(settings) + "/Campiello";
		::mkdir(dir.c_str(), 0755);
		return dir + "/smb_recent";
	}
	return "";
}

void LoadRecent(std::string& user, std::string& domain)
{
	std::string path = RecentPath();
	if (path.empty())
		return;
	FILE* f = std::fopen(path.c_str(), "r");
	if (f == nullptr)
		return;
	char line[512];
	if (std::fgets(line, sizeof(line), f) != nullptr) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		user = s;
	}
	if (std::fgets(line, sizeof(line), f) != nullptr) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		domain = s;
	}
	std::fclose(f);
}

void SaveRecent(const std::string& user, const std::string& domain)
{
	std::string path = RecentPath();
	if (path.empty() || user.empty())
		return;
	FILE* f = std::fopen(path.c_str(), "w");
	if (f == nullptr)
		return;
	std::fprintf(f, "%s\n%s\n", user.c_str(), domain.c_str());
	std::fclose(f);
}

// OPTIONAL, OPT-IN password storage, ENCRYPTED. Haiku's keystore developer headers are not present
// in this SDK, so instead of BKeyStore we encrypt the password with AES-256-GCM (OpenSSL) under a
// per-installation 32-byte key kept in an owner-only (0600) keyfile, and store the ciphertext in an
// owner-only secrets file keyed by server+user. The secrets file is useless without the keyfile
// (e.g. if copied elsewhere). Caveat: a process running as this user can read both files, so this
// protects at rest, not against local code running as you. Off by default. See docs/SMB.md.
std::string SecretsPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) == B_OK) {
		std::string dir = std::string(settings) + "/Campiello";
		::mkdir(dir.c_str(), 0755);
		return dir + "/smb_secrets";
	}
	return "";
}

std::string HexEncode(const std::string& s)
{
	static const char* kHex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		out.push_back(kHex[c >> 4]);
		out.push_back(kHex[c & 0x0f]);
	}
	return out;
}

std::string HexDecode(const std::string& s)
{
	auto nyb = [](char c) -> int {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		return -1;
	};
	std::string out;
	for (size_t i = 0; i + 1 < s.size(); i += 2) {
		int hi = nyb(s[i]), lo = nyb(s[i + 1]);
		if (hi < 0 || lo < 0)
			break;
		out.push_back(static_cast<char>((hi << 4) | lo));
	}
	return out;
}

// The per-installation AES key file.
std::string KeyPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) == B_OK)
		return std::string(settings) + "/Campiello/smb_secret.key";
	return "";
}

// Load the 32-byte key, creating it (random, 0600) on first use. Returns false if unavailable.
bool LoadOrCreateKey(unsigned char key[32])
{
	std::string path = KeyPath();
	if (path.empty())
		return false;
	FILE* f = std::fopen(path.c_str(), "rb");
	if (f != nullptr) {
		size_t n = std::fread(key, 1, 32, f);
		std::fclose(f);
		if (n == 32)
			return true;
	}
	if (RAND_bytes(key, 32) != 1)
		return false;
	FILE* w = std::fopen(path.c_str(), "wb");
	if (w == nullptr)
		return false;
	::chmod(path.c_str(), 0600); // owner-only, before writing the secret
	bool ok = std::fwrite(key, 1, 32, w) == 32;
	std::fclose(w);
	return ok;
}

// AES-256-GCM. Ciphertext blob = nonce(12) | ciphertext | tag(16). `aad` binds it to server+user.
bool Encrypt(const unsigned char key[32], const std::string& aad, const std::string& plain,
	std::vector<unsigned char>& blob)
{
	unsigned char nonce[12];
	if (RAND_bytes(nonce, sizeof(nonce)) != 1)
		return false;
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == nullptr)
		return false;
	bool ok = false;
	int len = 0;
	std::vector<unsigned char> ct(plain.size());
	unsigned char tag[16];
	if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
		&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(nonce), nullptr) == 1
		&& EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) == 1
		&& (aad.empty() || EVP_EncryptUpdate(ctx, nullptr, &len,
			reinterpret_cast<const unsigned char*>(aad.data()), (int)aad.size()) == 1)
		&& EVP_EncryptUpdate(ctx, ct.data(), &len,
			reinterpret_cast<const unsigned char*>(plain.data()), (int)plain.size()) == 1) {
		int ctlen = len;
		if (EVP_EncryptFinal_ex(ctx, ct.data() + ctlen, &len) == 1
			&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag) == 1) {
			ctlen += len;
			blob.assign(nonce, nonce + sizeof(nonce));
			blob.insert(blob.end(), ct.begin(), ct.begin() + ctlen);
			blob.insert(blob.end(), tag, tag + sizeof(tag));
			ok = true;
		}
	}
	EVP_CIPHER_CTX_free(ctx);
	return ok;
}

// Reverse of Encrypt; also verifies the GCM tag (fails on a wrong key or tampering).
bool Decrypt(const unsigned char key[32], const std::string& aad,
	const std::vector<unsigned char>& blob, std::string& plain)
{
	if (blob.size() < 12 + 16)
		return false;
	const unsigned char* nonce = blob.data();
	const unsigned char* ct = blob.data() + 12;
	int ctlen = (int)(blob.size() - 12 - 16);
	const unsigned char* tag = blob.data() + blob.size() - 16;
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == nullptr)
		return false;
	bool ok = false;
	int len = 0;
	std::vector<unsigned char> out(ctlen > 0 ? ctlen : 1);
	if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
		&& EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1
		&& EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) == 1
		&& (aad.empty() || EVP_DecryptUpdate(ctx, nullptr, &len,
			reinterpret_cast<const unsigned char*>(aad.data()), (int)aad.size()) == 1)
		&& EVP_DecryptUpdate(ctx, out.data(), &len, ct, ctlen) == 1) {
		int outlen = len;
		if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
				const_cast<unsigned char*>(tag)) == 1
			&& EVP_DecryptFinal_ex(ctx, out.data() + outlen, &len) == 1) {
			outlen += len;
			plain.assign(reinterpret_cast<char*>(out.data()), outlen);
			ok = true;
		}
	}
	EVP_CIPHER_CTX_free(ctx);
	return ok;
}

// The AAD that binds a ciphertext to one account (so entries can't be swapped between accounts).
std::string SecretAad(const std::string& server, const std::string& user)
{
	return server + std::string("\0", 1) + user;
}

// Rewrite the secrets file (one "server\tuser\thexciphertext" line per entry), dropping any entry
// for this server+user and, if `password` is non-null, adding the new (encrypted) one.
void WriteSecret(const std::string& server, const std::string& user, const std::string* password)
{
	std::string path = SecretsPath();
	if (path.empty())
		return;
	std::string prefix = server + "\t" + user + "\t";
	std::vector<std::string> keep;
	FILE* in = std::fopen(path.c_str(), "r");
	if (in != nullptr) {
		char line[2048];
		while (std::fgets(line, sizeof(line), in) != nullptr) {
			std::string s(line);
			while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
				s.pop_back();
			if (s.empty() || s.compare(0, prefix.size(), prefix) == 0)
				continue; // drop blanks and the entry we are replacing/removing
			keep.push_back(s);
		}
		std::fclose(in);
	}
	if (password != nullptr) {
		unsigned char key[32];
		std::vector<unsigned char> blob;
		if (LoadOrCreateKey(key) && Encrypt(key, SecretAad(server, user), *password, blob)) {
			keep.push_back(prefix
				+ HexEncode(std::string(reinterpret_cast<char*>(blob.data()), blob.size())));
		}
		std::memset(key, 0, sizeof(key));
		// If encryption is unavailable we simply do not store it - never a plaintext fallback.
	}

	FILE* out = std::fopen(path.c_str(), "w");
	if (out == nullptr)
		return;
	::chmod(path.c_str(), 0600); // owner-only
	for (const std::string& s : keep)
		std::fprintf(out, "%s\n", s.c_str());
	std::fclose(out);
}

void SavePassword(const std::string& server, const std::string& user, const std::string& password)
{
	if (server.empty() || user.empty() || password.empty())
		return;
	WriteSecret(server, user, &password);
}

void ForgetPassword(const std::string& server, const std::string& user)
{
	if (server.empty() || user.empty())
		return;
	WriteSecret(server, user, nullptr);
}

bool LoadPassword(const std::string& server, const std::string& user, std::string& password)
{
	std::string path = SecretsPath();
	if (path.empty() || server.empty() || user.empty())
		return false;
	FILE* f = std::fopen(path.c_str(), "r");
	if (f == nullptr)
		return false;
	std::string prefix = server + "\t" + user + "\t";
	char line[2048];
	bool found = false;
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		if (s.compare(0, prefix.size(), prefix) == 0) {
			std::string raw = HexDecode(s.substr(prefix.size()));
			std::vector<unsigned char> blob(raw.begin(), raw.end());
			unsigned char key[32];
			if (LoadOrCreateKey(key) && Decrypt(key, SecretAad(server, user), blob, password))
				found = true;
			std::memset(key, 0, sizeof(key));
			break;
		}
	}
	std::fclose(f);
	return found;
}

// Per-host remembered choices, so reconnecting to a known server prefills everything and (with the
// password) becomes one tap. One tab-separated line per server in <settings>/Campiello/smb_hosts:
// server, user, domain, share, asDisk, diskName - the string fields hex-encoded so they are
// tab/newline-safe. Also the list #5 (auto-mount at login) walks.
struct HostPrefs {
	std::string user;
	std::string domain;
	std::string share;
	std::string diskName;
	bool        asDisk = false;
	bool        readOnly = false; // mount read-only (safer) vs read-write
	bool        found = false;
};

std::string HostsPath()
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) == B_OK) {
		std::string dir = std::string(settings) + "/Campiello";
		::mkdir(dir.c_str(), 0755);
		return dir + "/smb_hosts";
	}
	return "";
}

// Split a line into up to `max` tab-separated fields.
std::vector<std::string> SplitTabs(const std::string& s)
{
	std::vector<std::string> out;
	size_t start = 0;
	for (size_t i = 0; i <= s.size(); ++i) {
		if (i == s.size() || s[i] == '\t') {
			out.push_back(s.substr(start, i - start));
			start = i + 1;
		}
	}
	return out;
}

// Read all host records (used by LoadHost and by the auto-mount at login).
std::vector<std::pair<std::string, HostPrefs>> LoadAllHosts()
{
	std::vector<std::pair<std::string, HostPrefs>> hosts;
	std::string path = HostsPath();
	if (path.empty())
		return hosts;
	FILE* f = std::fopen(path.c_str(), "r");
	if (f == nullptr)
		return hosts;
	char line[4096];
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		std::vector<std::string> f6 = SplitTabs(s);
		if (f6.size() < 6)
			continue;
		HostPrefs p;
		std::string server = HexDecode(f6[0]);
		p.user = HexDecode(f6[1]);
		p.domain = HexDecode(f6[2]);
		p.share = HexDecode(f6[3]);
		p.asDisk = f6[4] == "1";
		p.diskName = HexDecode(f6[5]);
		p.readOnly = f6.size() >= 7 && f6[6] == "1"; // older 6-field records default to read-write
		p.found = true;
		if (!server.empty())
			hosts.emplace_back(server, p);
	}
	std::fclose(f);
	return hosts;
}

HostPrefs LoadHost(const std::string& server)
{
	if (!server.empty()) {
		for (auto& h : LoadAllHosts()) {
			if (h.first == server)
				return h.second;
		}
	}
	return HostPrefs();
}

void SaveHost(const std::string& server, const HostPrefs& p)
{
	std::string path = HostsPath();
	if (path.empty() || server.empty())
		return;
	auto hosts = LoadAllHosts();
	bool replaced = false;
	for (auto& h : hosts) {
		if (h.first == server) {
			h.second = p;
			replaced = true;
			break;
		}
	}
	if (!replaced)
		hosts.emplace_back(server, p);

	FILE* f = std::fopen(path.c_str(), "w");
	if (f == nullptr)
		return;
	::chmod(path.c_str(), 0600); // owner-only (it names your servers and usernames)
	for (auto& h : hosts) {
		std::fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
			HexEncode(h.first).c_str(), HexEncode(h.second.user).c_str(),
			HexEncode(h.second.domain).c_str(), HexEncode(h.second.share).c_str(),
			h.second.asDisk ? "1" : "0", HexEncode(h.second.diskName).c_str(),
			h.second.readOnly ? "1" : "0");
	}
	std::fclose(f);
}

// Append a diagnostic line to <settings>/Campiello/smb.log so login failures can be inspected
// (the exact NT status from the server is the useful part).
void LogSmb(const char* context, const std::string& detail)
{
	char settings[1024];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, settings, sizeof(settings)) != B_OK)
		return;
	std::string dir = std::string(settings) + "/Campiello";
	::mkdir(dir.c_str(), 0755);
	FILE* f = std::fopen((dir + "/smb.log").c_str(), "a");
	if (f == nullptr)
		return;
	std::fprintf(f, "[%s] %s\n", context, detail.c_str());
	std::fclose(f);
}

// Read a WON share-shortcut file's attributes into a config (server, optional share/user), so a
// double-click prefills the dialog. Returns true if at least a server was found.
bool ReadShareFile(const entry_ref& ref, SmbConfig& out)
{
	BNode node(&ref);
	if (node.InitCheck() != B_OK)
		return false;
	BString value;
	if (node.ReadAttrString(kAttrServer, &value) == B_OK)
		out.server = value.String();
	if (node.ReadAttrString(kAttrShare, &value) == B_OK)
		out.share = value.String();
	if (node.ReadAttrString(kAttrUser, &value) == B_OK)
		out.user = value.String();
	return !out.server.empty();
}

// The path of the login hook: a small script in the classic ~/config/settings/boot/launch/ folder,
// which Haiku runs at login. Its presence means "auto-mount my remembered shares at login".
std::string LoginHookPath()
{
	char home[B_PATH_NAME_LENGTH];
	if (find_directory(B_USER_DIRECTORY, -1, false, home, sizeof(home)) != B_OK)
		return "";
	return std::string(home) + "/config/settings/boot/launch/campiello-smb-automount";
}

bool LoginHookInstalled()
{
	std::string path = LoginHookPath();
	if (path.empty())
		return false;
	struct stat st;
	return ::stat(path.c_str(), &st) == 0;
}

void InstallLoginHook()
{
	std::string path = LoginHookPath();
	if (path.empty())
		return;
	MakeDirs(path.substr(0, path.find_last_of('/')));
	FILE* f = std::fopen(path.c_str(), "w");
	if (f == nullptr)
		return;
	std::fprintf(f, "#!/bin/sh\n# Auto-mount Campiello SMB shares at login (managed by the helper).\n"
		"exec /boot/system/apps/campiello_smb_mount --automount\n");
	std::fclose(f);
	::chmod(path.c_str(), 0755);
}

void RemoveLoginHook()
{
	std::string path = LoginHookPath();
	if (!path.empty())
		::remove(path.c_str());
}

// Auto-mount every remembered host that has a stored password. Headless (no window): used by the
// login hook. Skips a host whose mount point is already there. Returns how many it mounted.
int AutoMountAll()
{
	std::string won = WonFolder();
	int mounted = 0;
	for (auto& h : LoadAllHosts()) {
		const std::string& server = h.first;
		const HostPrefs& p = h.second;
		std::string password;
		if (!LoadPassword(server, p.user, password))
			continue; // only hosts whose password we remembered can be mounted unattended
		SmbConfig c;
		c.server = server;
		c.user = p.user;
		c.domain = p.domain;
		c.share = p.share;
		c.password = password;

		std::string diskName = SanitizeDiskName(p.diskName);
		if (diskName.empty())
			diskName = SanitizeDiskName(p.share.empty() ? server : p.share);
		if (diskName.empty())
			diskName = "smb";
		bool asDisk = p.asDisk || won.empty();
		std::string mountPoint = asDisk ? ("/" + diskName) : (won + "/" + diskName);

		MakeDirs(mountPoint);
		std::string params = BuildSmbMountParameters(c);
		uint32 flags = p.readOnly ? B_MOUNT_READ_ONLY : 0;
		dev_t dev = fs_mount_volume(mountPoint.c_str(), nullptr, "userlandfs", flags, params.c_str());
		if (dev >= 0)
			++mounted;
		else
			LogSmb("automount", "server='" + server + "' -> mount failed (dev=" + std::to_string(dev)
				+ ")");
	}
	return mounted;
}

} // namespace

class ConnectWindow : public BWindow {
public:
	bool QuitRequested() override { be_app->PostMessage(B_QUIT_REQUESTED); return true; }
	explicit ConnectWindow(const SmbConfig& initial = SmbConfig());
	void MessageReceived(BMessage* message) override;

private:
	void DoConnect();                                      // starts the background connect+mount
	void DoUnmount();                                       // unmount this host's volume (KDL-warned)
	void DoEnumShares();                                    // starts the background enumeration
	void ShowSharePicker(const std::vector<std::string>& shares); // shows the result (UI thread)
	void SetStatus(const char* text);
	void SetBusy(bool busy);                               // enable/disable buttons during work
	std::string ComputeMountPoint(std::string& diskName);  // the mount path from the current fields
	void BuildConnectForm(const SmbConfig& initial, const HostPrefs& host); // the full login form
	void BuildMountedView();                               // the compact "already mounted" panel

	BTextControl* fServer;
	BTextControl* fShare;
	BTextControl* fUser;
	BTextControl* fPassword;
	BTextControl* fDomain;
	BTextControl* fPath;
	BTextControl* fMountPoint;
	BButton*      fConnectButton;
	BButton*      fEnumButton;
	BButton*      fUnmountButton;
	BCheckBox*    fRemember;    // opt-in: store the password on this computer
	BCheckBox*    fAsDisk;      // mount as a top-level Desktop disk instead of inside ~/WON
	BCheckBox*    fAutoMount;   // opt-in: mount remembered shares at login
	BCheckBox*    fReadOnly;    // mount read-only (no writing to the share)
	BBox*         fAdvancedBox; // the advanced fields, hidden until requested
	BStringView*  fStatus;
	bool          fAutoConnect = false; // one-tap: mount immediately if a password is remembered
	bool          fMountedMode = false; // showing the compact "already mounted" panel
	std::string   fMountPath;   // the volume this host maps to (also the unmount/open target)
	std::string   fMountName;
};

namespace {
// True if `mountPoint` is a mount root: its device id differs from its parent directory's.
bool IsMountedPath(const std::string& mountPoint)
{
	std::string parent = mountPoint.substr(0, mountPoint.find_last_of('/'));
	if (parent.empty())
		parent = "/";
	dev_t here = dev_for_path(mountPoint.c_str());
	dev_t up = dev_for_path(parent.c_str());
	return here >= 0 && here != up;
}
} // namespace

// A grid view of label:field rows, wrapped in a labeled BBox (the Haiku idiom for a tidy form).
// The classic "reveal password" eye: a small clickable view next to a password field that toggles
// its hidden-typing mode, drawing an open eye when visible and a struck-through eye when masked.
class EyeToggle : public BView {
public:
	explicit EyeToggle(BTextControl* target)
		: BView("eye", B_WILL_DRAW), fTarget(target)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetToolTip(B_TRANSLATE("Mostra o nascondi la password"));
	}

	BSize MinSize() override { return BSize(26, 20); }
	BSize MaxSize() override { return MinSize(); }
	BSize PreferredSize() override { return MinSize(); }

	void MouseDown(BPoint) override
	{
		fRevealed = !fRevealed;
		fTarget->TextView()->HideTyping(!fRevealed);
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
		StrokeEllipse(c, 9, 5.5f);
		SetHighColor(fRevealed ? (rgb_color){70, 120, 190, 255} : line);
		FillEllipse(c, 3.0f, 3.0f);
		if (!fRevealed) {
			SetHighColor(line);
			SetPenSize(1.8f);
			StrokeLine(BPoint(c.x - 10, c.y + 7), BPoint(c.x + 10, c.y - 7));
		}
	}

private:
	BTextControl* fTarget;
	bool          fRevealed = false;
};

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

ConnectWindow::ConnectWindow(const SmbConfig& initial)
	:
	BWindow(BRect(100, 100, 480, 400), "Campiello",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS)
{
	HostPrefs host = LoadHost(initial.server);

	// The volume this host maps to (same rule as the mount). If it is already mounted, show a
	// compact "it's mounted" panel (Apri / Smonta) instead of the whole login form.
	std::string share0 = !initial.share.empty() ? initial.share : (host.found ? host.share : "");
	bool asDisk0 = host.found ? host.asDisk : false;
	std::string diskName0 = SanitizeDiskName(host.found ? host.diskName : "");
	if (diskName0.empty())
		diskName0 = SanitizeDiskName(share0.empty() ? initial.server : share0);
	if (diskName0.empty())
		diskName0 = "smb";
	std::string won0 = WonFolder();
	if (won0.empty())
		asDisk0 = true;
	fMountName = diskName0;
	fMountPath = asDisk0 ? ("/" + diskName0) : (won0 + "/" + diskName0);

	if (!initial.server.empty() && IsMountedPath(fMountPath)) {
		fMountedMode = true;
		BuildMountedView();
	} else {
		BuildConnectForm(initial, host);
	}
	CenterOnScreen();
}

void ConnectWindow::BuildMountedView()
{
	BStringView* title = new BStringView("title", B_TRANSLATE("Condivisione montata"));
	BFont titleFont(be_bold_font);
	titleFont.SetSize(titleFont.Size() * 1.4f);
	title->SetFont(&titleFont);

	BString l1;
	l1 << B_TRANSLATE("Il volume \"") << fMountName.c_str() << B_TRANSLATE("\" e' gia' montato.");
	BString l2;
	l2 << B_TRANSLATE("Percorso: ") << fMountPath.c_str();
	BStringView* line1 = new BStringView("l1", l1.String());
	BStringView* line2 = new BStringView("l2", l2.String());
	fStatus = new BStringView("status", "");

	BButton* open = new BButton("open", B_TRANSLATE("Apri"), new BMessage(kMsgOpenVolume));
	fUnmountButton = new BButton("unmount", B_TRANSLATE("Smonta"), new BMessage(kMsgUnmount));
	BButton* close = new BButton("close", B_TRANSLATE("Chiudi"), new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(line1)
		.Add(line2)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(open)
			.AddGlue()
			.Add(fUnmountButton)
			.Add(close)
		.End();

	SetDefaultButton(open);
}

void ConnectWindow::BuildConnectForm(const SmbConfig& initial, const HostPrefs& host)
{
	// Prefill from the launch arguments (the WON app passes the picked server), then from this
	// host's remembered choices (user, domain, share, disk name, "as disk"), then from the global
	// last-used login, and a remembered password if opted in - so reconnecting a known host needs
	// nothing typed.
	std::string recentUser, recentDomain;
	LoadRecent(recentUser, recentDomain);
	std::string user = !initial.user.empty() ? initial.user
		: (host.found && !host.user.empty() ? host.user : recentUser);
	std::string domain = !initial.domain.empty() ? initial.domain
		: (host.found ? host.domain : recentDomain);
	std::string share = !initial.share.empty() ? initial.share : (host.found ? host.share : "");
	std::string savedPw;
	bool hasSavedPw = LoadPassword(initial.server, user, savedPw);

	fServer = new BTextControl("server", B_TRANSLATE("Server:"), initial.server.c_str(), nullptr);
	fShare = new BTextControl("share", B_TRANSLATE("Condivisione:"), share.c_str(), nullptr);
	fUser = new BTextControl("user", B_TRANSLATE("Utente:"), user.c_str(), nullptr);
	fPassword = new BTextControl("password", B_TRANSLATE("Password:"), savedPw.c_str(), nullptr);
	fPassword->TextView()->HideTyping(true);
	fDomain = new BTextControl("domain", B_TRANSLATE("Dominio:"), domain.c_str(), nullptr);
	fPath = new BTextControl("path", B_TRANSLATE("Sottocartella:"), initial.basePath.c_str(),
		nullptr);
	// A disk NAME, not a path: the volume mounts under ~/WON/<name>. Left empty it defaults to the
	// server (whole-server mount) or the chosen share.
	fMountPoint = new BTextControl("mount", B_TRANSLATE("Nome disco:"),
		host.found ? host.diskName.c_str() : "", nullptr);

	fRemember = new BCheckBox("remember", B_TRANSLATE("Ricorda password su questo computer"),
		nullptr);
	if (hasSavedPw)
		fRemember->SetValue(B_CONTROL_ON);
	fAsDisk = new BCheckBox("asdisk", B_TRANSLATE("Monta come disco sulla Scrivania"), nullptr);
	if (host.found && host.asDisk)
		fAsDisk->SetValue(B_CONTROL_ON);
	fAutoMount = new BCheckBox("automount", B_TRANSLATE("Monta i miei share all'avvio"), nullptr);
	if (LoginHookInstalled())
		fAutoMount->SetValue(B_CONTROL_ON);
	fReadOnly = new BCheckBox("readonly", B_TRANSLATE("Sola lettura (non scrivere sulla share)"),
		nullptr);
	if (host.found && host.readOnly)
		fReadOnly->SetValue(B_CONTROL_ON);

	BStringView* title = new BStringView("title",
		B_TRANSLATE("Connetti a una cartella condivisa Windows"));
	BFont titleFont(be_bold_font);
	titleFont.SetSize(titleFont.Size() * 1.4f);
	title->SetFont(&titleFont);

	BStringView* subtitle = new BStringView("subtitle",
		B_TRANSLATE("Utente e password per accedere. Condivisione vuota = sfoglia tutto il server;"));
	BStringView* subtitle2 = new BStringView("subtitle2",
		B_TRANSLATE("altrimenti premi \"Elenca condivisioni\" e scegline una."));

	// The share is a first-class choice now (empty = whole server, Windows \\server style); the
	// "Elenca condivisioni" button fills it. Domain/subfolder/disk-name stay behind Advanced.
	BBox* shareBox = FieldBox(B_TRANSLATE("Condivisione (vuoto = tutto il server)"), {fShare});
	fAdvancedBox = FieldBox(B_TRANSLATE("Dettagli"), {fDomain, fPath, fMountPoint});
	BCheckBox* advanced = new BCheckBox("adv", B_TRANSLATE("Opzioni avanzate"),
		new BMessage(kMsgAdvanced));

	fConnectButton = new BButton("connect", B_TRANSLATE("Connetti"), new BMessage(kMsgConnect));
	fEnumButton = new BButton("enum", B_TRANSLATE("Elenca condivisioni"), new BMessage(kMsgEnum));
	fUnmountButton = new BButton("unmount", B_TRANSLATE("Smonta"), new BMessage(kMsgUnmount));
	fStatus = new BStringView("status", "");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(title)
		.Add(subtitle)
		.Add(subtitle2)
		.Add(FieldBox(B_TRANSLATE("Accesso"), {fServer, fUser, fPassword}, fPassword))
		.Add(fRemember)
		.Add(fAsDisk)
		.Add(fAutoMount)
		.Add(fReadOnly)
		.Add(shareBox)
		.Add(advanced)
		.Add(fAdvancedBox)
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(fUnmountButton)
			.Add(fEnumButton)
			.Add(fConnectButton)
		.End();

	fAdvancedBox->Hide(); // collapsed until "Opzioni avanzate" is ticked
	SetDefaultButton(fConnectButton);

	// One-tap: if the password was remembered for this server+user, connect immediately without
	// making the user click. It queues until the window's message loop starts; a wrong saved
	// password just lands back in this dialog with an error.
	fAutoConnect = hasSavedPw && !std::string(fServer->Text()).empty()
		&& !std::string(fUser->Text()).empty();
	if (fAutoConnect)
		PostMessage(kMsgConnect);
}

void ConnectWindow::SetStatus(const char* text)
{
	fStatus->SetText(text);
}

// Disable the action buttons while a background connect/enumeration is running, so the user cannot
// launch a second one; re-enabled when it reports back.
void ConnectWindow::SetBusy(bool busy)
{
	fConnectButton->SetEnabled(!busy);
	fEnumButton->SetEnabled(!busy);
}

void ConnectWindow::MessageReceived(BMessage* message)
{
	if (message->what == kMsgConnect) {
		DoConnect();
		return;
	}
	if (message->what == kMsgUnmount) {
		DoUnmount();
		return;
	}
	if (message->what == kMsgOpenVolume) {
		BEntry entry(fMountPath.c_str());
		entry_ref ref;
		if (entry.GetRef(&ref) == B_OK)
			be_roster->Launch(&ref);
		return;
	}
	if (message->what == kMsgEnum) {
		DoEnumShares();
		return;
	}
	if (message->what == kMsgAdvanced) {
		if (fAdvancedBox->IsHidden())
			fAdvancedBox->Show();
		else
			fAdvancedBox->Hide();
		return;
	}
	if (message->what == kMsgSharesReady) {
		SetBusy(false);
		int32 status = B_ERROR;
		message->FindInt32("status", &status);
		if (status != static_cast<int32>(BackendStatus::kOk)) {
			const char* err = B_TRANSLATE("errore");
			message->FindString("error", &err);
			BString msg;
			if (status == static_cast<int32>(BackendStatus::kAccessDenied))
				msg = B_TRANSLATE("Accesso negato: ");
			else
				msg = B_TRANSLATE("Impossibile elencare le condivisioni: ");
			msg << err;
			SetStatus(msg.String());
			const char* user = "";
			const char* domain = "";
			message->FindString("user", &user);
			message->FindString("domain", &domain);
			LogSmb("enum", "user='" + std::string(user) + "' domain='" + std::string(domain)
				+ "' -> " + std::string(err));
			return;
		}
		std::vector<std::string> shares;
		const char* sh = nullptr;
		for (int32 i = 0; message->FindString("share", i, &sh) == B_OK; ++i)
			shares.push_back(sh);
		ShowSharePicker(shares);
		return;
	}
	if (message->what == kMsgConnectDone) {
		SetBusy(false);
		bool ok = false;
		message->FindBool("ok", &ok);
		if (ok) {
			// Honor the "mount at login" choice (a script in ~/config/settings/boot/launch/).
			if (fAutoMount->Value() == B_CONTROL_ON)
				InstallLoginHook();
			else
				RemoveLoginHook();
			// Open the freshly mounted volume in Tracker, then close this dialog: the Tracker window
			// is the confirmation, so the login box does not linger.
			const char* mp = "";
			message->FindString("mountpoint", &mp);
			BEntry entry(mp);
			entry_ref ref;
			if (entry.GetRef(&ref) == B_OK)
				be_roster->Launch(&ref);
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
		BString phase;
		message->FindString("phase", &phase);
		if (phase == "mount") {
			SetStatus(B_TRANSLATE("Montaggio fallito. Riprova; se il problema persiste, riavvia."));
			return;
		}
		if (phase == "noshares") {
			SetStatus(B_TRANSLATE("Nessuna condivisione visibile su questo server."));
			return;
		}
		// A validation (login) failure: surface the raw server status and a hint for the common case.
		int32 status = B_ERROR;
		message->FindInt32("status", &status);
		const char* detail = B_TRANSLATE("errore");
		message->FindString("detail", &detail);
		BString msg;
		if (status == static_cast<int32>(BackendStatus::kAccessDenied)) {
			msg = B_TRANSLATE("Accesso negato: ");
			msg << detail;
			if (BString(detail).IFindFirst("LOGON_FAILURE") >= 0)
				msg << B_TRANSLATE(" - usa la password dell'account Microsoft, non il PIN");
		} else if (status == static_cast<int32>(BackendStatus::kNotFound)) {
			msg = B_TRANSLATE("Condivisione non trovata: ");
			msg << detail;
		} else {
			msg = B_TRANSLATE("Connessione fallita: ");
			msg << detail;
		}
		SetStatus(msg.String());
		const char* user = "";
		const char* domain = "";
		const char* share = "";
		message->FindString("user", &user);
		message->FindString("domain", &domain);
		message->FindString("share", &share);
		LogSmb(std::string(share).empty() ? "enum" : "connect", "user='" + std::string(user)
			+ "' domain='" + std::string(domain) + "' share='" + std::string(share) + "' -> "
			+ std::string(detail));
		return;
	}
	BWindow::MessageReceived(message);
}

// The share picker runs the enumeration OFF the UI thread. EnumShares is a blocking network call
// (connect to IPC$ + srvsvc enum, up to ~15s), so doing it on the window's looper would freeze the
// whole dialog while it runs - the user would see no "Ricerca..." feedback and think the menu is
// broken. DoEnumShares spawns a worker that posts kMsgSharesReady back; the looper then shows the
// menu with ShowSharePicker while staying responsive.
namespace {
struct EnumJob {
	SmbConfig  config;
	BMessenger reply;
};

int32 EnumThread(void* arg)
{
	EnumJob* job = static_cast<EnumJob*>(arg);
	SmbBackend backend;
	std::vector<std::string> shares;
	BackendStatus s = backend.EnumShares(job->config, shares);

	BMessage msg(kMsgSharesReady);
	msg.AddInt32("status", static_cast<int32>(s));
	msg.AddString("user", job->config.user.c_str());
	msg.AddString("domain", job->config.domain.c_str());
	if (s == BackendStatus::kOk) {
		for (const std::string& sh : shares)
			msg.AddString("share", sh.c_str());
	} else {
		msg.AddString("error", backend.Error() != nullptr ? backend.Error() : B_TRANSLATE("errore"));
	}
	job->reply.SendMessage(&msg); // copies the message; safe once backend is destroyed
	delete job;
	return 0;
}
} // namespace

void ConnectWindow::DoEnumShares()
{
	if (std::string(fServer->Text()).empty()) {
		SetStatus(B_TRANSLATE("Inserisci il server."));
		return;
	}
	if (std::string(fUser->Text()).empty()) {
		// Modern Windows refuses anonymous enumeration, so a login is required.
		SetStatus(B_TRANSLATE("Inserisci utente e password."));
		return;
	}
	EnumJob* job = new EnumJob;
	job->config.server = fServer->Text();
	job->config.user = fUser->Text();
	job->config.password = fPassword->Text();
	job->config.domain = fDomain->Text();
	job->reply = BMessenger(this);

	thread_id t = spawn_thread(EnumThread, "smb_enum", B_NORMAL_PRIORITY, job);
	if (t < 0) {
		delete job;
		SetStatus(B_TRANSLATE("Impossibile avviare la ricerca."));
		return;
	}
	SetStatus(B_TRANSLATE("Ricerca condivisioni..."));
	SetBusy(true);
	resume_thread(t);
}

// Show the enumerated shares (UI thread): one share fills the field directly; several open a pop-up
// menu anchored to the button.
void ConnectWindow::ShowSharePicker(const std::vector<std::string>& shares)
{
	if (shares.empty()) {
		SetStatus(B_TRANSLATE("Nessuna condivisione visibile su questo server."));
		return;
	}
	if (shares.size() == 1) {
		fShare->SetText(shares.front().c_str());
		SetStatus((std::string(B_TRANSLATE("Condivisione scelta: ")) + shares.front()).c_str());
		return;
	}
	BPopUpMenu* menu = new BPopUpMenu("shares", false, false);
	for (const std::string& name : shares)
		menu->AddItem(new BMenuItem(name.c_str(), nullptr));
	BPoint where = fEnumButton->ConvertToScreen(fEnumButton->Bounds().LeftBottom());
	BMenuItem* picked = menu->Go(where, false, true);
	if (picked != nullptr) {
		fShare->SetText(picked->Label());
		SetStatus((std::string(B_TRANSLATE("Condivisione scelta: ")) + picked->Label()).c_str());
	} else {
		SetStatus(B_TRANSLATE("Scegli una condivisione dall'elenco."));
	}
	delete menu;
}

// The connect + mount also runs OFF the UI thread: validating the login (SMB handshake) and
// fs_mount_volume are both blocking and would freeze the dialog. DoConnect captures the form into a
// ConnectJob and spawns this worker, which validates, remembers the login, mounts, and posts
// kMsgConnectDone back with the outcome; the looper opens Tracker / shows the message.
namespace {
struct ConnectJob {
	SmbConfig   config;     // server, share, user, password, domain, basePath
	std::string mountPoint;
	std::string diskName;
	std::string mountName;  // the raw "Nome disco" field (empty = auto), remembered per host
	bool        asDisk = false;
	bool        readOnly = false;
	bool        remember = false;
	BMessenger  reply;
};

int32 ConnectThread(void* arg)
{
	ConnectJob* job = static_cast<ConnectJob*>(arg);
	const SmbConfig& c = job->config;

	// 1. Validate the login (server-level via share enumeration, single-share via connect).
	BackendStatus s;
	std::string detail;
	bool noShares = false;
	{
		SmbBackend probe;
		if (c.share.empty()) {
			std::vector<std::string> shares;
			s = probe.EnumShares(c, shares);
			noShares = (s == BackendStatus::kOk && shares.empty());
		} else {
			s = probe.Connect(c);
			probe.Disconnect();
		}
		detail = probe.Error() != nullptr ? probe.Error() : B_TRANSLATE("errore sconosciuto");
	}
	if (s != BackendStatus::kOk || noShares) {
		BMessage msg(kMsgConnectDone);
		msg.AddBool("ok", false);
		msg.AddString("phase", noShares ? "noshares" : "validate");
		msg.AddInt32("status", static_cast<int32>(s));
		msg.AddString("detail", detail.c_str());
		msg.AddString("user", c.user.c_str());
		msg.AddString("domain", c.domain.c_str());
		msg.AddString("share", c.share.c_str());
		job->reply.SendMessage(&msg);
		delete job;
		return 0;
	}

	// 2. Remember the login now that it works (password only if opted in), plus this host's choices
	// so the next connect prefills everything.
	SaveRecent(c.user, c.domain);
	if (job->remember)
		SavePassword(c.server, c.user, c.password);
	else
		ForgetPassword(c.server, c.user);
	HostPrefs prefs;
	prefs.user = c.user;
	prefs.domain = c.domain;
	prefs.share = c.share;
	prefs.diskName = job->mountName;
	prefs.asDisk = job->asDisk;
	prefs.readOnly = job->readOnly;
	SaveHost(c.server, prefs);

	// 3. Mount (read-write unless the user asked for read-only).
	MakeDirs(job->mountPoint);
	std::string params = BuildSmbMountParameters(c);
	uint32 flags = job->readOnly ? B_MOUNT_READ_ONLY : 0;
	dev_t dev = fs_mount_volume(job->mountPoint.c_str(), nullptr, "userlandfs", flags,
		params.c_str());

	BMessage msg(kMsgConnectDone);
	if (dev < 0) {
		msg.AddBool("ok", false);
		msg.AddString("phase", "mount");
	} else {
		msg.AddBool("ok", true);
		msg.AddString("mountpoint", job->mountPoint.c_str());
		msg.AddString("diskname", job->diskName.c_str());
		msg.AddBool("asdisk", job->asDisk);
		msg.AddBool("shareempty", c.share.empty());
	}
	job->reply.SendMessage(&msg);
	delete job;
	return 0;
}
} // namespace

void ConnectWindow::DoConnect()
{
	if (std::string(fServer->Text()).empty()) {
		SetStatus(B_TRANSLATE("Inserisci il server."));
		return;
	}

	ConnectJob* job = new ConnectJob;
	job->config.server = fServer->Text();
	job->config.share = fShare->Text();
	job->config.user = fUser->Text();
	job->config.password = fPassword->Text();
	job->config.domain = fDomain->Text();
	job->config.basePath = fPath->Text();
	// Auto-mount at login needs the password stored, so ticking it implies "remember password".
	job->remember = fRemember->Value() == B_CONTROL_ON || fAutoMount->Value() == B_CONTROL_ON;

	// Where to mount (see ComputeMountPoint): ~/WON/<name> by default (the WON folder doubles as the
	// live neighborhood), or a top-level /<name> Desktop disk if "Monta come disco" is ticked.
	std::string diskName;
	job->mountPoint = ComputeMountPoint(diskName);
	job->diskName = diskName;
	job->asDisk = fAsDisk->Value() == B_CONTROL_ON || WonFolder().empty();
	job->readOnly = fReadOnly->Value() == B_CONTROL_ON;
	job->mountName = fMountPoint->Text(); // raw field (empty = auto), remembered per host
	job->reply = BMessenger(this);

	thread_id t = spawn_thread(ConnectThread, "smb_connect", B_NORMAL_PRIORITY, job);
	if (t < 0) {
		delete job;
		SetStatus(B_TRANSLATE("Impossibile avviare la connessione."));
		return;
	}
	SetStatus(B_TRANSLATE("Connessione e montaggio in corso..."));
	SetBusy(true);
	resume_thread(t);
}

// The mount path the current form maps to (same rule as DoConnect), so Unmount targets exactly what
// Connect would mount.
std::string ConnectWindow::ComputeMountPoint(std::string& diskName)
{
	std::string share = fShare->Text();
	std::string server = fServer->Text();
	diskName = SanitizeDiskName(fMountPoint->Text());
	if (diskName.empty())
		diskName = SanitizeDiskName(share.empty() ? server : share);
	if (diskName.empty())
		diskName = "smb";
	std::string won = WonFolder();
	bool asDisk = fAsDisk->Value() == B_CONTROL_ON || won.empty();
	return asDisk ? ("/" + diskName) : (won + "/" + diskName);
}

// Unmount this host's volume, behind an explicit KDL warning. Unmounting a userlandfs volume has
// kernel-panicked on Haiku (working agreement), so this is deliberately a confirmed, manual action.
void ConnectWindow::DoUnmount()
{
	std::string diskName;
	std::string mountPoint;
	if (fMountedMode) {
		mountPoint = fMountPath; // compact panel: the target is fixed at open time
		diskName = fMountName;
	} else {
		mountPoint = ComputeMountPoint(diskName);
	}

	// Only offer to unmount if something is really mounted there: a mount point's device id differs
	// from its parent directory's.
	std::string parent = mountPoint.substr(0, mountPoint.find_last_of('/'));
	if (parent.empty())
		parent = "/";
	dev_t here = dev_for_path(mountPoint.c_str());
	dev_t up = dev_for_path(parent.c_str());
	if (here < 0 || here == up) {
		SetStatus((std::string(B_TRANSLATE("Nessun volume montato in \"")) + mountPoint + "\".")
			.c_str());
		return;
	}

	BString msg(B_TRANSLATE("ATTENZIONE: smontare un volume di rete su Haiku puo' bloccare il "
		"sistema (KDL) e costringere a un riavvio forzato. Procedi solo se puoi permetterti un "
		"riavvio.\n\nSmontare \""));
	msg << diskName.c_str() << "\"?";
	BAlert* alert = new BAlert(B_TRANSLATE("Smonta"), msg.String(), B_TRANSLATE("Smonta"),
		B_TRANSLATE("Annulla"), nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE); // Esc / default = Annulla (the safe choice)
	if (alert->Go() != 0)            // 0 = Smonta, 1 = Annulla
		return;

	SetStatus(B_TRANSLATE("Smontaggio in corso..."));
	status_t s = fs_unmount_volume(mountPoint.c_str(), 0);
	if (s == B_OK) {
		::rmdir(mountPoint.c_str()); // best-effort: drop the now-empty mount directory
		SetStatus((std::string(B_TRANSLATE("Smontato \"")) + diskName + "\".").c_str());
	} else if (s == B_BUSY) {
		// The usual cause: a Tracker window is still open on the volume (or a file/terminal inside).
		SetStatus(B_TRANSLATE("Il volume e' in uso: chiudi le finestre di Tracker aperte su di esso "
			"(e i file/terminali dentro), poi riprova."));
	} else {
		SetStatus((std::string(B_TRANSLATE("Smontaggio fallito: ")) + strerror(s)).c_str());
	}
}

// The application: opens a connect window from the launch arguments (server=..., as the WON app
// passes them) or from a double-clicked share-shortcut file (RefsReceived reads its attributes).
class SmbMountApp : public BApplication {
public:
	explicit SmbMountApp(const SmbConfig& argvConfig)
		: BApplication(kSignature), fArgvConfig(argvConfig) {}

	void ReadyToRun() override
	{
		// If we were not launched to open a share file, show the window built from the arguments.
		if (!fOpened) {
			(new ConnectWindow(fArgvConfig))->Show();
			fOpened = true;
		}
	}

	void RefsReceived(BMessage* message) override
	{
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; ++i) {
			SmbConfig cfg;
			if (ReadShareFile(ref, cfg)) {
				(new ConnectWindow(cfg))->Show();
				fOpened = true;
			}
		}
	}

private:
	SmbConfig fArgvConfig;
	bool      fOpened = false;
};

int main(int argc, char** argv)
{
	// Headless auto-mount (run by the login hook): mount every remembered host with a saved
	// password, no window, then exit.
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--automount") == 0)
			return AutoMountAll() >= 0 ? 0 : 1;
	}

	// Prefill from launch arguments (server=.../share=..., as the WON app passes them); reuses the
	// mount parser, ignoring its ok flag since a partial prefill is fine here.
	SmbMount mount = ParseSmbMount(argc, argv);
	SmbMountApp app(mount.config);
	app.Run();
	return 0;
}
