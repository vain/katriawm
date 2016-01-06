# To enable debug output:
#DEBUGFLAGS = -DDEBUG

CFLAGS += -std=c99 -Wall -Wextra -O3

INSTALL = install
INSTALL_PROGRAM = $(INSTALL)
INSTALL_DATA = $(INSTALL) -m 644

prefix = /usr/local
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
datarootdir = $(prefix)/share
mandir = $(datarootdir)/man
man1dir = $(mandir)/man1

# The source code itself shall include as little verbatim references to
# the project name as possible.
__NAME__ = katria
__NAME_WM__ = $(__NAME__)wm
__NAME_C__ = $(__NAME__)c
__NAME_BI__ = $(__NAME__)bi

__NAME_UPPERCASE__ = `echo $(__NAME__) | sed 's/.*/\U&/'`
__NAME_CAPITALIZED__ = `echo $(__NAME__) | sed 's/^./\U&\E/'`

__NAME_WM_UPPERCASE__ = `echo $(__NAME_WM__) | sed 's/.*/\U&/'`
__NAME_WM_CAPITALIZED__ = `echo $(__NAME_WM__) | sed 's/^./\U&\E/'`

__NAME_C_UPPERCASE__ = `echo $(__NAME_C__) | sed 's/.*/\U&/'`
__NAME_C_CAPITALIZED__ = `echo $(__NAME_C__) | sed 's/^./\U&\E/'`

__NAME_BI_UPPERCASE__ = `echo $(__NAME_BI__) | sed 's/.*/\U&/'`
__NAME_BI_CAPITALIZED__ = `echo $(__NAME_BI__) | sed 's/^./\U&\E/'`

__NAME_DEFINES__ = \
		-D__NAME__=\"$(__NAME__)\" \
		-D__NAME_UPPERCASE__=\"$(__NAME_UPPERCASE__)\" \
		-D__NAME_CAPITALIZED__=\"$(__NAME_CAPITALIZED__)\" \
		-D__NAME_WM__=\"$(__NAME_WM__)\" \
		-D__NAME_WM_UPPERCASE__=\"$(__NAME_WM_UPPERCASE__)\" \
		-D__NAME_WM_CAPITALIZED__=\"$(__NAME_WM_CAPITALIZED__)\" \
		-D__NAME_C__=\"$(__NAME_C__)\" \
		-D__NAME_C_UPPERCASE__=\"$(__NAME_C_UPPERCASE__)\" \
		-D__NAME_C_CAPITALIZED__=\"$(__NAME_C_CAPITALIZED__)\" \
		-D__NAME_BI__=\"$(__NAME_BI__)\" \
		-D__NAME_BI_UPPERCASE__=\"$(__NAME_BI_UPPERCASE__)\" \
		-D__NAME_BI_CAPITALIZED__=\"$(__NAME_BI_CAPITALIZED__)\"
