# Build the campiello_complete all-in-one bundle: ONE .hpkg with the core application AND every
# device add-on, so a single install gives the full experience.
#
# It is assembled from the already-built sibling packages: it takes the newest core
# ../campiello-*-x86_64.hpkg plus the newest ../<name>/campiello_<name>-*-x86_64.hpkg for each device
# add-on, extracts each package's payload into a shared staging tree, and repackages the lot under one
# PackageInfo. Build the individual packages first (the repo ships them), then:
#
#   make package    # builds campiello_complete-<version>-x86_64.hpkg
#   pkgman install ./campiello_complete-0.4.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool). Excludes campiello_smb (a userlandfs volume with an LGPL
# dependency and its own install notes); the core already carries the device icons.

VERSION := 0.4.2-1
ARCH := x86_64
PKG := campiello_complete-$(VERSION)-$(ARCH).hpkg
STAGE := stage

# Every device add-on that ships an apps/ handler + a data/campiello/handlers/ manifest.
ADDONS := afp airplay alexa cast daap daikin eero escl esphome firetv ftp homekit hue ipp \
          lutron matter nfs nut spotify vnc webdav

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_complete.PackageInfo
	rm -rf $(STAGE)
	mkdir -p $(STAGE)
	@core=`ls ../campiello-*-$(ARCH).hpkg 2>/dev/null | sort -V | tail -1`; \
	if [ -z "$$core" ]; then echo "MISSING core package"; exit 1; fi; \
	echo "  + $$core"; \
	package extract -C $(STAGE) "$$core" >/dev/null 2>&1
	@for d in $(ADDONS); do \
		f=`ls ../$$d/campiello_$$d-*-$(ARCH).hpkg 2>/dev/null | sort -V | tail -1`; \
		if [ -z "$$f" ]; then echo "MISSING package for add-on: $$d"; exit 1; fi; \
		echo "  + $$f"; \
		package extract -C $(STAGE) "$$f" >/dev/null 2>&1; \
	done
	rm -f $(STAGE)/.PackageInfo
	package create -C $(STAGE) -i campiello_complete.PackageInfo $(PKG)
	@echo "Built $(PKG)"

clean:
	rm -rf $(STAGE) $(PKG)
