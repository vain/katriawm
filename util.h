#ifndef _WM_UTIL_H
#define _WM_UTIL_H

#ifdef DEBUG
#define DPRINTF(...) fprintf(stderr, __VA_ARGS__)
#else
#define DPRINTF(...) (void)0
#endif

#endif /* _WM_UTIL_H */
