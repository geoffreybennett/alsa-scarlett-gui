# SPDX-FileCopyrightText: 2022-2025 Geoffrey D. Bennett <g@b4.vu>
# SPDX-License-Identifier: GPL-3.0-or-later

# Credit to Tom Tromey and Paul D. Smith:
# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/

# Parallel build by default
MAKEFLAGS += -j$(shell nproc)

# Colors for pretty output
COLOR_RESET = \033[0m
COLOR_GREEN = \033[32m
COLOR_BLUE  = \033[34m

# Pretty print (use V=1 to see full commands)
ifeq ($(V),1)
  Q =
  PRINTF = @true
else
  Q = @
  PRINTF = @printf
endif

# Progress counter
TOTAL := $(words $(sort $(wildcard *.c) $(patsubst %.xml,%.c,$(wildcard *.xml))))
DONE :=
counter = $(words $(DONE))$(eval DONE += x)
PCT = $$(( $(counter) * 100 / $(TOTAL) ))

VERSION := $(shell \
  git describe --abbrev=4 --dirty --always --tags 2>/dev/null || \
  echo $${APP_VERSION:-Unknown} \
)

DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

CFLAGS ?= -ggdb -fno-omit-frame-pointer -fPIE -O2
CFLAGS += -Wall -Werror
CFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
CFLAGS += -DVERSION=\"$(VERSION)\"
CFLAGS += -Wno-error=deprecated-declarations

PKG_CONFIG=pkg-config

CFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0)
CFLAGS += $(shell $(PKG_CONFIG) --cflags gtk4)
CFLAGS += $(shell $(PKG_CONFIG) --cflags alsa)

LDFLAGS += $(shell $(PKG_CONFIG) --libs glib-2.0)
LDFLAGS += $(shell $(PKG_CONFIG) --libs gtk4)
LDFLAGS += $(shell $(PKG_CONFIG) --libs alsa)
LDFLAGS += -lm -lcrypto -pie

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c

%.c: %.xml $(DEPDIR)/%-xml.d | $(DEPDIR)
	$(PRINTF) "$(COLOR_GREEN)[%3d%%]$(COLOR_RESET) $(COLOR_BLUE)RES$(COLOR_RESET)  %s\n" $(PCT) $<
	$(Q)echo $@: $(shell $(GLIB_COMPILE_RESOURCES) $< --generate-dependencies) > $(DEPDIR)/$*-xml.d
	$(Q)$(GLIB_COMPILE_RESOURCES) $< --target=$@ --generate-source

XML_SRC := $(wildcard *.xml)
XML_OBJ := $(patsubst %.xml,%.c,$(XML_SRC))

%.o: %.c
%.o: %.c Makefile $(DEPDIR)/%.d | $(DEPDIR)
	$(PRINTF) "$(COLOR_GREEN)[%3d%%]$(COLOR_RESET) $(COLOR_BLUE)CC$(COLOR_RESET)   %s\n" $(PCT) $<
	$(Q)$(COMPILE.c) $(OUTPUT_OPTION) $<

SRCS := $(sort $(wildcard *.c) $(XML_OBJ))
OBJS := $(patsubst %.c,%.o,$(SRCS))
TARGET := alsa-scarlett-gui
DOMAIN_PREFIX := vu.b4
DESKTOP_FILE := $(DOMAIN_PREFIX).$(TARGET).desktop
ICON_FILE := $(DOMAIN_PREFIX).$(TARGET).png

GLIB_COMPILE_RESOURCES := $(shell $(PKG_CONFIG) --variable=glib_compile_resources gio-2.0)

all: $(TARGET) $(DESKTOP_FILE)

clean: depclean
	rm -f $(TARGET) $(DESKTOP_FILE) $(OBJS) $(XML_OBJ)

depclean:
	rm -rf $(DEPDIR)

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:%.c=$(DEPDIR)/%.d) $(XML_SRC:%.xml=$(DEPDIR)/%-xml.d)
$(DEPFILES):

include $(wildcard $(DEPFILES))

$(TARGET): $(OBJS)
	$(PRINTF) "$(COLOR_GREEN)[100%%]$(COLOR_RESET) $(COLOR_BLUE)LINK$(COLOR_RESET) %s\n" $@
	$(Q)$(CC) -o $(TARGET) $(OBJS) ${LDFLAGS}

ifeq ($(PREFIX),)
  PREFIX := /usr/local
endif

BINDIR := $(DESTDIR)$(PREFIX)/bin
ICONTOP := $(DESTDIR)$(PREFIX)/share/icons/hicolor
ICONDIR := $(ICONTOP)/256x256/apps
DESKTOPDIR := $(DESTDIR)$(PREFIX)/share/applications

$(DESKTOP_FILE): $(DESKTOP_FILE).template
	$(PRINTF) "$(COLOR_GREEN)[100%%]$(COLOR_RESET) $(COLOR_BLUE)GEN$(COLOR_RESET)  %s\n" $@
	$(Q)sed 's_PREFIX_$(PREFIX)_' < $< > $@

install: all
	$(PRINTF) "$(COLOR_BLUE)INSTALL$(COLOR_RESET) %s\n" $(BINDIR)/$(TARGET)
	$(Q)install -d $(BINDIR)
	$(Q)install -m 755 $(TARGET) $(BINDIR)
	$(PRINTF) "$(COLOR_BLUE)INSTALL$(COLOR_RESET) %s\n" $(ICONDIR)/$(ICON_FILE)
	$(Q)install -d $(ICONDIR)
	$(Q)install -m 644 img/$(ICON_FILE) $(ICONDIR)
	$(PRINTF) "$(COLOR_BLUE)INSTALL$(COLOR_RESET) %s\n" $(DESKTOPDIR)/$(DESKTOP_FILE)
	$(Q)install -d $(DESKTOPDIR)
	$(Q)install -m 644 $(DESKTOP_FILE) $(DESKTOPDIR)

uninstall:
	$(PRINTF) "$(COLOR_BLUE)REMOVE$(COLOR_RESET)  %s\n" $(BINDIR)/$(TARGET)
	$(Q)rm -f $(BINDIR)/$(TARGET)
	$(PRINTF) "$(COLOR_BLUE)REMOVE$(COLOR_RESET)  %s\n" $(ICONDIR)/$(ICON_FILE)
	$(Q)rm -f $(ICONDIR)/$(ICON_FILE)
	$(PRINTF) "$(COLOR_BLUE)REMOVE$(COLOR_RESET)  %s\n" $(DESKTOPDIR)/$(DESKTOP_FILE)
	$(Q)rm -f $(DESKTOPDIR)/$(DESKTOP_FILE)

help:
	@echo "alsa-scarlett-gui"
	@echo
	@echo "This Makefile knows about:"
	@echo "  make"
	@echo "  make install"
	@echo "  make uninstall"
