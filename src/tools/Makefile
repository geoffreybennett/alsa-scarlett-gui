# SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
# SPDX-License-Identifier: GPL-3.0-or-later

PKG_CONFIG ?= pkg-config

CFLAGS ?= -ggdb -O2
CFLAGS += -Wall -Werror
CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0 alsa)

LDFLAGS += $(shell $(PKG_CONFIG) --libs glib-2.0 alsa)

TARGETS := alsa-scarlett-config

all: $(TARGETS)

alsa-scarlett-config: alsa-scarlett-config.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)

ifeq ($(PREFIX),)
  PREFIX := /usr/local
endif

BINDIR := $(DESTDIR)$(PREFIX)/bin

install: all
	install -d $(BINDIR)
	install -m 755 $(TARGETS) $(BINDIR)

uninstall:
	rm -f $(BINDIR)/alsa-scarlett-config

.PHONY: all clean install uninstall
