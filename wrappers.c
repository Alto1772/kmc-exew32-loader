#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
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

static struct wrapprog_exec_s *wpexec = NULL;
static DIR *find_file_obj = NULL;

#define SET_ERROR_CODE(errcode) *wpexec->wp_errcode_ptr = errcode
#define GET_REAL_FILENO(fd) fileno(fd_fileptrs[fd])

#define FUNCWRAPPER(func) CDECL static int func##_wrapper
#define FUNCWRAPPER_RET(rettyp, func) CDECL static rettyp func##_wrapper
#define FUNCWRAPPER_ARRDEF(func) (func_wrapper)func##_wrapper
#define NOT_IMPLEMENTED(func, ...) \
    CDECL static void func##_wrapper(void) { \
        PRINT_DBG(#func ": NOT IMPLEMENTED!\n"); \
        assert(0 && "Not Implemented:" #func); \
    }

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

#define DEFINE_FIXED_PATH(path) char *path##_tmp, *path##_tmp_dup
#define FIX_PATH(path) \
    path##_tmp = malloc(strlen(path) + 1); \
    path##_tmp_dup = path##_tmp; \
    strcpy(path##_tmp, path); \
    strrep_backslashes(path##_tmp); \
    path##_tmp = fix_win_path(path##_tmp); \
    replace_case_path(path##_tmp)

#define FREE_PATH(path) free(path##_tmp_dup)

// TODO: the loaded program changes the stack pointer to the address of init_first function, decide whether or not add a code that restores the stack pointer temporarily before jumping to these wrappers?

FUNCWRAPPER(realloc_segment) (UNUSED uint addr_high) {
    // we don't need this because we've allocated enough space for the loaded program
    PRINT_DBG("realloc_segment: address 0x%08x\n", addr_high << 12);
    return 0;
}

FUNCWRAPPER(open_file) (char *filename, exe32_fopen_mode mode) {
    int fdno;
    char *fopen_mode = NULL;
    FILE *fp;
    DEFINE_FIXED_PATH(filename);

    switch (mode) {
        case EXE32_FOPEN_R:
            fopen_mode = "rb";
            break;
        case EXE32_FOPEN_W:
            fopen_mode = "wb";
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
    fp = fopen(filename_tmp, fopen_mode);
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

FUNCWRAPPER(create_file) (char *filename, UNUSED int attrs) {
    int fdno;
    FILE *fp;
    DEFINE_FIXED_PATH(filename);

    PRINT_DBG("create_file: Create \"%s\" with attributes %d\n", filename, attrs);

    FIX_PATH(filename);
    fp = fopen(filename_tmp, "wb");
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

FUNCWRAPPER(write) (int fd, void *data, ulong size) {
    size_t b_write;

    IS_VALID_FD(fd)
    b_write = fwrite(data, 1, size, fd_fileptrs[fd]);
    fflush(fd_fileptrs[fd]);
    PRINT_DBG("write: written %d bytes at fd %d\n", b_write, fd);
    return b_write;
}

FUNCWRAPPER(read) (int fd, void *data, ulong size) {
    size_t b_read;

    IS_VALID_FD(fd)
    b_read = fread(data, 1, size, fd_fileptrs[fd]);
    PRINT_DBG("read: read %d bytes at fd %d\n", b_read, fd);
    return b_read;
}

FUNCWRAPPER(close) (int fd) {
    PRINT_DBG("close: closed fd %d\n", fd);
    IS_VALID_FD(fd)

    fclose(fd_fileptrs[fd]);
    fd_fileptrs[fd] = NULL;
    return 0;
}

FUNCWRAPPER(seek) (int fd, long offset, int whence) {
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

FUNCWRAPPER(file_attrs) (char *filename, UNUSED uint f_attributes, UNUSED int set_attr) {
    int ret = 0;
    struct stat sfile;
    DEFINE_FIXED_PATH(filename);

    FIX_PATH(filename);
    if (!set_attr) {
        PRINT_DBG("file_attrs: get attributes \"%s\"\n", filename);
        if (stat(filename_tmp, &sfile)) {
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

FUNCWRAPPER(set_dta) (struct unk_dta_s *dta) {
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
        (time->tm_mon << 5) |
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

FUNCWRAPPER(list_file_close) (void) {
    PRINT_DBG("list_file_close: close\n");
    if (find_file_obj) {
        closedir(find_file_obj);
        find_file_obj = NULL;
    }
    return 0;
}

FUNCWRAPPER(list_file) (char *path, UNUSED uint attr_mask) {
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
        if(stat(path_tmp, &spath)) {
            PRINT_DBG("list_file: cannot stat (%s)\n", strerror(errno));
            free(path_tmp_dup);
            return -1;
        }
        if (S_ISDIR(spath.st_mode)) {
            // TODO: implement opendir
            PRINT_DBG("list_file: is a directory, opendir not implemented\n");
            //free(path_tmp_dup);
            //return -1;
        }
        else if (!S_ISREG(spath.st_mode)) {
            PRINT_DBG("list_file: other types besides file, not implemented\n");
            //free(path_tmp_dup);
            //return -1;
        }

        copy_stat_to_dta(&spath, basename(path_tmp));
        FREE_PATH(path);
    }

    return 0;
}

FUNCWRAPPER(list_file_next) (void) {
    struct dirent *dent;
    if (!(dent = readdir(find_file_obj))) {
        //PRINT_DBG("list_file_next: cannot readdir (%s)\n", strerror(errno));
        PRINT_DBG("list_file_next: stop\n");
        return -1;
    }
    PRINT_DBG("list_file_next: next file: \"%s\"\n", dent->d_name);
    return copy_dirent_to_dta(dent);
}

FUNCWRAPPER(isatty) (int fd) {
    IS_VALID_FD(fd)
    return isatty(GET_REAL_FILENO(fd)) ? 0x80 : 0x0;
}

FUNCWRAPPER(get_file_time) (int fd, struct dos_datetime_s *dos_dt) {
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
        (time->tm_mon << 5) |
        ((time->tm_year - 80) << 9);
    return 0;
}

NOT_IMPLEMENTED(get_localtime,   struct systemtime_s systemtime)
NOT_IMPLEMENTED(set_file_time,   int fd, struct dos_datetime_s *dos_dt)

FUNCWRAPPER(mkdir) (char *dirname) {
    int ret = 0;
    DEFINE_FIXED_PATH(dirname);
    FIX_PATH(dirname);

    PRINT_DBG("mkdir: \"%s\"\n", dirname);
    if(mkdir(dirname_tmp, 0777)) {
        PRINT_DBG("mkdir: cannot mkdir (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        ret = -1;
    }

    FREE_PATH(dirname);
    return ret;
}

FUNCWRAPPER(rmdir) (char *dirname) {
    int ret = 0;
    DEFINE_FIXED_PATH(dirname);
    FIX_PATH(dirname);

    PRINT_DBG("rmdir: \"%s\"\n", dirname);
    if(rmdir(dirname_tmp)) {
        PRINT_DBG("rmdir: cannot rmdir (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        ret = -1;
    }

    FREE_PATH(dirname);
    return ret;
}

FUNCWRAPPER(remove) (char *path) {
    int ret = 0;
    DEFINE_FIXED_PATH(path);
    FIX_PATH(path);

    PRINT_DBG("remove: unlink \"%s\"\n", path);
    if ((ret = remove(path_tmp))) {
        PRINT_DBG("remove: cannot unlink (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_FILE_NOT_FOUND); // copied
        ret = -1;
    }

    FREE_PATH(path);
    return ret;
}

FUNCWRAPPER(rename) (char *oldpath, char *newpath) {
    int ret = 0;
    DEFINE_FIXED_PATH(oldpath);
    DEFINE_FIXED_PATH(newpath);
    FIX_PATH(oldpath);
    FIX_PATH(newpath);

    PRINT_DBG("rename: move \"%s\" to \"%s\"\n", oldpath, newpath);
    if (rename(oldpath_tmp, newpath_tmp)) {
        PRINT_DBG("rename: cannot mv (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        ret = -1;
    }

    FREE_PATH(oldpath);
    FREE_PATH(newpath);
    return ret;
}

FUNCWRAPPER(chdrive) (char *new_cwd, UNUSED char drive_num) {
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

FUNCWRAPPER(chdir) (char *dirname) {
    int ret = 0;
    DEFINE_FIXED_PATH(dirname);
    FIX_PATH(dirname);

    PRINT_DBG("chdir: dirname = %s\n", dirname);
    if (chdir(dirname_tmp)) {
        PRINT_DBG("chdir: chdir error (%s)\n", strerror(errno));
        SET_ERROR_CODE(ERR_PATH_NOT_FOUND);
        ret = -1;
    }

    FREE_PATH(dirname);
    return ret;
}

FUNCWRAPPER(getdrive) (void) {
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
    char *args_tmp = strdup(args), **ret_argv;
    char *token = strtok(args_tmp, " \t\n");

    if (token) {
        int argc = 1;

        do {
            argc++;
        } while ((token = strtok(NULL, " \t\n")));
        free(args_tmp);

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
        free(args_tmp);
        ret_argv = calloc(2, sizeof(char *));
        ret_argv[0] = progname;
        *argcp = 1;
    }

    return ret_argv;
}

static int return_code;

FUNCWRAPPER(spawnve) (char *progname, struct exec_s *exec_info) {
    // This function only does is to execute the program and wait for it to finish.

    int exec_argc, ret = 0;
    char *args = strdup(exec_info->args + (exec_info->args[1] == ' ' ? 2 : 1));
    char **exec_env = build_env_array(exec_info->env), **exec_argv, *exec_wpname = NULL;
    DEFINE_FIXED_PATH(progname);
    DEFINE_FIXED_PATH(exec_wpname);
    args[strlen(args)-1] = '\0';
    FIX_PATH(progname);

    PRINT_DBG("spawnve: progname = \"%s\", args = \"%s\"\n", progname, args);
    exec_argv = build_argv(progname_tmp, &exec_argc, args);

    if (!strcmp(basename(progname_tmp), "exew32.exe")) {
        exec_wpname = exec_argv[1];
        FIX_PATH(exec_wpname);
        exec_argv[1] = exec_wpname_tmp;
    }

    {
        pid_t pid;
        int status = 0;

        if (posix_spawn(&pid, progname_tmp, NULL, NULL, exec_argv, exec_env)) {
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

FUNCWRAPPER(get_return_code) (void) {
    return return_code;
}

FUNCWRAPPER(dup) (int fd) {
    int ret;
    IS_VALID_FD(fd)

    ret = append_fd(fd_fileptrs[fd]);
    PRINT_DBG("dup: duplicate fd %d to %d\n", fd, ret);
    return ret;
}

FUNCWRAPPER(dup2) (int src_fd, int dest_fd) {
    PRINT_DBG("dup: duplicate fd %d to %d\n", src_fd, dest_fd);
    IS_VALID_FD(src_fd)
    IS_VALID_FD(dest_fd)

    if (fd_fileptrs[dest_fd]) fclose(fd_fileptrs[dest_fd]);
    fd_fileptrs[dest_fd] = fd_fileptrs[src_fd];

    return dest_fd;
}

FUNCWRAPPER(get_dos_version) (void) {
    return 5;
}

FUNCWRAPPER_RET(void, exit) (int status) {
    exit(status);
}

FUNCWRAPPER(direct_stdin) (void) {  // unused?
    return 0;
}

FUNCWRAPPER_RET(void, sleep) (long time_msec) { // unused on some programs
    struct timespec ts;

    ts.tv_sec = time_msec / 1000;
    ts.tv_nsec = time_msec % 1000 * 1000000;

    nanosleep(&ts, &ts);
}

func_wrapper io_wrappers[] = {
    /*  0 */ FUNCWRAPPER_ARRDEF(realloc_segment),
    /*  1 */ FUNCWRAPPER_ARRDEF(open_file),
    /*  2 */ FUNCWRAPPER_ARRDEF(create_file),
    /*  3 */ FUNCWRAPPER_ARRDEF(write),
    /*  4 */ FUNCWRAPPER_ARRDEF(read),
    /*  5 */ FUNCWRAPPER_ARRDEF(close),
    /*  6 */ FUNCWRAPPER_ARRDEF(seek),
    /*  7 */ FUNCWRAPPER_ARRDEF(file_attrs),
    /*  8 */ FUNCWRAPPER_ARRDEF(set_dta),
    /*  9 */ FUNCWRAPPER_ARRDEF(list_file),
    /* 10 */ FUNCWRAPPER_ARRDEF(list_file_next),
    /* 11 */ FUNCWRAPPER_ARRDEF(list_file_close),
    /* 12 */ FUNCWRAPPER_ARRDEF(isatty),
    /* 13 */ FUNCWRAPPER_ARRDEF(get_file_time),
    /* 14 */ FUNCWRAPPER_ARRDEF(get_localtime),
    /* 15 */ FUNCWRAPPER_ARRDEF(set_file_time),
    /* 16 */ FUNCWRAPPER_ARRDEF(mkdir),
    /* 17 */ FUNCWRAPPER_ARRDEF(rmdir),
    /* 18 */ FUNCWRAPPER_ARRDEF(remove),
    /* 19 */ FUNCWRAPPER_ARRDEF(rename),
    /* 20 */ FUNCWRAPPER_ARRDEF(chdrive),
    /* 21 */ FUNCWRAPPER_ARRDEF(chdir),
    /* 22 */ FUNCWRAPPER_ARRDEF(getdrive),
    /* 23 */ FUNCWRAPPER_ARRDEF(spawnve),
    /* 24 */ FUNCWRAPPER_ARRDEF(get_return_code),
    /* 25 */ FUNCWRAPPER_ARRDEF(dup),
    /* 26 */ FUNCWRAPPER_ARRDEF(dup2),
    /* 27 */ FUNCWRAPPER_ARRDEF(get_dos_version),
    /* 28 */ FUNCWRAPPER_ARRDEF(exit),
    /* 29 */ FUNCWRAPPER_ARRDEF(direct_stdin),
    /* 30 */ FUNCWRAPPER_ARRDEF(sleep),
    (func_wrapper)NULL
};

void exec_init_first(init_first_t init_first, struct wrapprog_exec_s *exec_info) {
    wpexec = exec_info;
	(*init_first)(EXE32_PARAMS, io_wrappers, exec_info);
}
