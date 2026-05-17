PREFIX ?= /usr/local
BINARY  = display-toggle

.PHONY: all install uninstall clean

all: $(BINARY)

$(BINARY): display-toggle.c
	clang -O2 -Wall -Wextra -o $@ $< -framework CoreGraphics -framework CoreFoundation -framework ColorSync

install: $(BINARY)
	install -d $(PREFIX)/bin
	install -m 755 $(BINARY) $(PREFIX)/bin/

uninstall:
	rm -f $(PREFIX)/bin/$(BINARY)

clean:
	rm -f $(BINARY)
