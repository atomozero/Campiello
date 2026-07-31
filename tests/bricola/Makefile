# Build and run the Bricola mDNS wire tests.
# Pure standard C++17, no Haiku dependency, so this runs in CI on any platform.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2 -pthread

# MdnsWire is pure std; MdnsSocket uses POSIX sockets, which live in libnetwork on Haiku and
# in libc on Linux.
LDLIBS ?=
ifeq ($(shell uname),Haiku)
LDLIBS += -lnetwork
endif

MDNS_DIR := ../../src/bricola/mdns
VIC_DIR := ../../src/vicinato

TESTS := test_mdnswire test_mdnssocket test_browser test_responder test_bricola test_radar \
         test_radarlabels test_neighborhood test_smbfinder test_handler test_netintel

test_mdnswire: test_mdnswire.cpp $(MDNS_DIR)/MdnsWire.cpp $(MDNS_DIR)/MdnsWire.h
	$(CXX) $(CXXFLAGS) -o $@ test_mdnswire.cpp $(MDNS_DIR)/MdnsWire.cpp

test_browser: test_browser.cpp $(MDNS_DIR)/Browser.cpp $(MDNS_DIR)/PeerTable.cpp \
              $(MDNS_DIR)/MdnsWire.cpp $(MDNS_DIR)/Browser.h $(MDNS_DIR)/PeerTable.h \
              $(MDNS_DIR)/Peer.h $(MDNS_DIR)/MdnsWire.h
	$(CXX) $(CXXFLAGS) -o $@ test_browser.cpp $(MDNS_DIR)/Browser.cpp \
	    $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsWire.cpp

test_responder: test_responder.cpp $(MDNS_DIR)/Responder.cpp $(MDNS_DIR)/Browser.cpp \
                $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsWire.cpp $(MDNS_DIR)/Responder.h \
                $(MDNS_DIR)/Browser.h $(MDNS_DIR)/PeerTable.h $(MDNS_DIR)/Peer.h \
                $(MDNS_DIR)/MdnsWire.h
	$(CXX) $(CXXFLAGS) -o $@ test_responder.cpp $(MDNS_DIR)/Responder.cpp \
	    $(MDNS_DIR)/Browser.cpp $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsWire.cpp

test_bricola: test_bricola.cpp $(MDNS_DIR)/Bricola.cpp $(MDNS_DIR)/Responder.cpp \
              $(MDNS_DIR)/Browser.cpp $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsSocket.cpp \
              $(MDNS_DIR)/MdnsWire.cpp $(MDNS_DIR)/Bricola.h $(MDNS_DIR)/Responder.h \
              $(MDNS_DIR)/Browser.h $(MDNS_DIR)/PeerTable.h $(MDNS_DIR)/MdnsSocket.h \
              $(MDNS_DIR)/Peer.h $(MDNS_DIR)/MdnsWire.h
	$(CXX) $(CXXFLAGS) -o $@ test_bricola.cpp $(MDNS_DIR)/Bricola.cpp \
	    $(MDNS_DIR)/Responder.cpp $(MDNS_DIR)/Browser.cpp $(MDNS_DIR)/PeerTable.cpp \
	    $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp $(LDLIBS)

# Hands-on discovery demo (not part of `check`): run it to watch two local nodes discover each
# other on the loopback interface, no second machine needed. See discover_demo.cpp.
discover_demo: discover_demo.cpp $(MDNS_DIR)/Bricola.cpp $(MDNS_DIR)/Responder.cpp \
               $(MDNS_DIR)/Browser.cpp $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsSocket.cpp \
               $(MDNS_DIR)/MdnsWire.cpp
	$(CXX) $(CXXFLAGS) -o $@ discover_demo.cpp $(MDNS_DIR)/Bricola.cpp \
	    $(MDNS_DIR)/Responder.cpp $(MDNS_DIR)/Browser.cpp $(MDNS_DIR)/PeerTable.cpp \
	    $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp $(LDLIBS)

test_mdnssocket: test_mdnssocket.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsSocket.h \
                 $(MDNS_DIR)/MdnsWire.cpp $(MDNS_DIR)/MdnsWire.h
	$(CXX) $(CXXFLAGS) -o $@ test_mdnssocket.cpp $(MDNS_DIR)/MdnsSocket.cpp \
	    $(MDNS_DIR)/MdnsWire.cpp $(LDLIBS)

# The network-neighborhood demo GUI (Haiku-only): a window listing discovered peers live.
# Run it (optionally with CAMPIELLO_MDNS_IFACE=127.0.0.1 alongside discover_demo) to watch
# devices appear and leave. Built via `make peers-demo`; linked, not part of `check`.
DEMO_DIR := ../../src/bricola/demo
peers-demo: $(DEMO_DIR)/campiello_peers.cpp $(MDNS_DIR)/Bricola.cpp $(MDNS_DIR)/Responder.cpp \
            $(MDNS_DIR)/Browser.cpp $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsSocket.cpp \
            $(MDNS_DIR)/MdnsWire.cpp
	$(CXX) $(CXXFLAGS) -o campiello_peers $(DEMO_DIR)/campiello_peers.cpp \
	    $(MDNS_DIR)/Bricola.cpp $(MDNS_DIR)/Responder.cpp $(MDNS_DIR)/Browser.cpp \
	    $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp \
	    -lbe $(LDLIBS)

