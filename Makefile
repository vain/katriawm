CFLAGS += -Wall -Wextra -O3

__NAME__ = wm
__NAME_WM__ = $(__NAME__)
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

.PHONY: all

all: $(__NAME_WM__) $(__NAME_C__)

$(__NAME_WM__): wm.c ipc.h pixmaps.h
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(__NAME_DEFINES__) \
		-o $@ $< \
		-lX11 -lXrandr

$(__NAME_C__): client.c ipc.h
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(__NAME_DEFINES__) \
		-o $@ $< \
		-lX11

clean:
	rm -f $(__NAME_WM__) $(__NAME_C__)
