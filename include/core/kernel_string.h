#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <stddef.h>

void* kmemcpy(void* dst, const void* src, size_t n);
void* kmemset(void* dst, int c, size_t n);
int   kmemcmp(const void* a, const void* b, size_t n);

size_t kstrlen(const char* s);
int    kstrcmp(const char* a, const char* b);

#endif
