#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <sys/random.h>
static inline void esp_fill_random(void *dest, size_t size) {
    unsigned char *p=(unsigned char *)dest;
    while (size) { ssize_t n=getrandom(p,size,0); if(n<=0)abort(); p+=n;size-=(size_t)n; }
}
