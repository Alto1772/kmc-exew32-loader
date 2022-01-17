#ifndef EXE32_COMMON_H
#define EXE32_COMMON_H

#include <string.h>

typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

__attribute__((format(printf,1,2))) void write_log(const char *, ...);

#define UNUSED __attribute__((unused))

#define PRINT_ERR(...) fprintf(stderr, __VA_ARGS__)

#ifndef NDEBUG
#define PRINT_DBG(...) write_log(__VA_ARGS__)
#else
#define PRINT_DBG(...)
#endif

#define PRINT_DBG_FUNC(fmt, ...) PRINT_DBG(__func__ ": " fmt, __VA_ARGS__)
#define PRINT_DBG_INTFUNC(fmt, ...) PRINT_DBG("> " __func__ ": " fmt, __VA_ARGS__)

// Very simple implementation that does not modify its characters
static inline char *basename(char *path) {
    char *base_path = strrchr(path, '/');
    if (base_path) return base_path + 1;
    else return path;
}



static inline void strrep(char *str, char csrc, char cdes) {
    while ((str = strchr(str, csrc))) {
        *str++ = cdes;
    }
}

#define strrep_backslashes(str) strrep(str, '\\', '/')
#define strrep_forwslashes(str) strrep(str, '/', '\\')

static inline void strnrep(char *str, char csrc, char cdes, int len) {
    char *curstr = str;
    while (len > 0 && (curstr = strchr(str, csrc))) {
        *curstr++ = cdes;
        len -= curstr - str;
        str = curstr;
    }
}

#define strnrep_backslashes(str, len) strnrep(str, '\\', '/', len)
#define strnrep_forwslashes(str, len) strnrep(str, '/', '\\', len)

#include <stdlib.h>

static inline char *concat(const char *cp1, const char *cp2) {
    char *catstr = malloc(strlen(cp1) + strlen(cp2) + 1);
    strcpy(catstr, cp1);
    strcat(catstr, cp2);

    return catstr;
}

static inline char *concat3(const char *cp1, const char *cp2, const char *cp3) {
    char *catstr = malloc(strlen(cp1) + strlen(cp2) + strlen(cp3) + 1);
    strcpy(catstr, cp1);
    strcat(catstr, cp2);
    strcat(catstr, cp3);

    return catstr;
}

static inline char *concat4(const char *cp1, const char *cp2, const char *cp3, const char *cp4) {
    char *catstr = malloc(strlen(cp1) + strlen(cp2) + strlen(cp3) + strlen(cp4) + 1);
    strcpy(catstr, cp1);
    strcat(catstr, cp2);
    strcat(catstr, cp3);
    strcat(catstr, cp4);

    return catstr;
}

#if 0
#include <stddef.h>
#include <stdarg.h>

static inline char *concatn(const char num, ...) {
    char *catstr;
    size_t catsize = 0;
    int i;
    va_list ap;

    va_start(ap, num);
    for (i = 0; i < num; i++) catsize += strlen(va_arg(ap, char *));
    va_end(ap);

    catstr = malloc(catsize + 1);

    va_start(ap, num);
    for (i = 0; i < num; i++) {
        if (i == 0) strcpy(catstr, va_arg(ap, char *));
        else        strcat(catstr, va_arg(ap, char *));
    }
    va_end(ap);

    return catstr;
}
#endif

#endif // EXE32_COMMON_H
