# Credit to Tom Tromey and Paul D. Smith:
# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/

DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

CFLAGS := -Wall -Werror -ggdb -fno-omit-frame-pointer -O2 -D_FORTIFY_SOURCE=2

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

GLIB_COMPILE_RESOURCES := $(shell $(PKG_CONFIG) --variable=glib_compile_resources gio-2.0)

all: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJS) $(XML_OBJ)

depclean:
	rm -rf $(DEPDIR)

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:%.c=$(DEPDIR)/%.d) $(XML_SRC:%.xml=$(DEPDIR)/%-xml.d)
$(DEPFILES):

include $(wildcard $(DEPFILES))

$(TARGET): $(OBJS)
	cc ${LDFLAGS} -lm -o $(TARGET) $(OBJS)
