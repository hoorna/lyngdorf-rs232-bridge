# Build the event-driven C++ bridge.
#   make                     build ./lyngdorf_bridge
#   sudo make install        install to /usr/local/bin
#   make CXX=clang++         build with clang instead of gcc
#
# Plain compiler command, if you prefer not to use make:
#   g++ -O2 -std=c++17 -Wall -Wextra -o lyngdorf_bridge src/lyngdorf_bridge.cpp $(pkg-config --cflags --libs libusb-1.0)

CXXFLAGS   ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wextra
PKG_CONFIG ?= pkg-config
USB_CFLAGS := $(shell $(PKG_CONFIG) --cflags libusb-1.0)
USB_LIBS   := $(shell $(PKG_CONFIG) --libs libusb-1.0)
PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin

all: lyngdorf_bridge

lyngdorf_bridge: src/lyngdorf_bridge.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(USB_CFLAGS) -o $@ $< $(LDFLAGS) $(USB_LIBS)

install: lyngdorf_bridge
	install -D -m 0755 lyngdorf_bridge $(DESTDIR)$(BINDIR)/lyngdorf_bridge

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/lyngdorf_bridge

clean:
	rm -f lyngdorf_bridge

.PHONY: all install uninstall clean
