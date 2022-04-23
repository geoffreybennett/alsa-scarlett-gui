# Makefile for various po files.

podir = .

MSGINIT   = msginit
MSGMERGE  = msgmerge
MSGFMT    = msgfmt
XGETTEXT  = xgettext
CATOBJEXT = .po

include linguas

MO_FILES = $(addsuffix .gmo, $(LINGUAS))

TD = alsa-scarlett-gui

default: help

fail:
	false

all: $(TD).pot update-po update-mo

help:
	@echo "Available targets:"
	@echo "  pot                       - remake master catalog"
	@echo "  update-po                 - merge po files"
	@echo "  update-mo                 - regenerate mo files"
	@echo "  all                       - all of the above"

POTFILES = ../about.c ../driver-strings.txt ../file.c ../iface-mixer.c ../iface-no-mixer.c ../iface-none.c ../iface-unknown.c ../menu.c ../tooltips.c ../widget-combo.c ../window-hardware.c ../window-routing.c ../window-startup.c

pot: $(TD).pot 

clean:
	rm -f *~ *.bak *.gmo $(TD).pot

$(TD).pot: $(POTFILES)
	$(XGETTEXT) --output=$(podir)/$(TD).pox --from-code=utf-8 \
	  --keyword=_ --keyword=N_ --language=C --add-comments --sort-output $(POTFILES)  && \
	rm -f $@ && mv $(TD).pox $@

update-mo: $(MO_FILES)

update-po:
	$(MAKE) $(TD).pot
	cd $(podir); \
	catalogs='$(LINGUAS)'; \
	for lang in $$catalogs; do \
		echo "$$lang:"; \
		if [ -f $$lang.po ]; then \
			$(MSGMERGE) --update $$lang.po $(TD).pot; \
		else \
			$(MSGINIT) --input=$(TD).pot --locale=$$lang --output=$$lang.po ; \
		fi \
	done

.SUFFIXES:
.SUFFIXES: .po .gmo

.po.gmo:
	$(MSGFMT) --check --statistics --verbose -o $@ $<


