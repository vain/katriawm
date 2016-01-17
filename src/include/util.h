#ifndef _WM_UTIL_H
#define _WM_UTIL_H

#ifdef DEBUG
#define D if (true)
#else
#define D if (false)
#endif

void *ecalloc(size_t nmemb, size_t size);

#endif /* _WM_UTIL_H */
