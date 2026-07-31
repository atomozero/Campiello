# Build and run the trust-layer tests: the fingerprint / trust store / paths, and the
# pairing policy. Pure standard C++17, no OpenSSL, no Haiku dependency; CI-portable.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

TLS_DIR := ../../src/traghetto/tls
TRUST_DIR := ../../src/traghetto/trust

TESTS := test_truststore test_pairing

test_truststore: test_truststore.cpp \
        $(TRUST_DIR)/TrustStore.cpp \
        $(TRUST_DIR)/Paths.cpp \
        $(TLS_DIR)/Fingerprint.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

test_pairing: test_pairing.cpp \
        $(TRUST_DIR)/Pairing.cpp \
        $(TRUST_DIR)/TokenBucket.cpp \
        $(TRUST_DIR)/TrustStore.cpp \
        $(TLS_DIR)/Fingerprint.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: check clean
check: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

clean:
	rm -f $(TESTS) truststore_test.tmp pairing_test.tmp
