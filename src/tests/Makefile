# SPDX-FileCopyrightText: 2025 Geoffrey D. Bennett <g@b4.vu>
# SPDX-License-Identifier: GPL-3.0-or-later

PKG_CONFIG ?= pkg-config

TESTS = test-biquad

CFLAGS = -I.. -Wall $(shell $(PKG_CONFIG) --cflags glib-2.0)
LDFLAGS = -lm $(shell $(PKG_CONFIG) --libs glib-2.0)

all: $(TESTS)

test-biquad: test-biquad.c ../biquad.c ../biquad.h
	$(CC) $(CFLAGS) -o $@ test-biquad.c ../biquad.c $(LDFLAGS)

test: $(TESTS)
	@for t in $(TESTS); do echo "Running $$t..."; ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all test clean
