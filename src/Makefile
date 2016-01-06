include config.mk

.PHONY: all clean install installdirs

all: core/$(__NAME_WM__) core/$(__NAME_C__) barinfo/$(__NAME_BI__)

core/$(__NAME_WM__): core/wm.c core/config.h core/theme.h \
	include/ipc.h include/util.h include/theme_types.h
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(__NAME_DEFINES__) $(DEBUGFLAGS) -DSRVR_$$HOSTNAME \
		-o $@ $< \
		-Icore -Iinclude -I/usr/include/freetype2 \
		-lX11 -lXrandr -lXft

core/$(__NAME_C__): core/client.c include/ipc.h include/util.h
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(__NAME_DEFINES__) $(DEBUGFLAGS) -DSRVR_$$HOSTNAME \
		-Iinclude -o $@ $< -lX11

barinfo/$(__NAME_BI__): barinfo/barinfo.c barinfo/config.h \
	include/ipc.h include/util.h
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(__NAME_DEFINES__) $(DEBUGFLAGS) -DSRVR_$$HOSTNAME \
		-Iinclude -o $@ $< -lX11

core/config.h:
	cp core/config.def.h $@

core/theme.h:
	../themes/make_theme_h.sh ../themes/motif >$@

barinfo/config.h:
	cp barinfo/config.def.h $@

install: all installdirs
	$(INSTALL_PROGRAM) core/$(__NAME_WM__) $(DESTDIR)$(bindir)/$(__NAME_WM__)
	$(INSTALL_PROGRAM) core/$(__NAME_C__) $(DESTDIR)$(bindir)/$(__NAME_C__)
	$(INSTALL_PROGRAM) barinfo/$(__NAME_BI__) $(DESTDIR)$(bindir)/$(__NAME_BI__)
	$(INSTALL_DATA) ../doc/man1/$(__NAME_WM__).1 $(DESTDIR)$(man1dir)/$(__NAME_WM__).1
	$(INSTALL_DATA) ../doc/man1/$(__NAME_C__).1 $(DESTDIR)$(man1dir)/$(__NAME_C__).1
	$(INSTALL_DATA) ../doc/man1/$(__NAME_BI__).1 $(DESTDIR)$(man1dir)/$(__NAME_BI__).1

installdirs:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(man1dir)

clean:
	rm -f core/$(__NAME_WM__) core/$(__NAME_C__) barinfo/$(__NAME_BI__)
