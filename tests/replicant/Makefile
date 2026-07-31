# Build-verify the Campiello Deskbar replicant add-on.
#
# The replicant is a Haiku BView add-on (needs libbe) and drives a browse-only Bricola. We
# link it as a shared add-on to prove the whole thing compiles and resolves, but never load or
# install it here: a replicant is exercised in a throwaway VM per the working agreement. On
# non-Haiku hosts there is no libbe, so the build is skipped.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2 -pthread -fPIC

MDNS_DIR := ../../src/bricola/mdns
REPL_DIR := ../../src/bricola/replicant

# Discovery sources the replicant links (browse-only, so the responder is included but unused).
BRICOLA_SRCS := $(MDNS_DIR)/Bricola.cpp $(MDNS_DIR)/Responder.cpp $(MDNS_DIR)/Browser.cpp \
                $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp

.PHONY: check clean
ifeq ($(shell uname),Haiku)
check: campiello_replicant
	@echo "== campiello_replicant == linked (not loaded)"

campiello_replicant: $(REPL_DIR)/PeerReplicant.cpp $(BRICOLA_SRCS)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^ -lbe -lnetwork
else
check:
	@echo "== campiello_replicant == skipped (not Haiku)"
endif

clean:
	rm -f campiello_replicant
