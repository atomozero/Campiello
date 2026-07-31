# Top-level build for Campiello: one command compiles and tests the whole project.
#
#   make            # same as `make check`
#   make check      # build and run every unit-test suite
#   make packages   # build the core and optional .hpkg packages
#   make all        # check + packages
#   make apps       # build the Haiku GUI apps (radar, vicinato) for quick dev
#   make clean      # clean every subdirectory
#
# Each subdirectory keeps its own Makefile; this just drives them in one place. Haiku-only suites
# (GUI / FUSE) build their Haiku pieces when run on Haiku; the portable suites also run on Linux
# CI. `check` runs every suite and reports which, if any, failed, exiting non-zero on failure.

# Portable + Haiku unit-test suites (each has a `check` target). Ordered fast-to-slow-ish.
TEST_DIRS := tests/wire tests/tls tests/transport tests/dispatch tests/trust \
             tests/discovery tests/fondamenta tests/server tests/fuse tests/ui \
             tests/replicant tests/bricola optional/smb

# Package builds (each has a `package` target).
PKG_DIRS := packaging packaging/smb

.PHONY: all check packages apps clean

all: check packages

check:
	@fail=""; \
	for d in $(TEST_DIRS); do \
		echo "======== check $$d ========"; \
		$(MAKE) --no-print-directory -C $$d check || fail="$$fail $$d"; \
	done; \
	if [ -n "$$fail" ]; then echo; echo "FAILED SUITES:$$fail"; exit 1; fi; \
	echo; echo "All test suites passed."

packages:
	@for d in $(PKG_DIRS); do \
		echo "======== package $$d ========"; \
		$(MAKE) --no-print-directory -C $$d package || exit 1; \
	done

# The Haiku GUI apps, for quick iteration (not part of `check`; the portable cores they use are
# tested there).
apps:
	$(MAKE) --no-print-directory -C tests/bricola radar
	$(MAKE) --no-print-directory -C tests/bricola vicinato

clean:
	@for d in $(TEST_DIRS) $(PKG_DIRS); do \
		$(MAKE) --no-print-directory -C $$d clean 2>/dev/null || true; \
	done
	@echo "Cleaned."
