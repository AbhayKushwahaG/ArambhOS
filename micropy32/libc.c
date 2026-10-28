#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

void *memcpy(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    char *p = (char *)s;
    while (n--) {
        *p++ = (char)c;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++)) ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n && (*d++ = *src++)) {
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return dest;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return NULL;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    if (!buf || size == 0) return 0;
    size_t out = 0;
    while (*fmt) {
        if (*fmt != '%') {
            if (out + 1 < size) buf[out] = *fmt;
            out++;
            fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            if (out + 1 < size) buf[out] = '%';
            out++;
            fmt++;
            continue;
        }

        int pad_zero = 0;
        int left_align = 0;
        int width = 0;
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left_align = 1;
            if (*fmt == '0') pad_zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == 'l' || *fmt == 'z') {
            fmt++;
            if (*fmt == 'l') fmt++;
        }

        if (*fmt == 's') {
            fmt++;
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t slen = strlen(s);
            if (!left_align && width > (int)slen) {
                for (int p = 0; p < width - (int)slen; p++) {
                    if (out + 1 < size) buf[out] = ' ';
                    out++;
                }
            }
            while (*s) {
                if (out + 1 < size) buf[out] = *s;
                out++;
                s++;
            }
            if (left_align && width > (int)slen) {
                for (int p = 0; p < width - (int)slen; p++) {
                    if (out + 1 < size) buf[out] = ' ';
                    out++;
                }
            }
        } else if (*fmt == 'u' || *fmt == 'd') {
            char type = *fmt++;
            unsigned int u;
            if (type == 'd') {
                int d = va_arg(ap, int);
                if (d < 0) {
                    if (out + 1 < size) buf[out] = '-';
                    out++;
                    u = (unsigned int)(-d);
                } else {
                    u = (unsigned int)d;
                }
            } else {
                u = va_arg(ap, unsigned int);
            }
            char tmp[16];
            int ti = 0;
            if (u == 0) {
                tmp[ti++] = '0';
            } else {
                while (u > 0) {
                    tmp[ti++] = '0' + (u % 10);
                    u /= 10;
                }
            }
            while (ti < width) {
                if (out + 1 < size) buf[out] = pad_zero ? '0' : ' ';
                out++;
                width--;
            }
            while (ti > 0) {
                if (out + 1 < size) buf[out] = tmp[--ti];
                out++;
            }
        } else if (*fmt == 'c') {
            fmt++;
            char c = (char)va_arg(ap, int);
            if (out + 1 < size) buf[out] = c;
            out++;
        } else if (*fmt == 'x' || *fmt == 'X') {
            char type = *fmt++;
            unsigned int u = va_arg(ap, unsigned int);
            const char *hex_digits = (type == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char tmp[16];
            int ti = 0;
            if (u == 0) {
                tmp[ti++] = '0';
            } else {
                while (u > 0) {
                    tmp[ti++] = hex_digits[u & 0xF];
                    u >>= 4;
                }
            }
            while (ti < width) {
                if (out + 1 < size) buf[out] = pad_zero ? '0' : ' ';
                out++;
                width--;
            }
            while (ti > 0) {
                if (out + 1 < size) buf[out] = tmp[--ti];
                out++;
            }
        } else {
            if (out + 1 < size) buf[out] = *fmt;
            out++;
            fmt++;
        }
    }
    if (out < size) {
        buf[out] = '\0';
    } else {
        buf[size - 1] = '\0';
    }
    return (int)out;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

char *strtok(char *str, const char *delim) {
    static char *last = NULL;
    if (str != NULL) {
        last = str;
    }
    if (last == NULL) {
        return NULL;
    }
    while (*last) {
        if (strchr(delim, *last) == NULL) break;
        last++;
    }
    if (*last == '\0') {
        last = NULL;
        return NULL;
    }
    char *token_start = last;
    while (*last) {
        if (strchr(delim, *last) != NULL) {
            *last = '\0';
            last++;
            return token_start;
        }
        last++;
    }
    last = NULL;
    return token_start;
}

char *(*_imp__strtok)(char *, const char *) = strtok;

void _assert(const char *msg, const char *file, int line) {
    (void)msg; (void)file; (void)line;
    for (;;) ;
}

void __assert(const char *msg, const char *file, int line) {
    (void)msg; (void)file; (void)line;
    for (;;) ;
}

void (*_imp___assert)(const char *, const char *, int) = __assert;

int __mingw_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int atoi(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s++ - '0');
    }
    return res * sign;
}

long atol(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    long sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    long res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s++ - '0');
    }
    return res * sign;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    size_t needle_len = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

char *strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return (char *)last;
}

int vsscanf(const char *str, const char *fmt, va_list ap) {
    if (!str || !fmt) return 0;
    int count = 0;
    while (*fmt && *str) {
        if (*fmt == ' ') {
            while (*str == ' ' || *str == '\t') str++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (*str != *fmt) return count;
            str++;
            fmt++;
            continue;
        }
        fmt++; // skip '%'
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == 'u' || *fmt == 'd') {
            fmt++;
            while (*str == ' ' || *str == '\t') str++;
            if (*str < '0' || *str > '9') return count;
            unsigned int val = 0;
            while (*str >= '0' && *str <= '9') {
                val = val * 10 + (*str++ - '0');
            }
            unsigned int *p = va_arg(ap, unsigned int *);
            if (p) *p = val;
            count++;
        } else if (*fmt == 's') {
            fmt++;
            while (*str == ' ' || *str == '\t') str++;
            char *p = va_arg(ap, char *);
            int len = 0;
            while (*str && *str != ' ' && *str != '\t' && *str != '\n' && (width == 0 || len < width)) {
                if (p) *p++ = *str;
                str++;
                len++;
            }
            if (p) *p = '\0';
            count++;
        } else {
            return count;
        }
    }
    return count;
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsscanf(str, fmt, ap);
    va_end(ap);
    return ret;
}

int __mingw_sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsscanf(str, fmt, ap);
    va_end(ap);
    return ret;
}

int abs(int j) {
    return (j < 0) ? -j : j;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dest;
}
