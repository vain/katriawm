CFLAGS += -Wall -Wextra -O3

# To enable debug output:
#DEBUGFLAGS = -DDEBUG

__NAME__ = katria
__NAME_WM__ = $(__NAME__)wm
__NAME_C__ = $(__NAME__)c

__NAME_UPPERCASE__ = `echo $(__NAME__) | sed 's/.*/\U&/'`
__NAME_CAPITALIZED__ = `echo $(__NAME__) | sed 's/^./\U&\E/'`

__NAME_WM_UPPERCASE__ = `echo $(__NAME_WM__) | sed 's/.*/\U&/'`
__NAME_WM_CAPITALIZED__ = `echo $(__NAME_WM__) | sed 's/^./\U&\E/'`

__NAME_C_UPPERCASE__ = `echo $(__NAME_C__) | sed 's/.*/\U&/'`
__NAME_C_CAPITALIZED__ = `echo $(__NAME_C__) | sed 's/^./\U&\E/'`

__NAME_DEFINES__ = \
		-D__NAME__=\"$(__NAME__)\" \
		-D__NAME_UPPERCASE__=\"$(__NAME_UPPERCASE__)\" \
		-D__NAME_CAPITALIZED__=\"$(__NAME_CAPITALIZED__)\" \
		-D__NAME_WM__=\"$(__NAME_WM__)\" \
		-D__NAME_WM_UPPERCASE__=\"$(__NAME_WM_UPPERCASE__)\" \
		-D__NAME_WM_CAPITALIZED__=\"$(__NAME_WM_CAPITALIZED__)\" \
		-D__NAME_C__=\"$(__NAME_C__)\" \
		-D__NAME_C_UPPERCASE__=\"$(__NAME_C_UPPERCASE__)\" \
		-D__NAME_C_CAPITALIZED__=\"$(__NAME_C_CAPITALIZED__)\"

INSTALL = install
INSTALL_PROGRAM = $(INSTALL)
INSTALL_DATA = $(INSTALL) -m 644

prefix = /usr/local
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
datarootdir = $(prefix)/share
mandir = $(datarootdir)/man
man1dir = $(mandir)/man1

.PHONY: all clean install installdirs

all: $(__NAME_WM__) $(__NAME_C__)

$(__NAME_WM__): wm.c ipc.h pixmaps.h util.h
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(__NAME_DEFINES__) $(DEBUGFLAGS) \
		-o $@ $< \
		-lX11 -lXrandr

$(__NAME_C__): client.c ipc.h util.h
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(__NAME_DEFINES__) $(DEBUGFLAGS) \
		-o $@ $< \
		-lX11

install: all installdirs
	$(INSTALL_PROGRAM) $(__NAME_WM__) $(DESTDIR)$(bindir)/$(__NAME_WM__)
	$(INSTALL_PROGRAM) $(__NAME_C__) $(DESTDIR)$(bindir)/$(__NAME_C__)
	$(INSTALL_DATA) man1/$(__NAME_WM__).1 $(DESTDIR)$(man1dir)/$(__NAME_WM__).1
	$(INSTALL_DATA) man1/$(__NAME_C__).1 $(DESTDIR)$(man1dir)/$(__NAME_C__).1

installdirs:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(man1dir)

clean:
	rm -f $(__NAME_WM__) $(__NAME_C__)
