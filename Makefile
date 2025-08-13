# Detect distribution and set library path
DISTRO := $(shell if [ -f /etc/fedora-release ]; then echo "fedora"; elif [ -f /etc/debian_version ]; then echo "debian"; else echo "unknown"; fi)

ifeq ($(DISTRO),fedora)
    LIBDIR = /usr/lib64/xfce4/panel/plugins
else
    LIBDIR = /usr/lib/xfce4/panel/plugins
endif

# Build configuration
DEBUG ?= 0
PREFIX = /usr
PLUGINDIR = $(PREFIX)/share/xfce4/panel/plugins

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -fPIC -std=c99
LDFLAGS = -shared

ifeq ($(DEBUG),1)
    CFLAGS += -g -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

PKGS = gtk+-3.0 libxfce4panel-2.0 libxfce4ui-2 libwnck-3.0 libxml-2.0 libxfconf-0

CFLAGS += $(shell pkg-config --cflags $(PKGS)) -DWNCK_I_KNOW_THIS_IS_UNSTABLE
LIBS = $(shell pkg-config --libs $(PKGS))

# Source files
SOURCES = launch-menu.c

# Output files
TARGET = liblaunch-menu.so

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(TARGET)

install: all
	install -d $(DESTDIR)$(PREFIX)/lib64
	install -d $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(PLUGINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(LIBDIR)/
	install -m 644 launch-menu.desktop $(DESTDIR)$(PLUGINDIR)/

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/$(TARGET)
	rm -f $(DESTDIR)$(PLUGINDIR)/launch-menu.desktop

.PHONY: all clean install uninstall
