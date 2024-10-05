# cream - simple browser
# See LICENSE file for copyright and license details.

include config.mk

.PHONY: all clean install installdirs uninstall clean-install clean-uninstall $(NAME)

all: $(NAME)

$(NAME): browser.c
	$(CC) $(CFLAGS) $(LDFLAGS) \
		-DNAME=\"$(NAME)\" \
		-DNAME_UPPERCASE=\"$(NAME_UPPERCASE)\" \
		-DVERSION=\"$(VERSION)\" \
		-o $@ $< \
		`pkg-config --cflags --libs gtk+-3.0 glib-2.0 webkit2gtk-4.1`

install: all installdirs
	$(INSTALL_PROGRAM) $(NAME) $(DESTDIR)$(bindir)/$(NAME)
	sed "s/VERSION/$(VERSION)/g" $(NAME).1 > $(DESTDIR)$(man1dir)/$(NAME).1
	chmod 644 $(DESTDIR)$(man1dir)/$(NAME).1
	$(INSTALL_DATA) $(NAME).desktop $(DESTDIR)$(applicationsdir)/$(NAME).desktop
	$(INSTALL_DATA) $(NAME).png $(DESTDIR)$(iconsdir)/$(NAME).png

installdirs:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(man1dir) \
		$(DESTDIR)$(applicationsdir) $(DESTDIR)$(iconsdir)

uninstall:
	rm -f $(DESTDIR)$(bindir)/$(NAME)
	rm -f $(DESTDIR)$(man1dir)/$(NAME).1
	rm -f $(DESTDIR)$(applicationsdir)/$(NAME).desktop
	rm -f $(DESTDIR)$(iconsdir)/$(NAME).png

clean:
	rm -f $(NAME)

clean-install: clean uninstall install

clean-uninstall: uninstall clean
