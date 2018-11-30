/* 61604239 */

/* This file is definitions of utility functions for mysh. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include "utils.h"

// my_malloc() allocates size bytes on memory, initializes it and returns its pointer.
void*
my_malloc(size_t sz)
{
    void *p;
    p = calloc(1, sz);
    if (!p) {
        exit(3);
    }
    return p;
}

// my_realloc() reallocates size bytes to ptr amd return its pointer.
void*
my_realloc(void *ptr, size_t sz)
{
    void *p;
    if (!ptr) {
    	return my_malloc(sz);
    }
    p = realloc(ptr, sz);
    if (!p) {
        exit(3);
    }
    return p;
}
