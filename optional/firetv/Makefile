# Build the OPTIONAL Campiello Fire TV remote (Haiku-only).
#
# A device add-on (docs/DEVICE_ADDONS.md): an on-screen remote for an Amazon Fire TV over its
# official REST API. Links OpenSSL (Apache-2.0) + libbe + the network kit; the MIT core never
# depends on it. Launched with the device from the WON neighborhood: host=<ip> [name=<label>].
#
#   make            # builds campiello_firetv
#   make check      # builds it (no unit test; the client is validated against a real device)

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDLIBS ?= -lbe -lssl -lcrypto -lnetwork

APP := campiello_firetv
SRC := campiello_firetv.cpp FireTVRemote.cpp

.PHONY: all check clean
all: $(APP)

$(APP): $(SRC) FireTVRemote.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDLIBS)

check: $(APP)
	@echo "built $(APP) (no offline unit test; FireTVRemote is validated live)"

clean:
	rm -f $(APP)