# The MdnsRadar aggregator test: pure std, feeds hand-built packets, no socket. Links
# MdnsSocket.cpp only for the symbols MdnsRadar references (it never opens a socket here).
test_radar: test_radar.cpp $(MDNS_DIR)/MdnsRadar.cpp $(MDNS_DIR)/MdnsRadar.h \
            $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp $(MDNS_DIR)/MdnsSocket.h \
            $(MDNS_DIR)/MdnsWire.h
	$(CXX) $(CXXFLAGS) -o $@ test_radar.cpp $(MDNS_DIR)/MdnsRadar.cpp $(MDNS_DIR)/MdnsSocket.cpp \
	    $(MDNS_DIR)/MdnsWire.cpp $(LDLIBS)

# The radar label/TXT decoding and report writer: pure std, unit-testable off Haiku.
test_radarlabels: test_radarlabels.cpp $(MDNS_DIR)/RadarLabels.cpp $(MDNS_DIR)/RadarLabels.h \
                  $(MDNS_DIR)/MdnsRadar.h
	$(CXX) $(CXXFLAGS) -o $@ test_radarlabels.cpp $(MDNS_DIR)/RadarLabels.cpp

# The neighborhood model (service classification): pure std, unit-testable off Haiku.
test_neighborhood: test_neighborhood.cpp $(VIC_DIR)/NetworkDirectory.cpp $(MDNS_DIR)/RadarLabels.cpp \
                   $(VIC_DIR)/NetworkDirectory.h $(MDNS_DIR)/RadarLabels.h $(MDNS_DIR)/MdnsRadar.h
	$(CXX) $(CXXFLAGS) -o $@ test_neighborhood.cpp $(VIC_DIR)/NetworkDirectory.cpp \
	    $(MDNS_DIR)/RadarLabels.cpp

# The SMB host finder's pure helpers (subnet, host->service, merge). Links MdnsSocket for the
# local-address lookup the finder uses; the live scan is not exercised in the test.
test_smbfinder: test_smbfinder.cpp $(VIC_DIR)/SmbHostFinder.cpp $(VIC_DIR)/NetworkDirectory.cpp \
                $(MDNS_DIR)/RadarLabels.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp \
                $(VIC_DIR)/SmbHostFinder.h
	$(CXX) $(CXXFLAGS) -o $@ test_smbfinder.cpp $(VIC_DIR)/SmbHostFinder.cpp \
	    $(VIC_DIR)/NetworkDirectory.cpp $(MDNS_DIR)/RadarLabels.cpp $(MDNS_DIR)/MdnsSocket.cpp \
	    $(MDNS_DIR)/MdnsWire.cpp $(LDLIBS)

# The device add-on framework: manifest parsing + matching + the launch protocol (pure std).
test_handler: test_handler.cpp $(VIC_DIR)/HandlerRegistry.cpp $(VIC_DIR)/DeviceLaunch.cpp \
              $(VIC_DIR)/HandlerRegistry.h $(VIC_DIR)/DeviceLaunch.h
	$(CXX) $(CXXFLAGS) -o $@ test_handler.cpp $(VIC_DIR)/HandlerRegistry.cpp \
	    $(VIC_DIR)/DeviceLaunch.cpp $(LDLIBS)

# The LAN-intel module (OUI/ARP, Wake-on-LAN, NetBIOS, SSDP) ported from LANterna: the test drives
# only the pure encode/parse helpers, but links the whole .cpp (POSIX sockets -> libnetwork).
test_netintel: test_netintel.cpp $(VIC_DIR)/NetIntel.cpp $(VIC_DIR)/NetIntel.h
	$(CXX) $(CXXFLAGS) -o $@ test_netintel.cpp $(VIC_DIR)/NetIntel.cpp $(LDLIBS)

# The network radar GUI (Haiku-only): a window showing all mDNS hosts and services live, with an
# interface picker, decoded TXT, and log export. Built via `make radar`; not part of `check`.
# See docs/RADAR.md.
radar: $(VIC_DIR)/campiello_radar.cpp $(MDNS_DIR)/MdnsRadar.cpp $(MDNS_DIR)/RadarLabels.cpp \
       $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp
	$(CXX) $(CXXFLAGS) -o campiello_radar $(VIC_DIR)/campiello_radar.cpp $(MDNS_DIR)/MdnsRadar.cpp \
	    $(MDNS_DIR)/RadarLabels.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp -lbe $(LDLIBS)

# The Vicinato network-neighborhood companion app (Haiku-only): lists discovered services and
# opens the login helper or an info card on double-click. Built via `make vicinato`; see
# docs/NEIGHBORHOOD.md.
vicinato: $(VIC_DIR)/campiello_vicinato.cpp $(VIC_DIR)/NetworkDirectory.cpp \
          $(VIC_DIR)/SmbHostFinder.cpp $(VIC_DIR)/ShareFolder.cpp $(VIC_DIR)/NetIntel.cpp \
          $(VIC_DIR)/HandlerRegistry.cpp $(VIC_DIR)/OldSchoolDemo.cpp $(MDNS_DIR)/MdnsRadar.cpp \
          $(MDNS_DIR)/RadarLabels.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp
	$(CXX) $(CXXFLAGS) -o campiello_won $(VIC_DIR)/campiello_vicinato.cpp \
	    $(VIC_DIR)/NetworkDirectory.cpp $(VIC_DIR)/SmbHostFinder.cpp $(VIC_DIR)/ShareFolder.cpp \
	    $(VIC_DIR)/NetIntel.cpp $(VIC_DIR)/HandlerRegistry.cpp $(VIC_DIR)/OldSchoolDemo.cpp \
	    $(MDNS_DIR)/MdnsRadar.cpp $(MDNS_DIR)/RadarLabels.cpp $(MDNS_DIR)/MdnsSocket.cpp \
	    $(MDNS_DIR)/MdnsWire.cpp -lbe -lmedia $(LDLIBS)

.PHONY: check clean
check: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

clean:
	rm -f $(TESTS) discover_demo campiello_peers campiello_radar campiello_won
