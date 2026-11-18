#include "kernel_string.h"
#include <stdint.h>

/* rep movs/stos: процессор копирует сам, крупными кусками (раньше — по байту) */
void* kmemcpy(void* dst, const void* src, size_t n) {
    void *d = dst;
    size_t words = n / 4, rest = n % 4;
    __asm__ volatile("rep movsl" : "+D"(d), "+S"(src), "+c"(words) : : "memory");
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(rest) : : "memory");
    return dst;
}

void* kmemset(void* dst, int c, size_t n) {
    void *d = dst;
    uint32_t v = (uint8_t)c * 0x01010101u;
    size_t words = n / 4, rest = n % 4;
    __asm__ volatile("rep stosl" : "+D"(d), "+c"(words) : "a"(v) : "memory");
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(rest) : "a"(v) : "memory");
    return dst;
}

int kmemcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = a;
    const unsigned char* y = b;
    while (n--) {
        if (*x != *y) return *x - *y;
        x++; y++;
    }
    return 0;
}

size_t kstrlen(const char* s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

int kstrcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
