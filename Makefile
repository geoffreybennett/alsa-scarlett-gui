NAME := alsa-scarlett-gui
VERSION := $(shell git describe --abbrev=4 --always --tags | sed 's/-rc/~rc/g; s/-/./g')
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
	sed 's_VERSION$$_$(VERSION)_' < $(SPEC_FILE).template > $(SPEC_FILE)
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
	sed "s/VERSION/$(VERSION)/g" debian/control > deb-build/DEBIAN/control
	dpkg-deb --root-owner-group --build deb-build $(NAME)_$(VERSION).deb
	rm -rf deb-build

arch:
	sed 's/VERSION/$(VERSION)/g' PKGBUILD.template > PKGBUILD
