# Makefile for Geany - Line Operations++.
#
# Derived from geany-plugins' lineoperations plugin (GPL-2+), extended with
# the full Notepad++ line-sort matrix and the remaining line operations.
#
# Targets:
#   make            build tests and the plugin
#   make test       build and run the Geany-independent core tests
#   make e2e        drive every menu entry in a real, headless Geany
#   make install    install the plugin into Geany's plugin directory
#   make install-user   install into ~/.local/lib/geany (no root needed)
#   make clean

PLUGIN      := lineoperations-plus
TARGET      := $(PLUGIN).so

PREFIX      ?= /usr/local
GEANY_LIBDIR ?= $(shell pkg-config --variable=libdir geany 2>/dev/null)
ifneq ($(GEANY_LIBDIR),)
  PLUGINDIR ?= $(GEANY_LIBDIR)/geany
else
  PLUGINDIR ?= $(PREFIX)/lib/geany
endif

USER_PLUGINDIR ?= $(HOME)/.local/lib/geany

PKG_CONFIG   ?= pkg-config
GEANY_CFLAGS := $(shell $(PKG_CONFIG) --cflags geany)
GEANY_LIBS   := $(shell $(PKG_CONFIG) --libs geany)

CC       ?= gcc
CFLAGS   ?= -O2 -g
override CFLAGS += -std=c99 -fPIC -Wall -Wextra -Wno-unused-parameter \
                   $(GEANY_CFLAGS)

# Upstream supplies these from autotools' config.h. This plugin is built
# with a plain makefile, so they are defined here instead.
override CFLAGS += -DGETTEXT_PACKAGE=\"$(PLUGIN)\" \
                   -DLOCALEDIR=\"$(PREFIX)/share/locale\"

# The core (lo_sort.c, lo_lines.c) is deliberately free of Scintilla and
# Geany references so the test binary can link it on its own.
CORE_SRC := src/lo_sort.c src/lo_lines.c src/lo_doc.c
CORE_OBJ := $(CORE_SRC:.c=.o)

PLUGIN_SRC := src/lineoperations.c src/lo_edit.c src/lo_prefs.c
PLUGIN_OBJ := $(CORE_OBJ) $(PLUGIN_SRC:.c=.o)

TEST_CORE := tests/test_lo_core
TEST_DOC  := tests/test_lo_doc
TEST_BIN := $(TEST_CORE) $(TEST_DOC)

.PHONY: all test asan e2e clean install install-user uninstall

all: $(TEST_CORE) $(TEST_DOC) $(TARGET)

$(TEST_CORE): tests/test_lo_core.o $(CORE_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell $(PKG_CONFIG) --libs glib-2.0)

$(TEST_DOC): tests/test_lo_doc.o src/lo_doc.o
	$(CC) $(LDFLAGS) -o $@ $^ $(shell $(PKG_CONFIG) --libs glib-2.0)

$(TARGET): $(PLUGIN_OBJ)
	$(CC) -shared $(LDFLAGS) -o $@ $^ $(GEANY_LIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -I$(CURDIR)/src -c -o $@ $<

test: $(TEST_CORE) $(TEST_DOC)
	./$(TEST_CORE)
	@echo
	./$(TEST_DOC)

# Rebuild the two test binaries with AddressSanitizer, UndefinedBehavior
# Sanitizer and LeakSanitizer. The tests link only the Geany-independent
# core, so they can be instrumented directly.
asan:
	$(MAKE) clean
	$(MAKE) test \
		CFLAGS="-O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address,undefined"

# Build the plugin itself under AddressSanitizer and run every menu entry
# in a throwaway Geany on a virtual display (needs Xvfb and xdotool).
e2e:
	$(MAKE) clean
	$(MAKE) $(TARGET) \
		CFLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address"
	tests/e2e.sh ./$(TARGET)
	$(MAKE) clean

install: $(TARGET)
	install -d $(DESTDIR)$(PLUGINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(PLUGINDIR)/$(TARGET)

install-user: $(TARGET)
	install -d $(USER_PLUGINDIR)
	install -m 0755 $(TARGET) $(USER_PLUGINDIR)/$(TARGET)
	@echo "Installed to $(USER_PLUGINDIR)"

uninstall:
	rm -f $(DESTDIR)$(PLUGINDIR)/$(TARGET)

clean:
	rm -f $(CORE_OBJ) $(PLUGIN_OBJ) tests/*.o $(TARGET) $(TEST_BIN)

# Header dependencies, kept explicit; the tree is small.
$(CORE_OBJ) $(PLUGIN_OBJ): src/lo_sort.h
src/lo_doc.o src/lo_edit.o: src/lo_doc.h
$(PLUGIN_OBJ) src/lo_edit.o src/lo_prefs.o src/lineoperations.o: src/lo_lines.h src/lo_edit.h src/lo_prefs.h
tests/test_lo_core.o: src/lo_sort.h src/lo_lines.h
tests/test_lo_doc.o: src/lo_doc.h
