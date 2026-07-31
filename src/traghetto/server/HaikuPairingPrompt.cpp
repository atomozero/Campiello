// HaikuPairingPrompt.cpp
//
// Implementation of the Haiku BAlert pairing prompt. See HaikuPairingPrompt.h.
//
// Verified against the Haiku headers and the BAlert API reference (docs/VERIFIED.md
// section 10): the BAlert constructor is
//   BAlert(title, text, button1, button2=NULL, button3=NULL, width, alert_type)
// (Alert.h:40-45); the synchronous int32 Go() (Alert.h:69) shows the alert modally, returns
// the 0-based index of the clicked button counting left to right (or -1 if the alert is asked
// to quit), and DELETES the BAlert before returning. So the object is never deleted or
// touched here after Go(). be_app is declared in Application.h:169; B_ESCAPE in
// InterfaceDefs.h:73.

#ifdef __HAIKU__

#include "HaikuPairingPrompt.h"

#include <string>

#include <Alert.h>
#include <Application.h>
#include <InterfaceDefs.h>

namespace campiello {
namespace net {

namespace {

// The peer name is attacker-controlled (docs/PROPOSAL.md section 9). Strip control bytes so a
// crafted name cannot inject extra lines into the dialog text, and cap the display length.
// UTF-8 continuation bytes are >= 0x80, so dropping bytes < 0x20 keeps multibyte glyphs whole.
std::string SanitizeForDisplay(const std::string& name)
{
	const size_t kMaxDisplay = 64;
	std::string out;
	out.reserve(name.size());
	for (unsigned char c : name) {
		out.push_back(c < 0x20 ? ' ' : (char)c);
		if (out.size() >= kMaxDisplay) {
			out += "...";
			break;
		}
	}
	if (out.empty())
		out = "?";
	return out;
}

} // namespace

bool HaikuPairingPrompt::Ask(const std::string& name, const Fingerprint& /*fp*/,
	TrustDecision decision)
{
	// BAlert is a BWindow and needs an app_server connection, i.e. a running BApplication. If
	// there is none we cannot ask the user, so fail safe by denying.
	if (be_app == nullptr)
		return false;

	std::string who = SanitizeForDisplay(name);
	std::string text;
	if (decision == TrustDecision::kKeyChanged) {
		// A known name presenting a different key: surface the impersonation risk plainly.
		text = "\"" + who + "\" si presenta con una chiave diversa dall'ultima volta.\n"
			"Potrebbe non essere lo stesso dispositivo. Consentire comunque la connessione?";
	} else {
		text = "\"" + who + "\" vuole connettersi a questo computer.\nConsentire?";
	}

	// Buttons left to right: index 0 "Nega", index 1 "Consenti". Escape maps to Nega, so an
	// accidental dismissal denies rather than allows.
	BAlert* alert = new BAlert("Campiello", text.c_str(), "Nega", "Consenti", nullptr,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);

	int32 choice = alert->Go(); // modal; deletes the BAlert before returning
	return choice == 1;          // only an explicit "Consenti" allows; -1 (quit) denies
}

} // namespace net
} // namespace campiello

#endif // __HAIKU__
