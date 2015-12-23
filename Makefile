CFLAGS += -Wall -Wextra -Wno-unused-parameter -O3
__NAME__ = wm
__NAME_UPPERCASE__ = `echo $(__NAME__) | sed 's/.*/\U&/'`
__NAME_CAPITALIZED__ = `echo $(__NAME__) | sed 's/^./\U&\E/'`

$(__NAME__): wm.c config.h
	$(CC) $(CFLAGS) $(LDFLAGS) \
		-D__NAME__=\"$(__NAME__)\" \
		-D__NAME_UPPERCASE__=\"$(__NAME_UPPERCASE__)\" \
		-D__NAME_CAPITALIZED__=\"$(__NAME_CAPITALIZED__)\" \
		-o $@ $< \
		-lX11 -lXrandr

config.h:
	cp config.def.h config.h

clean:
	rm -f $(__NAME__)
