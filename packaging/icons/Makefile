# Build the OPTIONAL Campiello icon package (.hpkg).
#
# Installs every optional/icons/*.hvif to /system/data/campiello/icons/, where the WON app and the
# ~/WON shortcuts look them up (docs/ICONS.md). The icons are NOT part of the MIT core; see
# optional/icons/ATTRIBUTION for their authorship and terms.
#
#   make package    # builds campiello_icons-<version>.hpkg from optional/icons/*.hvif
#   pkgman install ./campiello_icons-0.1.0-1-any.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-1
ARCH := any
PKG := campiello_icons-$(VERSION)-$(ARCH).hpkg

ICONS := ../../optional/icons
STAGE := stage

.PHONY: package clean
package:
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/data/campiello/icons
	@n=0; for f in $(ICONS)/*.hvif; do \
		[ -e "$$f" ] || continue; \
		cp "$$f" $(STAGE)/data/campiello/icons/; n=$$((n+1)); \
	done; \
	cp $(ICONS)/ATTRIBUTION $(STAGE)/data/campiello/icons/ATTRIBUTION; \
	echo "Staged $$n icon(s)"; \
	if [ $$n -eq 0 ]; then echo "NOTE: no .hvif files in $(ICONS) yet - see docs/ICONS.md"; fi
	package create -C $(STAGE) -i campiello_icons.PackageInfo $(PKG)
	@echo "Built $(PKG)"

clean:
	rm -rf $(STAGE) $(PKG)
