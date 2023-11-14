# SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
# SPDX-License-Identifier: GPL-3.0-or-later

# Credit to Tom Tromey and Paul D. Smith:
# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/

VERSION := $(shell \
  git describe --abbrev=4 --dirty --always --tags 2>/dev/null || \
  echo $${APP_VERSION:-Unknown} \
)

DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

CFLAGS := -Wall -Werror -ggdb -fno-omit-frame-pointer -O2 -D_FORTIFY_SOURCE=2
CFLAGS += -DVERSION=\"$(VERSION)\"
CFLAGS += -Wno-error=deprecated-declarations

PKG_CONFIG=pkg-config

CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0)
CFLAGS += $(shell $(PKG_CONFIG) --cflags gtk4)
CFLAGS += $(shell $(PKG_CONFIG) --cflags alsa)

LDFLAGS += $(shell $(PKG_CONFIG) --libs glib-2.0)
LDFLAGS += $(shell $(PKG_CONFIG) --libs gtk4)
LDFLAGS += $(shell $(PKG_CONFIG) --libs alsa)

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c

%.c: %.xml $(DEPDIR)/%-xml.d | $(DEPDIR)
	echo $@: $(shell $(GLIB_COMPILE_RESOURCES) $< --generate-dependencies) > $(DEPDIR)/$*-xml.d
	$(GLIB_COMPILE_RESOURCES) $< --target=$@ --generate-source

XML_SRC := $(wildcard *.xml)
XML_OBJ := $(patsubst %.xml,%.c,$(XML_SRC))

%.o: %.c
%.o: %.c Makefile $(DEPDIR)/%.d | $(DEPDIR)
	$(COMPILE.c) $(OUTPUT_OPTION) $<

SRCS := $(sort $(wildcard *.c) $(XML_OBJ))
OBJS := $(patsubst %.c,%.o,$(SRCS))
TARGET := alsa-scarlett-gui
DOMAIN_PREFIX := vu.b4
DESKTOP_FILE := $(DOMAIN_PREFIX).$(TARGET).desktop
ICON_FILE := $(DOMAIN_PREFIX).$(TARGET).png

GLIB_COMPILE_RESOURCES := $(shell $(PKG_CONFIG) --variable=glib_compile_resources gio-2.0)

all: $(TARGET) $(DESKTOP_FILE)

clean:
	rm -f $(TARGET) $(DESKTOP_FILE) $(OBJS) $(XML_OBJ)

depclean:
	rm -rf $(DEPDIR)

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:%.c=$(DEPDIR)/%.d) $(XML_SRC:%.xml=$(DEPDIR)/%-xml.d)
$(DEPFILES):

include $(wildcard $(DEPFILES))

$(TARGET): $(OBJS)
	cc -o $(TARGET) $(OBJS) ${LDFLAGS} -lm 

ifeq ($(PREFIX),)
  PREFIX := /usr/local
endif

BINDIR := $(DESTDIR)$(PREFIX)/bin
ICONTOP := $(DESTDIR)$(PREFIX)/share/icons/hicolor
ICONDIR := $(ICONTOP)/256x256/apps
DESKTOPDIR := $(DESTDIR)$(PREFIX)/share/applications

$(DESKTOP_FILE): $(DESKTOP_FILE).template
	sed 's_PREFIX_$(PREFIX)_' < $< > $@

install: all
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)
	install -d $(ICONDIR)
	install -m 644 img/$(ICON_FILE) $(ICONDIR)
	install -d $(DESKTOPDIR)
	install -m 644 $(DESKTOP_FILE) $(DESKTOPDIR)

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(ICONDIR)/$(ICON_FILE)
	rm -f $(DESKTOPDIR)/$(DESKTOP_FILE)

help:
	@echo "alsa-scarlett-gui"
	@echo
	@echo "This Makefile knows about:"
	@echo "  make"
	@echo "  make install"
	@echo "  make uninstall"
