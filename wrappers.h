#ifndef EXE32_WRAPPERS_H
#define EXE32_WRAPPERS_H

#include <stdint.h>
#include "common.h"

#define CDECL __attribute__((cdecl))
#define REGPARM2 __attribute__((regparm(2)))

typedef void (*func_wrapper)(void);

#define MAX_FILEPATH 1024

typedef enum exe32_win32_errcode {
    ERR_SUCCESS,
    ERR_INVALID_FUNCTION,    /* #1 Invalid function number */
    ERR_FILE_NOT_FOUND,      /* #2 File not found */
    ERR_PATH_NOT_FOUND,      /* #3 Path not found */
    ERR_TOO_MANY_OPEN_FILES, /* #4 Too many open files */
    ERR_ACCESS_DENIED,       /* #5 Permission denied */
    ERR_INVALID_HANDLE,      /* #6 Invalid file handle */
    ERR_ARENA_TRASHED,       /* #7 Memory blocks destroyed */
    ERR_NOT_ENOUGH_MEMORY,   /* #8 Not enough Memory */
    ERR_INVALID_BLOCK,       /* #9 Invalid memory address */
    ERR_BAD_ENVIRONMENT,     /* #10 Invalid environment */
    ERR_BAD_FORMAT,          /* #11 Invalid format */
    ERR_INVALID_ACCESS,      /* #12 Invalid access code */
    ERR_INVALID_DATA,        /* #13 Invalid data */
    ERR_OUTOFMEMORY,         /* #14 ???? */
    ERR_INVALID_DRIVE,       /* #15 Invalid drive name */
    ERR_CURRENT_DIRECTORY,   /* #16 Attempted to remove current directory */
    ERR_NOT_SAME_DEVICE,     /* #17 Invalid device name */
    ERR_NO_MORE_FILES,       /* #18 No more files */
    ERR_WRITE_PROTECT,       /* #19 Invalid argument/Disk write protected */
    ERR_BAD_UNIT,            /* #20 Invalid disk unit number */
    ERR_NOT_READY,           /* #21 Execution error/Drive not ready */
    ERR_BAD_COMMAND,         /* #22 Invalid disk command */
    ERR_CRC,                 /* #23 CRC error */
    ERR_BAD_LENGTH,          /* #24 Command packet length error */
    ERR_SEEK,                /* #25 seek error */
    ERR_NOT_DOS_DISK,        /* #26 Invalid disk format */
    ERR_SECTOR_NOT_FOUND,    /* #27 Not found sector */
    ERR_OUT_OF_PAPER,        /* #28 Out of paper */
    ERR_WRITE_FAULT,         /* #29 Write error */
    ERR_READ_FAULT,          /* #30 Read error */
    ERR_GEN_FAILURE,         /* #31 General error */
    ERR_SHARING_VIOLATION,   /* #32 */
    ERR_LOCK_VIOLATION,      /* #33 Math argument */
    ERR_WRONG_DISK,          /* #34 Result too large */
} win32_errcode;

typedef enum exe32_fopen_mode {
    EXE32_FOPEN_R,
    EXE32_FOPEN_W,
    EXE32_FOPEN_RW,
} exe32_fopen_mode;

/* Win32 File attribute bitfields */
#define FILEATTR_READONLY     (1 << 0)    /* 0x0001 */
#define FILEATTR_HIDDEN       (1 << 1)    /* 0x0002 */
#define FILEATTR_SYSTEM       (1 << 2)    /* 0x0004 */
#define FILEATTR_DIRECTORY    (1 << 4)    /* 0x0010 */
#define FILEATTR_ARCHIVE      (1 << 5)    /* 0x0020 */
#define FILEATTR_DEVICE       (1 << 6)    /* 0x0040 */
#define FILEATTR_NORMAL       (1 << 7)    /* 0x0080 */
#define FILEATTR_TEMPORARY    (1 << 8)    /* 0x0100 */
#define FILEATTR_SPARSEFILE   (1 << 9)    /* 0x0200 */
#define FILEATTR_REPARSEPOINT (1 << 10)   /* 0x0400 */
#define FILEATTR_COMPRESSED   (1 << 11)   /* 0x0800 */
#define FILEATTR_OFFLINE      (1 << 12)   /* 0x1000 */
#define FILEATTR_NOTINDEXED   (1 << 13)   /* 0x2000 */
#define FILEATTR_ENCRYPTED    (1 << 14)   /* 0x4000 */

/* DOS Date-Time format:
 *   Date: +-------------+---+---+---+---+---+---+---+---+---+---+----+----+----+----+----+----+
 *         | Bits        | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
 *         +-------------+---+---+---+---+---+---+---+---+---+---+----+----+----+----+----+----+
 *         | Description |    Day of month   |     Month     |        Years since 1980         |
 *         +-------------+-------------------+---------------+---------------------------------+
 *     
 *   Time: +-------------+---+---+---+---+---+---+---+---+---+---+----+----+----+----+----+----+
 *         | Bits        | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
 *         +-------------+---+---+---+---+---+---+---+---+---+---+----+----+----+----+----+----+
 *         | Description |    Seconds / 2    |         Minutes        |          Hours         |
 *         +-------------+-------------------+------------------------+------------------------+
 */
struct dos_datetime_s {
    ushort date;
    ushort time;
};

struct unk_dta_s {
    char search_attr;
    char search_drive;
    char search_name[11];
    uint16_t direntry_num;
    uint16_t cwd_clus_num;
    uint32_t reserved;
    char attributes;
    struct dos_datetime_s datetime;
    struct {
        uint16_t low;
        uint16_t high;
    } filesize;
    //char filename[13];
    char filename[256];
} __attribute__((packed)); /* Adding the packed attribute makes the loaded program understand */

struct systemtime_s {
    uint year;
    uint mon;
    uint mday;
    uint hour;
    uint min;
    uint sec;
    uint msec;
};

struct exec_s {
    char *env;
    ushort segment;
    char *args;
} __attribute__((packed));

typedef struct wrapprog_exec_s {
    void *wp_heap_start;
    int  *wp_errcode_ptr;
    char *wp_name;
    char *wp_args;
    char *wp_environ;
    struct unk_dta_s *wp_filedata;
} wrapprog_exec;

typedef REGPARM2 void (*init_first_gcc)(uint, void *);
typedef REGPARM2 void (*init_first_exe32)(uint, uint,
		func_wrapper[], struct wrapprog_exec_s*);

typedef init_first_exe32 init_first_t;

void exec_init_first(init_first_t, struct wrapprog_exec_s *);

#endif // EXE32_WRAPPERS_H
