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

#endif // EXE32_COMMON_H
