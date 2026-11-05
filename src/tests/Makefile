# SPDX-FileCopyrightText: 2023-2026 Geoffrey D. Bennett <g@b4.vu>
# SPDX-License-Identifier: GPL-3.0-or-later

PKG_CONFIG ?= pkg-config

TESTS = test-biquad test-remote-crypto

# The online update tests need the same libraries as the feature, so
# they are only built when it is enabled
ONLINE_UPDATE ?= 1
ifeq ($(ONLINE_UPDATE),1)
  TESTS += test-index-cache test-client-version test-requirements \
           test-downloaded
endif

CFLAGS = -I.. -Wall $(shell $(PKG_CONFIG) --cflags glib-2.0)
LDFLAGS = -lm -ldl $(shell $(PKG_CONFIG) --libs glib-2.0)

ONLINE_PKGS = gtk4 alsa libsoup-3.0 json-glib-1.0
ONLINE_CFLAGS = -DHAVE_ONLINE_UPDATE \
                $(shell $(PKG_CONFIG) --cflags $(ONLINE_PKGS))

# The app's own version. test-requirements overrides it, min_gui being
# a comparison against this.
ONLINE_VERSION = -DVERSION=\"test\"
ONLINE_LDFLAGS = -lcrypto $(shell $(PKG_CONFIG) --libs $(ONLINE_PKGS))

all: $(TESTS)

test-biquad: test-biquad.c ../biquad.c ../biquad.h
	$(CC) $(CFLAGS) -o $@ test-biquad.c ../biquad.c $(LDFLAGS)

test-index-cache: test-index-cache.c alsa-stubs.c ../online-update.c \
                  ../online-update.h ../scarlett4-firmware.c \
                  ../scarlett4-firmware.h
	$(CC) $(CFLAGS) $(ONLINE_CFLAGS) $(ONLINE_VERSION) -o $@ \
	  test-index-cache.c alsa-stubs.c ../online-update.c \
	  ../scarlett4-firmware.c $(LDFLAGS) $(ONLINE_LDFLAGS)

test-client-version: test-client-version.c alsa-stubs.c ../online-update.c \
                     ../online-update.h ../scarlett4-firmware.c \
                     ../scarlett4-firmware.h
	$(CC) $(CFLAGS) $(ONLINE_CFLAGS) $(ONLINE_VERSION) -o $@ \
	  test-client-version.c alsa-stubs.c ../online-update.c \
	  ../scarlett4-firmware.c $(LDFLAGS) $(ONLINE_LDFLAGS)

# min_gui compares against the app's own compiled-in version, so this
# one needs a VERSION it can make assertions about
test-requirements: test-requirements.c alsa-stubs.c ../online-update.c \
                   ../online-update.h ../scarlett4-firmware.c \
                   ../scarlett4-firmware.h
	$(CC) $(CFLAGS) $(ONLINE_CFLAGS) -DVERSION=\"1.0\" \
	  -o $@ test-requirements.c alsa-stubs.c ../online-update.c \
	  ../scarlett4-firmware.c $(LDFLAGS) $(ONLINE_LDFLAGS)

test-downloaded: test-downloaded.c alsa-stubs.c ../online-update.c \
                 ../online-update.h ../scarlett4-firmware.c \
                 ../scarlett4-firmware.h
	$(CC) $(CFLAGS) $(ONLINE_CFLAGS) $(ONLINE_VERSION) -o $@ \
	  test-downloaded.c alsa-stubs.c ../online-update.c \
	  ../scarlett4-firmware.c $(LDFLAGS) $(ONLINE_LDFLAGS)

test-remote-crypto: test-remote-crypto.c ../remote-crypto.c ../remote-crypto.h
	$(CC) $(CFLAGS) -o $@ test-remote-crypto.c ../remote-crypto.c $(LDFLAGS)

test: $(TESTS)
	@for t in $(TESTS); do echo "Running $$t..."; ./$$t || exit 1; done
