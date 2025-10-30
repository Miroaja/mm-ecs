HEADER = rational.hpp

PREFIX ?= /usr/local
INCLUDEDIR = $(PREFIX)/include/mm

INSTALL ?= install

.PHONY: all install uninstall clean

all:
	@echo "Nothing to build."

install:
	$(INSTALL) -Dm644 $(HEADER) $(INCLUDEDIR)/$(HEADER)
	@echo "Installed $(HEADER) to $(INCLUDEDIR)"

uninstall:
	rm -f $(INCLUDEDIR)/$(HEADER)
	@echo "Uninstalled $(HEADER) from $(INCLUDEDIR)"

clean:
	@echo "Nothing to clean."
