#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <spawn.h>
#include "common.h"
#include "wrappers.h"
#include "fd.h"
#include "paths.h"
#include "memmap.h"

static struct wrapprog_exec_s *wpexec = NULL;
static DIR *find_file_obj = NULL;

#define SET_ERROR_CODE(errcode) *wpexec->wp_errcode_ptr = errcode
#define GET_REAL_FILENO(fd) fileno(fd_fileptrs[fd])

// -- Extra parameters needed for init_first to satisfy the loaded program --
// gcc.out also loads and runs certain programs inside this process's memory just like this exe32 program
// (so that means when gcc.out needs to run as.out, gcc.out loads it inside the memory instead of using spawnvp for reasons,
// which means they both share the same memory as this process) and uses the GCC_PARAMS parameters instead of EXE32_PARAMS.
#define GCC_PARAMS 0x12345678, NULL  /* idk, the 2nd value might also be a wp_exec_info struct */
#define EXE32_PARAMS 0x0, 0x87654321 /* the next 2 parameters are required */

#define IS_VALID_FD(fd) \
    if (fd < 0 || fd >= NUM_FILEPTRS || fd_fileptrs[fd] == NULL) { \
        PRINT_DBG("fd no. %d is invalid or null\n", fd); \
        SET_ERROR_CODE(ERR_INVALID_HANDLE); \
        return -1; \
    }

