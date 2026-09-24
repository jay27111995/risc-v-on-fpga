#ifndef STRING_H
#define STRING_H

#include "stddef.h"

static void *memset(void *s, int c, unsigned int n) {
    unsigned char *p = s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

static void *memcpy(void *dest, const void *src, unsigned int n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static int memcmp(const void *s1, const void *s2, unsigned int n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

static unsigned int strlen(const char *s) {
    unsigned int len = 0;
    while (*s++) len++;
    return len;
}

static char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

static char *strncpy(char *dest, const char *src, unsigned int n) {
    char *d = dest;
    while (n-- && (*d++ = *src++));
    while (n-- > 0) *d++ = '\0';
    return dest;
}

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int strncmp(const char *s1, const char *s2, unsigned int n) {
    while (n-- && *s1 && *s1 == *s2) { s1++; s2++; }
    return n == (unsigned int)-1 ? 0 : *(unsigned char*)s1 - *(unsigned char*)s2;
}

static char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

static char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    return c == '\0' ? (char*)s : (void*)0;
}

static char *strrchr(const char *s, int c) {
    const char *last = (void*)0;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    return c == '\0' ? (char*)s : (char*)last;
}

#endif
