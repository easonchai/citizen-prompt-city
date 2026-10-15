/* Tiny libc for the wasm32 freestanding build (qrcodegen.c and compiler-emitted calls).
 * Native and ESP32 builds use the real libc, so everything here is behind CZ_FREESTANDING. */
#ifdef CZ_FREESTANDING
#include <stddef.h>

void *memset(void *d, int c, size_t n) {
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    if (p < q) { while (n--) *p++ = *q++; }
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}

size_t strlen(const char *s) {
    const char *e = s;
    while (*e) e++;
    return (size_t)(e - s);
}

char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return 0;
    }
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
#else
typedef int cz_libc_shim_unused; /* ISO C forbids an empty translation unit */
#endif
