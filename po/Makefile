# SPDX-FileCopyrightText: 2026 Geoffrey D. Bennett <g@b4.vu>
# SPDX-License-Identifier: GPL-3.0-or-later

PACKAGE = alsa-scarlett-gui
POFILES = $(wildcard *.po)
MOFILES = $(patsubst %.po,%.mo,$(POFILES))

all: $(MOFILES)
	@for mo in $(MOFILES); do \
		lang=$${mo%.mo}; \
		mkdir -p ../locale/$$lang/LC_MESSAGES; \
		cp -f $$mo ../locale/$$lang/LC_MESSAGES/$(PACKAGE).mo; \
	done

%.mo: %.po
	msgfmt -o $@ $<

update-po: $(PACKAGE).pot
	@for po in $(POFILES); do \
		echo "Updating $$po..."; \
		msgmerge --update $$po $(PACKAGE).pot; \
	done

$(PACKAGE).pot: POTFILES.in
	xgettext --default-domain=$(PACKAGE) \
	         --directory=.. \
	         --files-from=POTFILES.in \
	         --keyword=_ --keyword=N_ \
	         --from-code=UTF-8 \
	         --output=$(PACKAGE).pot

clean:
	rm -f *.mo
	rm -rf ../locale

.PHONY: all update-po clean
