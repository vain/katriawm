#include <assert.h>
#include <stdlib.h>

#include "util.h"

void *
ecalloc(size_t nmemb, size_t size)
{
    void *p;

    p = calloc(nmemb, size);
    assert(p != NULL);
    return p;
}
