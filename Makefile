NAME := alsa-scarlett-gui
VERSION := $(shell git describe --abbrev=4 --always --tags | sed 's/-/./g')

# rpm and dpkg both sort "1.0beta1" above "1.0", so a release does not
# supersede its own pre-releases. A tilde sorts below everything, and
# git refnames cannot contain one, so it is introduced here, for rpm and
# deb only: pacman gives it no meaning. VERSION stays as the tag is
# named: it is the tarball URL, the pkgver, and the version the binary
# reports for comparison against the online update index.
PKG_VERSION = $(shell \
  echo '$(VERSION)' | sed \
    -e 's/\.\(alpha\|beta\|rc\)/~\1/g' \
    -e 's/\([0-9]\)\(alpha\|beta\|rc\)/\1~\2/g' \
)

# The tilde versions sort below 1.0, but the betas already published
# without one do not. The epoch lifts every version from here on above
# them, and so must stay.
EPOCH := 1

NAMEVER := $(NAME)-$(VERSION)
TAR_FILE := $(NAMEVER).tar
TARGZ_FILE := $(TAR_FILE).gz
SPEC_FILE := $(NAME).spec

default:
	@echo "alsa-scarlett-gui"
	@echo
	@echo "If you want to build and install from source, please try:"
	@echo "  cd src"
	@echo "  make -j$(shell nproc)"
	@echo "  sudo make install"
	@echo
	@echo "This Makefile knows about packaging:"
	@echo "  make tar"
	@echo "  make rpm"
	@echo "  make deb"
	@echo "  make arch"

tar: $(TARGZ_FILE)

$(TARGZ_FILE):
	git archive --format=tar --prefix=$(NAMEVER)/ HEAD > $(TAR_FILE)
	sed -e 's|TAG_VERSION$$|$(VERSION)|' \
	    -e 's|VERSION$$|$(PKG_VERSION)|' \
	    -e 's|EPOCH$$|$(EPOCH)|' \
	    < $(SPEC_FILE).template > $(SPEC_FILE)
	tar --append -f $(TAR_FILE) \
		--transform s_^_$(NAMEVER)/_ \
		--owner=root --group=root \
		$(SPEC_FILE)
	rm -f $(SPEC_FILE)
	gzip < $(TAR_FILE) > $(TARGZ_FILE)
	rm -f $(TAR_FILE)

rpm: $(TARGZ_FILE)
	rpmbuild -tb $(TARGZ_FILE)

deb:
	rm -f src/vu.b4.alsa-scarlett-gui.desktop
	$(MAKE) -C src VERSION=$(VERSION) PREFIX=/usr
	mkdir -p deb-build/DEBIAN \
	         deb-build/usr/bin \
	         deb-build/usr/share/applications \
	         deb-build/usr/share/icons/hicolor/256x256/apps \
	         deb-build/usr/share/doc/$(NAME)
	cp src/alsa-scarlett-gui deb-build/usr/bin/
	cp src/vu.b4.alsa-scarlett-gui.desktop deb-build/usr/share/applications/
	cp src/img/vu.b4.alsa-scarlett-gui.png deb-build/usr/share/icons/hicolor/256x256/apps/
	cp -r README.md FAQ.md RELEASE-NOTES.md demo docs img deb-build/usr/share/doc/$(NAME)/
	sed -e "s/VERSION/$(PKG_VERSION)/g" \
	    -e "s/EPOCH/$(EPOCH)/g" \
	    debian/control > deb-build/DEBIAN/control
	dpkg-deb --root-owner-group --build deb-build $(NAME)_$(VERSION).deb
	rm -rf deb-build

arch:
	sed -e 's/VERSION/$(VERSION)/g' \
	    -e 's/EPOCH/$(EPOCH)/g' \
	    PKGBUILD.template > PKGBUILD