#define DEFINE_FIXED_PATH(path) char *path##_fixed, *path##_fixed_dup
#define FIX_PATH(path) \
    path##_fixed = malloc(strlen(path) + 1); \
    path##_fixed_dup = path##_fixed; \
    strcpy(path##_fixed, path); \
    strrep_backslashes(path##_fixed); \
    path##_fixed = fix_win_path(path##_fixed); \
    replace_case_path(path##_fixed)

#define FREE_PATH(path) free(path##_fixed_dup)

// TODO: the loaded program changes the stack pointer to the address of init_first function, decide whether or not add a code that restores the stack pointer temporarily before jumping to these wrappers?

CDECL static int realloc_segment_wrapper (uint addr_high) {
    PRINT_DBG("realloc_segment: address 0x%08x\n", addr_high << 12);
    return heap_alloc((void *)(addr_high << 12));
    return 0;
}

CDECL static int open_file_wrapper (char *filename, exe32_fopen_mode mode) {
    // CreateFileA with OPEN_EXISTING flag

    int fdno;
    char *fopen_mode = NULL;
    FILE *fp;
    DEFINE_FIXED_PATH(filename);

    switch (mode) {
        case EXE32_FOPEN_R:
            fopen_mode = "rb";
            break;
        case EXE32_FOPEN_W: 
            fopen_mode = "r+b"; // "wb" truncates the file, and we dont want it to be, so...
            break;
        case EXE32_FOPEN_RW:
            fopen_mode = "r+b";
            break;
    }
    if (fopen_mode == NULL) {
        PRINT_DBG("open_file: invalid access mode %d\n", mode);
        SET_ERROR_CODE(ERR_INVALID_ACCESS);
        return -1;
    }

    FIX_PATH(filename);
    fp = fopen(filename_fixed, fopen_mode);
    if (fp == NULL) {
        PRINT_DBG("open_file: cannot open (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_FILE_NOT_FOUND);
        FREE_PATH(filename);
        return -1;
    }

    fdno = append_fd(fp);
    PRINT_DBG("open_file: Open \"%s\" with flag %d, returned with fd %d\n", filename, mode, fdno);
    FREE_PATH(filename);
    return fdno;
}

CDECL static int create_file_wrapper (char *filename, UNUSED int attrs) {
    // CreateFileA with CREATE_ALWAYS flag

    int fdno;
    FILE *fp;
    DEFINE_FIXED_PATH(filename);

    PRINT_DBG("create_file: Create \"%s\" with attributes %d\n", filename, attrs);

    FIX_PATH(filename);
    fp = fopen(filename_fixed, "wb");
    if (fp == NULL) {
        PRINT_DBG("create_file: cannot write (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND); // ???
        FREE_PATH(filename);
        return -1;
    }

    fdno = append_fd(fp);
    FREE_PATH(filename);
    PRINT_DBG("create_file: returned with fd %d\n", fdno);
    return fdno;
}

CDECL static int write_wrapper (int fd, void *data, ulong size) {
    size_t b_write;

    IS_VALID_FD(fd)
    b_write = fwrite(data, 1, size, fd_fileptrs[fd]);
    fflush(fd_fileptrs[fd]);
    PRINT_DBG("write: written %d bytes at fd %d\n", b_write, fd);
    return b_write;
}

CDECL static int read_wrapper (int fd, void *data, ulong size) {
    size_t b_read;

    IS_VALID_FD(fd)
    b_read = fread(data, 1, size, fd_fileptrs[fd]);
    PRINT_DBG("read: read %d bytes at fd %d\n", b_read, fd);
    return b_read;
}

CDECL static int close_wrapper (int fd) {
    PRINT_DBG("close: closed fd %d\n", fd);
    IS_VALID_FD(fd)

    fclose(fd_fileptrs[fd]);
    fd_fileptrs[fd] = NULL;
    return 0;
}

CDECL static int seek_wrapper (int fd, long offset, int whence) {
    PRINT_DBG("seek: seek fd %d at offset %#lx bytes from whence %d\n", fd, offset, whence);
    long ret_offset;

    IS_VALID_FD(fd)
    if (fseek(fd_fileptrs[fd], offset, whence)) {
        PRINT_DBG("seek: seek error\n");
        SET_ERROR_CODE(ERR_SEEK);
        return -1;
    }
    ret_offset = ftell(fd_fileptrs[fd]);
    return (uint)ret_offset;
}

CDECL static int file_attrs_wrapper (char *filename, UNUSED uint f_attributes, UNUSED int set_attr) {
    int ret = 0;
    struct stat sfile;
    DEFINE_FIXED_PATH(filename);

    FIX_PATH(filename);
    if (!set_attr) {
        PRINT_DBG("file_attrs: get attributes \"%s\"\n", filename);
        if (stat(filename_fixed, &sfile)) {
            PRINT_DBG("file_attrs: file not found!\n");
            SET_ERROR_CODE(ERR_FILE_NOT_FOUND);
            ret = -1;
        }
        else {
            switch (sfile.st_mode & S_IFMT) {
                case S_IFLNK:
                case S_IFREG:
                    PRINT_DBG("file_attrs: is file\n");
                    //ret = FILEATTR_NORMAL;
                    ret = FILEATTR_ARCHIVE;
                    break;
                case S_IFDIR:
                    PRINT_DBG("file_attrs: is directory\n");
                    ret = FILEATTR_DIRECTORY;
                    break;
                default:
                    PRINT_DBG("file_attrs: is not file or dir\n");
                    ret = -1;
            }
        }
    }
    else {
        PRINT_DBG("file_attrs: set attributes \"%s\" with attr = %#x, ignored\n", filename, f_attributes);
    }

    FREE_PATH(filename);
    return ret;
}

CDECL static int set_dta_wrapper (struct unk_dta_s *dta) {
    PRINT_DBG("set_dta: [%p]\n", dta);
    wpexec->wp_filedata = dta;
    return 0;
}

void copy_stat_to_dta(struct stat *st, char *filename) {
    int attr = 0;
    struct unk_dta_s *dta = wpexec->wp_filedata;
    struct tm *time;

    if (S_ISDIR(st->st_mode))
        attr = FILEATTR_DIRECTORY;
    else if (S_ISREG(st->st_mode))
        attr = FILEATTR_ARCHIVE;

    time = localtime(&st->st_mtime);
    dta->attributes = attr;
    dta->datetime.time =
        (time->tm_sec / 2) |
        (time->tm_min << 5) |
        (time->tm_hour << 11);
    dta->datetime.date =
        (time->tm_mday) |
        ((time->tm_mon + 1) << 5) |
        ((time->tm_year - 80) << 9);
    dta->filesize.low = st->st_size & 0xffff;
    dta->filesize.high = st->st_size >> 16;
    strncpy(dta->filename, filename, 255);
}

int copy_dirent_to_dta(struct dirent *dent) {
    struct stat st;
    if (stat(dent->d_name, &st)) {
        PRINT_DBG("> copy_dirent_to_dta: cannot stat (%s)\n", strerror(errno));
        return -1;
    }
    copy_stat_to_dta(&st, dent->d_name);
    return 0;
}

CDECL static int list_file_close_wrapper (void) {
    PRINT_DBG("list_file_close: close\n");
    if (find_file_obj) {
        closedir(find_file_obj);
        find_file_obj = NULL;
    }
    return 0;
}

CDECL static int list_file_wrapper (char *path, UNUSED uint attr_mask) {
    // This function does not set error code
    if (find_file_obj) {
        closedir(find_file_obj);
        find_file_obj = NULL;
    }

    if(!strcmp(".\\*.*", path)) {
        struct dirent *dent;
        PRINT_DBG("list_file: glob pattern (.\\*.*)\n");
        if (!(find_file_obj = opendir("."))) {
            PRINT_DBG("list_file: cannot opendir (%s)\n", strerror(errno));
            return -1;
        }
        if (!(dent = readdir(find_file_obj))) {
            PRINT_DBG("list_file: cannot readdir (%s)\n", strerror(errno));
            return -1;
        }
        PRINT_DBG("list_file: first file: \"%s\"\n", dent->d_name);
        return copy_dirent_to_dta(dent);
    }
    else {
        struct stat spath;
        DEFINE_FIXED_PATH(path);
        FIX_PATH(path);

        PRINT_DBG("list_file: path = \"%s\", attr_mask = 0x%04x\n", path, attr_mask);
        if(stat(path_fixed, &spath)) {
            PRINT_DBG("list_file: cannot stat (%s)\n", strerror(errno));
            free(path_fixed_dup);
            return -1;
        }
        if (S_ISDIR(spath.st_mode)) {
            // TODO: implement opendir
            PRINT_DBG("list_file: is a directory, opendir not implemented\n");
            //free(path_fixed_dup);
            //return -1;
        }
        else if (!S_ISREG(spath.st_mode)) {
            PRINT_DBG("list_file: other types besides file, not implemented\n");
            //free(path_fixed_dup);
            //return -1;
        }

        copy_stat_to_dta(&spath, basename(path_fixed));
        FREE_PATH(path);
    }

    return 0;
}

CDECL static int list_file_next_wrapper (void) {
    struct dirent *dent;
    if (!(dent = readdir(find_file_obj))) {
        //PRINT_DBG("list_file_next: cannot readdir (%s)\n", strerror(errno));
        PRINT_DBG("list_file_next: stop\n");
        return -1;
    }
    PRINT_DBG("list_file_next: next file: \"%s\"\n", dent->d_name);
    return copy_dirent_to_dta(dent);
}

CDECL static int isatty_wrapper (int fd) {
    IS_VALID_FD(fd)
    return isatty(GET_REAL_FILENO(fd)) ? 0x80 : 0x0;
}

CDECL static int get_file_time_wrapper (int fd, struct dos_datetime_s *dos_dt) {
    struct stat fst;
    struct tm *time;

    PRINT_DBG("get_file_time: fd %d\n", fd);
    IS_VALID_FD(fd)

    fstat(GET_REAL_FILENO(fd), &fst);
    time = localtime(&fst.st_mtime);
    dos_dt->time =
        (time->tm_sec / 2) |
        (time->tm_min << 5) |
        (time->tm_hour << 11);
    dos_dt->date =
        (time->tm_mday) |
        ((time->tm_mon + 1) << 5) |
        ((time->tm_year - 80) << 9);
    return 0;
}

CDECL static int get_localtime_wrapper (struct systemtime_s *systemtime) { // might be unused
    time_t tm = time(NULL);
    struct tm *localtm = localtime(&tm);
    PRINT_DBG("get_localtime: %s", asctime(localtm));

    systemtime->year = localtm->tm_year + 1900;
    systemtime->mon  = localtm->tm_mon  + 1;
    systemtime->mday = localtm->tm_mday;
    systemtime->hour = localtm->tm_hour;
    systemtime->min  = localtm->tm_min;
    systemtime->sec  = localtm->tm_sec;
    systemtime->msec = 0; // no miliseconds entry

    return 0;
}

CDECL static int set_file_time_wrapper (UNUSED int fd, UNUSED struct dos_datetime_s *dos_dt) { // might be unused
    PRINT_DBG("set_file_time: NOT IMPLEMENTED!\n");  // TODO
    return -1;
}

CDECL static int mkdir_wrapper (char *dirname) {
    int ret = 0;
    DEFINE_FIXED_PATH(dirname);
    FIX_PATH(dirname);

    PRINT_DBG("mkdir: \"%s\"\n", dirname);
    if(mkdir(dirname_fixed, 0777)) {
        PRINT_DBG("mkdir: cannot mkdir (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        ret = -1;
    }

    FREE_PATH(dirname);
    return ret;
}

CDECL static int rmdir_wrapper (char *dirname) {
    int ret = 0;
    DEFINE_FIXED_PATH(dirname);
    FIX_PATH(dirname);

    PRINT_DBG("rmdir: \"%s\"\n", dirname);
    if(rmdir(dirname_fixed)) {
        PRINT_DBG("rmdir: cannot rmdir (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        ret = -1;
    }

    FREE_PATH(dirname);
    return ret;
}

CDECL static int remove_wrapper (char *path) {
    int ret = 0;
    DEFINE_FIXED_PATH(path);
    FIX_PATH(path);

    PRINT_DBG("remove: unlink \"%s\"\n", path);
    if ((ret = remove(path_fixed))) {
        PRINT_DBG("remove: cannot unlink (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_FILE_NOT_FOUND); // copied
        ret = -1;
    }

    FREE_PATH(path);
    return ret;
}

CDECL static int rename_wrapper (char *oldpath, char *newpath) {
    int ret = 0;
    DEFINE_FIXED_PATH(oldpath);
    DEFINE_FIXED_PATH(newpath);
    FIX_PATH(oldpath);
    FIX_PATH(newpath);

    PRINT_DBG("rename: move \"%s\" to \"%s\"\n", oldpath, newpath);
    if (rename(oldpath_fixed, newpath_fixed)) {
        PRINT_DBG("rename: cannot mv (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        ret = -1;
    }

    FREE_PATH(oldpath);
    FREE_PATH(newpath);
    return ret;
}

CDECL static int chdrive_wrapper (char *new_cwd, UNUSED char drive_num) {
    PRINT_DBG("chdrive: new_cwd ptr = %p, drive %c\n", new_cwd, drive_num + 'A');

#if 0
    //new_cwd[0] = drive_num + 'A';
    new_cwd[0] = 'C';
    new_cwd[1] = ':';
    new_cwd[2] = '\\';
    if (!getcwd(new_cwd + 3, MAX_FILEPATH - 3)) {
        PRINT_DBG("chg_drive: error getting current dir\n");
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        return -1;
    }
    strrep_forwslashes(new_cwd + 3);
#else
    if (!getcwd(new_cwd, MAX_FILEPATH)) {
        PRINT_DBG("chdrive: error getting current dir (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        return -1;
    }
    strrep_forwslashes(new_cwd);
#endif
    return 0;
}

CDECL static int chdir_wrapper (char *dirname) {
    int ret = 0;
    DEFINE_FIXED_PATH(dirname);
    FIX_PATH(dirname);

    PRINT_DBG("chdir: dirname = %s\n", dirname);
    if (chdir(dirname_fixed)) {
        PRINT_DBG("chdir: chdir error (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        ret = -1;
    }

    FREE_PATH(dirname);
    return ret;
}

CDECL static int getdrive_wrapper (void) {
    PRINT_DBG("getdrive: return value 0x3 (C drive)\n");
    return 'C' - 'A'; /* just return the fake C drive ;) */
}

void print_flatenv(char *env, char *file) {
    FILE *fp = fopen(file, "wb");
    if (!fp) {
        PRINT_DBG("> printenv: error opening file\n");
        return;
    }

    do {
        fputs(env, fp);
        fputc('\n', fp);
    } while (*(env += strlen(env) + 1));

    fclose(fp);
}

char **build_env_array(char *env) {
    char *envptr = env, **env_array;
    int envvar_count = 0, i;
    do {
        envvar_count++;
    } while (*(envptr += strlen(envptr) + 1));
    env_array = calloc(envvar_count+1, sizeof(char *));
    envptr = env;

    for (i = 0; i < envvar_count ; i++) {
        env_array[i] = envptr;
        envptr += strlen(envptr) + 1;
    }

    return env_array;
}

char **build_argv(char *progname, int *argcp, char *args) {
    // TODO: add quoted entries
    char *args_fixed = strdup(args), **ret_argv;
    char *token = strtok(args_fixed, " \t\n");

    if (token) {
        int argc = 1;

        do {
            argc++;
        } while ((token = strtok(NULL, " \t\n")));
        free(args_fixed);

        ret_argv = calloc(argc+1, sizeof(char *));
        argc = 0;
        ret_argv[argc++] = progname;

        token = strtok(args, " \t\n");
        do {
            ret_argv[argc++] = token;
        } while ((token = strtok(NULL, " \t\n")));

        *argcp = argc;
    }
    else {
        free(args_fixed);
        ret_argv = calloc(2, sizeof(char *));
        ret_argv[0] = progname;
        *argcp = 1;
    }

    return ret_argv;
}

static int return_code;

CDECL static int spawnve_wrapper (char *progname, struct exec_s *exec_info) {
    // This function only does is to execute the program and wait for it to finish.

    int exec_argc, ret = 0;
    char *args = strdup(exec_info->args + (exec_info->args[1] == ' ' ? 2 : 1));
    char **exec_env = build_env_array(exec_info->env), **exec_argv, *exec_wpname = NULL;
    DEFINE_FIXED_PATH(progname);
    DEFINE_FIXED_PATH(exec_wpname);
    args[strlen(args)-1] = '\0';
    FIX_PATH(progname);

    PRINT_DBG("spawnve: progname = \"%s\", args = \"%s\"\n", progname, args);
    exec_argv = build_argv(progname_fixed, &exec_argc, args);

    if (!strcmp(basename(progname_fixed), "exew32.exe")) {
        exec_wpname = exec_argv[1];
        FIX_PATH(exec_wpname);
        exec_argv[1] = exec_wpname_fixed;
    }

    {
        pid_t pid;
        int status = 0;

        if (posix_spawn(&pid, progname_fixed, NULL, NULL, exec_argv, exec_env)) {
            PRINT_DBG("spawnve: cannot spawn (%s)\n", strerror(errno));
            ret = -1;
            goto spawnve_free;
        }
        PRINT_DBG("spawnve: child PID: %d\n", pid);
        do {
            if (waitpid(pid, &status, 0) == -1) {
                PRINT_DBG("spawnve: waitpid returns an error! (%s)\n", strerror(errno));
                ret = -1;
                break;
            }
            else if (WIFEXITED(status)) {
                PRINT_DBG("spawnve: child PID %d exited with status %d\n", pid, WEXITSTATUS(status));
                return_code = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status)) {
                PRINT_ERR("spawnve: child PID %d aborted with signal %s%s\n", pid, strsignal(WTERMSIG(status)),
                        WCOREDUMP(status) ? " (core dumped)" : "");
                return_code = 255;
            }
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

spawnve_free:
    if (exec_wpname) FREE_PATH(exec_wpname);
    free(exec_env);
    free(exec_argv);
    FREE_PATH(progname);
    free(args);
    return ret;
}

CDECL static int get_return_code_wrapper (void) {
    return return_code;
}

CDECL static int dup_wrapper (int fd) {
    int ret;
    IS_VALID_FD(fd)

    ret = append_fd(fd_fileptrs[fd]);
    PRINT_DBG("dup: duplicate fd %d to %d\n", fd, ret);
    return ret;
}

CDECL static int dup2_wrapper (int src_fd, int dest_fd) {
    PRINT_DBG("dup: duplicate fd %d to %d\n", src_fd, dest_fd);
    IS_VALID_FD(src_fd)
    IS_VALID_FD(dest_fd)

    if (fd_fileptrs[dest_fd]) fclose(fd_fileptrs[dest_fd]);
    fd_fileptrs[dest_fd] = fd_fileptrs[src_fd];

    return dest_fd;
}

CDECL static int get_dos_version_wrapper (void) {
    return 5;
}

CDECL static void exit_wrapper (int status) {
    exit(status);
}

CDECL static int direct_stdin_wrapper (void) {  // unused?
    return 0;
}

CDECL static void sleep_wrapper (long time_msec) { // unused on some programs
    struct timespec ts;

    ts.tv_sec = time_msec / 1000;
    ts.tv_nsec = time_msec % 1000 * 1000000;

    nanosleep(&ts, &ts);
}

func_wrapper io_wrappers[] = {
    (func_wrapper) realloc_segment_wrapper,   /*  0 */
    (func_wrapper) open_file_wrapper,         /*  1 */
    (func_wrapper) create_file_wrapper,       /*  2 */
    (func_wrapper) write_wrapper,             /*  3 */
    (func_wrapper) read_wrapper,              /*  4 */
    (func_wrapper) close_wrapper,             /*  5 */
    (func_wrapper) seek_wrapper,              /*  6 */
    (func_wrapper) file_attrs_wrapper,        /*  7 */
    (func_wrapper) set_dta_wrapper,           /*  8 */
    (func_wrapper) list_file_wrapper,         /*  9 */
    (func_wrapper) list_file_next_wrapper,    /* 10 */
    (func_wrapper) list_file_close_wrapper,   /* 11 */
    (func_wrapper) isatty_wrapper,            /* 12 */
    (func_wrapper) get_file_time_wrapper,     /* 13 */
    (func_wrapper) get_localtime_wrapper,     /* 14 */
    (func_wrapper) set_file_time_wrapper,     /* 15 */
    (func_wrapper) mkdir_wrapper,             /* 16 */
    (func_wrapper) rmdir_wrapper,             /* 17 */
    (func_wrapper) remove_wrapper,            /* 18 */
    (func_wrapper) rename_wrapper,            /* 19 */
    (func_wrapper) chdrive_wrapper,           /* 20 */
    (func_wrapper) chdir_wrapper,             /* 21 */
    (func_wrapper) getdrive_wrapper,          /* 22 */
    (func_wrapper) spawnve_wrapper,           /* 23 */
    (func_wrapper) get_return_code_wrapper,   /* 24 */
    (func_wrapper) dup_wrapper,               /* 25 */
    (func_wrapper) dup2_wrapper,              /* 26 */
    (func_wrapper) get_dos_version_wrapper,   /* 27 */
    (func_wrapper) exit_wrapper,              /* 28 */
    (func_wrapper) direct_stdin_wrapper,      /* 29 */
    (func_wrapper) sleep_wrapper,             /* 30 */
    (func_wrapper) NULL
};

void exec_init_first(init_first_t init_first, struct wrapprog_exec_s *exec_info) {
    wpexec = exec_info;
	(*init_first)(EXE32_PARAMS, io_wrappers, exec_info);
}
