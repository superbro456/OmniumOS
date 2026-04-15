#ifndef MEM_H
#define MEM_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);

#endif
