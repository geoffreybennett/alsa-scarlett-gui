NAME := alsa-scarlett-gui
VERSION := $(shell git describe --abbrev=4 --always --tags | sed 's/-/./g')
NAMEVER := $(NAME)-$(VERSION)
TAR_FILE := $(NAMEVER).tar
TARGZ_FILE := $(TAR_FILE).gz
SPEC_FILE := $(NAME).spec

default:
	@echo "alsa-scarlett-gui"
	@echo
	@echo "If you want to build and install from source, please try:"
	@echo "  cd src"
	@echo "  make -j4"
	@echo "  sudo make install"
	@echo
	@echo "This Makefile knows about packaging:"
	@echo "  make tar"
	@echo "  make rpm"

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
