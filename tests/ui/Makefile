# Compile-and-link check for the Haiku BAlert pairing prompt. Haiku-only: it needs libbe. The
# source is __HAIKU__-guarded, so on other systems this target is a no-op. The check does NOT
# pop the dialog (Ask() needs a BApplication); it only proves the BAlert API usage builds.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

SRV_DIR := ../../src/traghetto/server

.PHONY: check clean

ifeq ($(shell uname),Haiku)

test_haikuprompt: test_haikuprompt.cpp $(SRV_DIR)/HaikuPairingPrompt.cpp \
                  $(SRV_DIR)/HaikuPairingPrompt.h
	$(CXX) $(CXXFLAGS) -o $@ test_haikuprompt.cpp $(SRV_DIR)/HaikuPairingPrompt.cpp -lbe

check: test_haikuprompt
	./test_haikuprompt

else

check:
	@echo "tests/ui: skipped (not Haiku)"

endif

clean:
	rm -f test_haikuprompt
