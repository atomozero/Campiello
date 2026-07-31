// campiello_peers.cpp
//
// A demo of Bricola discovery: a window that lists the Campiello devices found on the LAN,
// live, appearing and disappearing by themselves (the BeOS "network neighborhood" feel). It is
// a test/preview surface; the real target is presenting the same peers as a folder in Tracker
// (a discovery filesystem), but this proves the discovery + live-update path visually first.
//
// It owns a browse-only Bricola and receives its worker-thread PeerObserver callbacks as
// BMessages via a BMessenger (the "worker never touches a BView" idiom, same as the replicant).
// The window mutates its list only in MessageReceived.
//
// Interface selection follows Bricola's default (the primary LAN interface, or the
// CAMPIELLO_MDNS_IFACE override, e.g. 127.0.0.1 to watch local advertisers such as
// discover_demo). Haiku-only. End-user strings are Italian.

#include <Application.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Message.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>
#include <Window.h>

#include <map>
#include <string>

#include "../mdns/Bricola.h"
#include "../mdns/Peer.h"

using namespace campiello::bricola::mdns;

static const char* const kSignature = "application/x-vnd.Campiello-peers";

static const uint32 kMsgPeerFound   = 'pFnd';
static const uint32 kMsgPeerUpdated = 'pUpd';
static const uint32 kMsgPeerLost    = 'pLst';

// Forwards Bricola's worker-thread callbacks to the window as BMessages.
class MessengerObserver : public PeerObserver {
public:
	void SetTarget(const BMessenger& target) { fTarget = target; }

	void PeerFound(const Peer& p) override { Post(kMsgPeerFound, p); }
	void PeerUpdated(const Peer& p) override { Post(kMsgPeerUpdated, p); }
	void PeerLost(const Peer& p) override { Post(kMsgPeerLost, p); }

private:
	void Post(uint32 what, const Peer& p)
	{
		if (!fTarget.IsValid())
			return;
		BMessage msg(what);
		msg.AddString("key", p.key.c_str());
		msg.AddString("instance", p.instance.c_str());
		msg.AddString("host", p.hostname.c_str());
		msg.AddInt32("port", static_cast<int32>(p.port));
		msg.AddString("addr", p.addresses.empty() ? "" : p.addresses.front().c_str());
		msg.AddBool("bfs", p.bfsAttrs);
		fTarget.SendMessage(&msg);
	}

	BMessenger fTarget;
};

struct PeerRow {
	BString instance;
	BString host;
	BString addr;
	int32   port = 0;
	bool    bfs = false;
};

class PeerListWindow : public BWindow {
public:
	PeerListWindow();
	void MessageReceived(BMessage* message) override;
	bool QuitRequested() override;

private:
	void Rebuild();

	BListView*   fList;
	BStringView* fStatus;

	Bricola           fBricola;
	MessengerObserver fObserver;
	std::map<std::string, PeerRow> fPeers; // key: instance FQDN
};

PeerListWindow::PeerListWindow()
	:
	BWindow(BRect(120, 120, 520, 460), "Campiello: dispositivi in rete",
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fList = new BListView("peers", B_SINGLE_SELECTION_LIST);
	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true);
	fStatus = new BStringView("status", "In ascolto sulla rete...");

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(scroll)
		.Add(fStatus);

	// Start browsing and route discovery events back to this window (it now has a looper).
	fObserver.SetTarget(BMessenger(this));
	fBricola.StartBrowsing(&fObserver);
}

bool PeerListWindow::QuitRequested()
{
	fBricola.Stop(); // join the worker before the observer/messenger die
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}

void PeerListWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPeerFound:
		case kMsgPeerUpdated:
		{
			const char* key = nullptr;
			if (message->FindString("key", &key) != B_OK || key == nullptr)
				return;
			PeerRow row;
			const char* s = nullptr;
			if (message->FindString("instance", &s) == B_OK) row.instance = s;
			if (message->FindString("host", &s) == B_OK) row.host = s;
			if (message->FindString("addr", &s) == B_OK) row.addr = s;
			message->FindInt32("port", &row.port);
			message->FindBool("bfs", &row.bfs);
			fPeers[key] = row;
			Rebuild();
			return;
		}
		case kMsgPeerLost:
		{
			const char* key = nullptr;
			if (message->FindString("key", &key) == B_OK && key != nullptr)
				fPeers.erase(key);
			Rebuild();
			return;
		}
	}
	BWindow::MessageReceived(message);
}

void PeerListWindow::Rebuild()
{
	fList->MakeEmpty(); // small lists; rebuild wholesale on each change
	for (const auto& kv : fPeers) {
		const PeerRow& r = kv.second;
		BString label;
		label << r.instance << "   " << r.host;
		if (r.port != 0)
			label << ":" << r.port;
		if (r.bfs)
			label << "   [BFS]";
		fList->AddItem(new BStringItem(label.String()));
	}

	BString status;
	if (fPeers.empty())
		status = "Nessun dispositivo. In ascolto sulla rete...";
	else
		status.SetToFormat("%d dispositivo/i in rete", static_cast<int>(fPeers.size()));
	fStatus->SetText(status.String());
}

int main()
{
	BApplication app(kSignature);
	PeerListWindow* window = new PeerListWindow();
	window->Show();
	app.Run();
	return 0;
}
