// PeerReplicant.cpp
//
// The Campiello Deskbar replicant: a small tray view that shows the peers Bricola discovers on
// the LAN, appearing and disappearing live. This is the visible half of "the other machine
// shows up by itself" (PROPOSAL.md milestone M2).
//
// Structure lifted from the proven LocalSend replicant (docs/REUSE.md): Instantiate/Archive
// with the add_on=own-signature reload trick, dual export entry points, an HVIF icon from the
// MIME database, install via BDeskbar::AddItem. Verified against the Haiku headers
// (docs/VERIFIED.md section 10): BView archive contract (Dragger.h), BDeskbar::AddItem
// (Deskbar.h), replicant validate_instantiation.
//
// Data source: the replicant owns a browse-only Bricola (it does not advertise; the resident
// daemon is the node that advertises, so there is no second responder for this machine). Peer
// events arrive on Bricola's worker thread and are forwarded to this view via a BMessenger, so
// the worker never touches a BView (the "worker never touches UI" idiom, docs/REUSE.md). The
// view mutates its peer list only in MessageReceived, on the Deskbar's looper.
//
// Haiku-only (libbe). Build-verified by linking the add-on; not run here (a replicant is
// exercised in a throwaway VM per the working agreement).

#include <Bitmap.h>
#include <Deskbar.h>
#include <IconUtils.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <MimeType.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <map>
#include <string>

#include "../mdns/Bricola.h"
#include "../mdns/Peer.h"

// The resident daemon's signature, used to borrow its icon from the MIME database.
static const char* const kAppSignature = "application/x-vnd.Campiello-daemon";
// This add-on's own signature; the Deskbar stores it in "add_on" to reload us after a reboot.
static const char* const kReplicantSignature = "application/x-vnd.Campiello-replicant";

static const char* const kItemName = "CampielloPeers";
static const char* const kClassName = "campiello::PeerReplicantView";

// Peer events forwarded from the Bricola worker thread to the view's looper.
static const uint32 kMsgPeerFound   = 'cpPf';
static const uint32 kMsgPeerUpdated = 'cpPu';
static const uint32 kMsgPeerLost    = 'cpPl';
static const uint32 kMsgOpenWon     = 'cpOw'; // menu -> open the WON neighborhood app
static const char* const kWonSignature = "application/x-vnd.Campiello-won";

namespace campiello {

// A discovered peer, reduced to what the tray needs to show and (later) connect.
struct PeerRow {
	BString instance;
	BString host;
	BString addr;
	int32   port = 0;
};

// Forwards Bricola's worker-thread callbacks to the view as BMessages. Holds only a
// BMessenger, so it is safe to call from the worker while the view lives on its looper.
class MessengerObserver : public bricola::mdns::PeerObserver {
public:
	void SetTarget(const BMessenger& target) { fTarget = target; }

	void PeerFound(const bricola::mdns::Peer& p) override { Post(kMsgPeerFound, p); }
	void PeerUpdated(const bricola::mdns::Peer& p) override { Post(kMsgPeerUpdated, p); }
	void PeerLost(const bricola::mdns::Peer& p) override { Post(kMsgPeerLost, p); }

private:
	void Post(uint32 what, const bricola::mdns::Peer& p)
	{
		if (!fTarget.IsValid())
			return;
		BMessage msg(what);
		msg.AddString("key", p.key.c_str());
		msg.AddString("instance", p.instance.c_str());
		msg.AddString("host", p.hostname.c_str());
		msg.AddInt32("port", static_cast<int32>(p.port));
		if (!p.addresses.empty())
			msg.AddString("addr", p.addresses.front().c_str());
		fTarget.SendMessage(&msg);
	}

	BMessenger fTarget;
};

class PeerReplicantView : public BView {
public:
	PeerReplicantView(BRect frame);
	PeerReplicantView(BMessage* archive);
	virtual ~PeerReplicantView();

	static PeerReplicantView* Instantiate(BMessage* archive);
	virtual status_t Archive(BMessage* into, bool deep = true) const;

	virtual void AttachedToWindow();
	virtual void DetachedFromWindow();
	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);
	virtual void MessageReceived(BMessage* message);
	virtual void GetPreferredSize(float* width, float* height);

private:
	void LoadIcon();
	void UpdateToolTip();

	BBitmap*                     fIcon;
	bricola::mdns::Bricola       fBricola;
	MessengerObserver            fObserver;
	std::map<std::string, PeerRow> fPeers;   // key: instance FQDN
};

PeerReplicantView::PeerReplicantView(BRect frame)
	:
	BView(frame, kItemName, B_FOLLOW_NONE, B_WILL_DRAW | B_TRANSPARENT_BACKGROUND),
	fIcon(nullptr)
{
}

PeerReplicantView::PeerReplicantView(BMessage* archive)
	:
	BView(archive),
	fIcon(nullptr)
{
}

PeerReplicantView::~PeerReplicantView()
{
	fBricola.Stop();   // join the worker before the observer/messenger die
	delete fIcon;
}

PeerReplicantView*
PeerReplicantView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kClassName))
		return nullptr;
	return new PeerReplicantView(archive);
}

status_t
PeerReplicantView::Archive(BMessage* into, bool deep) const
{
	status_t err = BView::Archive(into, deep);
	if (err != B_OK)
		return err;
	into->AddString("add_on", kReplicantSignature);
	into->AddString("class", kClassName);
	return B_OK;
}

