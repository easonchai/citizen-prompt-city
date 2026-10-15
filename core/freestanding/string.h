/* Minimal libc headers for the wasm32 freestanding build only (see core/libc_shim.c). */
#ifndef CZ_SHIM_STRING_H
#define CZ_SHIM_STRING_H
#include <stddef.h>
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);
#endif