void
PeerReplicantView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (Parent())
		SetViewColor(Parent()->ViewColor());
	else
		SetViewColor(B_TRANSPARENT_COLOR);
	LoadIcon();

	// Start browsing and route peer events back to this view (now that it has a looper).
	fObserver.SetTarget(BMessenger(this));
	fBricola.StartBrowsing(&fObserver);
	UpdateToolTip();
}

void
PeerReplicantView::DetachedFromWindow()
{
	fBricola.Stop();
	BView::DetachedFromWindow();
}

void
PeerReplicantView::LoadIcon()
{
	delete fIcon;
	fIcon = nullptr;

	BMimeType type(kAppSignature);
	uint8* data = nullptr;
	size_t size = 0;
	if (type.GetIcon(&data, &size) != B_OK || data == nullptr)
		return;   // no installed icon yet: Draw falls back to a simple glyph

	BBitmap* bm = new BBitmap(Bounds(), B_RGBA32);
	if (BIconUtils::GetVectorIcon(data, size, bm) != B_OK) {
		delete bm;
		free(data);
		return;
	}
	free(data);
	fIcon = bm;
}

void
PeerReplicantView::Draw(BRect)
{
	BRect bounds = Bounds();
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	if (fIcon != nullptr) {
		DrawBitmap(fIcon, bounds.LeftTop());
	} else {
		// Fallback glyph: a rounded square with a "C", so the item is visible before the app
		// icon is installed in the MIME database.
		SetHighColor(90, 130, 200, 255);
		FillRoundRect(bounds, 3, 3);
		SetHighColor(255, 255, 255, 255);
		SetFont(be_bold_font);
		const char* c = "C";
		font_height fh;
		GetFontHeight(&fh);
		float x = bounds.left + (bounds.Width() - StringWidth(c)) / 2.0f;
		float y = bounds.top + (bounds.Height() + fh.ascent - fh.descent) / 2.0f;
		MovePenTo(x, y);
		DrawString(c);
	}

	// A small dot in the corner when at least one peer is present.
	if (!fPeers.empty()) {
		SetHighColor(80, 200, 110, 255);
		BRect dot(bounds.right - 5, bounds.top, bounds.right, bounds.top + 5);
		FillEllipse(dot);
	}
}

void
PeerReplicantView::MouseDown(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu("campiello-peers", false, false);
	menu->SetAsyncAutoDestruct(true);

	// Always offer to open the WON neighborhood window (the full list of network services; the
	// peers below are only other Campiello nodes).
	menu->AddItem(new BMenuItem("Apri WON (vicinato di rete)", new BMessage(kMsgOpenWon)));
	menu->AddSeparatorItem();

	if (fPeers.empty()) {
		// User-facing string: Italian (working agreement rule 4).
		BMenuItem* none = new BMenuItem("Nessun peer trovato", nullptr);
		none->SetEnabled(false);
		menu->AddItem(none);
	} else {
		for (const auto& kv : fPeers) {
			const PeerRow& row = kv.second;
			BString label = row.instance;
			if (row.host.Length() > 0)
				label << " (" << row.host << ")";
			// No action wired yet: opening a peer in Tracker lands with the mount path (M2+).
			BMenuItem* item = new BMenuItem(label.String(), nullptr);
			item->SetEnabled(false);
			menu->AddItem(item);
		}
	}

	menu->SetTargetForItems(this);
	BPoint screenPoint = ConvertToScreen(where);
	menu->Go(screenPoint, true, true, true);
}

void
PeerReplicantView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOpenWon:
			be_roster->Launch(kWonSignature);
			return;
		case kMsgPeerFound:
		case kMsgPeerUpdated:
		{
			const char* key = nullptr;
			if (message->FindString("key", &key) != B_OK || key == nullptr)
				return;
			PeerRow row;
			const char* s = nullptr;
			if (message->FindString("instance", &s) == B_OK)
				row.instance = s;
			if (message->FindString("host", &s) == B_OK)
				row.host = s;
			if (message->FindString("addr", &s) == B_OK)
				row.addr = s;
			message->FindInt32("port", &row.port);
			fPeers[key] = row;
			UpdateToolTip();
			Invalidate();
			return;
		}
		case kMsgPeerLost:
		{
			const char* key = nullptr;
			if (message->FindString("key", &key) == B_OK && key != nullptr)
				fPeers.erase(key);
			UpdateToolTip();
			Invalidate();
			return;
		}
	}
	BView::MessageReceived(message);
}

void
PeerReplicantView::UpdateToolTip()
{
	// User-facing string: Italian (working agreement rule 4).
	BString tip;
	if (fPeers.empty()) {
		tip = "Campiello: nessun peer";
	} else {
		tip.SetToFormat("Campiello: %d peer", static_cast<int>(fPeers.size()));
	}
	SetToolTip(tip.String());
}

void
PeerReplicantView::GetPreferredSize(float* width, float* height)
{
	BRect b = Bounds();
	*width = b.Width();
	*height = b.Height();
}

} // namespace campiello

static BView*
MakeView(float maxWidth, float maxHeight)
{
	float side = maxHeight;
	if (side < 1)
		side = 16;
	if (maxWidth > 0 && maxWidth < side)
		side = maxWidth;
	return new campiello::PeerReplicantView(BRect(0, 0, side - 1, side - 1));
}

// Entry point preferred by the modern Deskbar (passes the tray height).
extern "C" _EXPORT BView*
instantiate_deskbar_entry(image_id, const entry_ref*, float maxWidth, float maxHeight)
{
	return MakeView(maxWidth, maxHeight);
}

// Legacy entry point for older Deskbars.
extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	return MakeView(maxWidth, maxHeight);
}
