#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include "common.h"
#include "load.h"
#include "paths.h"

#ifndef EXEPROGNAME
#define EXEPROGNAME "exe32-linux"
#endif
#ifndef EXEPROGVER
#define EXEPROGVER "unknown"
#endif

char *exe32_dirpath = NULL;
int is_exe32 = 0;
int exe32_lock = 0;

static char *wp_progname;
static char *wp_args;
static char *wp_environ;

static void init_allocate(void) {
    void *memmap;
    
    memmap = mmap((void*)0x01000000, 0x10000000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0x0);
    if (memmap == NULL) {
        printf(EXEPROGNAME": Cannot allocate virtual memory address at 0x01000000!\n");
        exit(10);
    }
}

#define LOCKNAME ".exe32-lock"
#define MAX_READ_RETRIES 100000000

static char *lock_path;

void lock_wait(void) {
    int lock_fd;
    char *tmpdir, pidstr[11];

    if (!exe32_lock) return;

    if ((tmpdir = getenv("TMPDIR")) == NULL)
        if ((tmpdir = getenv("TEMP")) == NULL)
            tmpdir = ".";

    lock_path = malloc(strlen(tmpdir) + sizeof(LOCKNAME) + 1);
    strcpy(lock_path, tmpdir);
    strcat(lock_path, "/");
    strcat(lock_path, LOCKNAME);

    do {
        lock_fd = open(lock_path, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (lock_fd == -1) {
            if (errno == EEXIST) {
                lock_fd = open(lock_path, O_RDONLY);
                if (lock_fd == -1) {
                    if (errno != ENOENT) {
                        PRINT_DBG("> lock_wait: Error creating lock file \"%s\", (%s). Disabled cross-process locking.\n", lock_path, strerror(errno));
                        exe32_lock = 0;
                        return;
                    }
                }
                else {
                    ssize_t bread; // :like:
                    unsigned long pidnum, read_retries = 0;
                    memset(pidstr, 0, 11);

                    do {
                        bread = read(lock_fd, pidstr, 10);
                    } while (read_retries++ < MAX_READ_RETRIES && (bread == 0 || pidstr[0] == '\0'));
                    if (read_retries >= MAX_READ_RETRIES) { // failsafe if process didn't give us pid
                        PRINT_DBG("> lock_wait: read retries exceeded to max. Disabled cross-process locking.\n");
                        close(lock_fd);
                        exe32_lock = 0;
                        return;
                    }

                    pidnum = strtoul(pidstr, NULL, 10);
                    if (kill(pidnum, 0) == 0) {
                        PRINT_DBG("> lock_wait: waiting for pid %ld to exit.\n", pidnum);
                        while (kill(pidnum, 0) == 0) {
                            ;
                        }
                        close(lock_fd);
                    }
                    else {
                        PRINT_DBG("> lock_wait: pid %ld does not exist at this time.\n", pidnum);
                        close(lock_fd);
                        remove(lock_path);
                    }

                    lock_fd = -1;
                }
            }
            else if (errno != ENOENT) {
                PRINT_DBG("> lock_wait: Error creating lock file \"%s\", (%s). Disabled cross-process locking.\n", lock_path, strerror(errno));
                exe32_lock = 0;
                return;
            }
        }
    } while (lock_fd == -1);

    snprintf(pidstr, 10, "%d", getpid());
    write(lock_fd, pidstr, strlen(pidstr));
    close(lock_fd);
}

void unlock_wait(void) {
    if (exe32_lock && lock_path) {
        remove(lock_path);
        free(lock_path);
        lock_path = NULL;
        exe32_lock = 0;
    }
}

extern char **environ;

#ifdef DEFAULT_BASE_PATH
static char *get_full_base_path(void) {
    char *full_base_path = malloc(strlen(exe32_dirpath) + sizeof(DEFAULT_BASE_PATH));

    strcpy(full_base_path, exe32_dirpath);
    strcat(full_base_path, DEFAULT_BASE_PATH);

    return full_base_path;
}
#endif

void build_flat_environ(void) {
    char *wpenv_ptr;
    size_t flatenv_size = 0;
    int i;

#ifdef DEFAULT_BASE_PATH
    char *base_path = get_full_base_path();
    size_t base_path_len = strlen(base_path);
    base_path[base_path_len-1] = ':';
#endif

    for (i = 0; environ[i] != NULL; i++) {
#ifdef DEFAULT_BASE_PATH
        if (strstr(environ[i], "PATH=") == environ[i])
            flatenv_size += base_path_len + strlen(environ[i]) + 1;
        else
#endif
            flatenv_size += strlen(environ[i]) + 1;
    }

    wp_environ = malloc(++flatenv_size);
    wpenv_ptr = wp_environ;
    for (i = 0; environ[i] != NULL; i++) {
        size_t entry_size = strlen(environ[i]);

#ifdef DEFAULT_BASE_PATH
        if (strstr(environ[i], "PATH=") == environ[i]) {
            /*  append the base path to PATH env (PATH=<base_path>:...)
             *  because gcc.out does not search for a specific .out program
             *  in the GCCDIR environment and instead use PATH.
             */
            strcpy(wpenv_ptr, "PATH=");
            strcat(wpenv_ptr, base_path);
            strcat(wpenv_ptr, environ[i] + 5);

            strrep(wpenv_ptr, ':', ';');
            free(base_path);
            base_path = NULL;
            wpenv_ptr += entry_size + base_path_len + 1;
        }
        else {
            strcpy(wpenv_ptr, environ[i]);
            wpenv_ptr += entry_size + 1;
        }
#else
        strcpy(wpenv_ptr, environ[i]);
        if (strstr(environ[i], "PATH=") == environ[i]) strrep(wpenv_ptr, ':', ';');
        wpenv_ptr += entry_size + 1;
#endif
    }

    *wpenv_ptr = '\0';
}

static void parse_args(int argc, char **argv) {
    char *exe32_dirpath_slash;

    if (strchr(argv[0], '/'))
        exe32_dirpath = realpath(argv[0], NULL);
    if (exe32_dirpath == NULL) {
        PRINT_DBG("> realpath(argv[0]) failed or "EXEPROGNAME" is exec'd from PATH\n");
        exe32_dirpath = malloc(1024);
        readlink("/proc/self/exe", exe32_dirpath, 1024);
    }
    exe32_dirpath_slash = strrchr(exe32_dirpath, '/');
    if (exe32_dirpath_slash != NULL)
        *(exe32_dirpath_slash+1) = '\0';

    if (!strcmp(basename(argv[0]), EXEPROGNAME)
            || !strcmp(basename(argv[0]), "exew32.exe")) { // compatibility
        if (argc > 1) {
            if (argc > 2) {
                wp_args = malloc(512);
                join_args(wp_args, argc - 2, argv + 2);
            }
            else wp_args = NULL;
            wp_progname = fix_progname(argv[1]);
            is_exe32 = 1;
        }
        else {
            PRINT_ERR("Please specify a \".out\" program to load and run.\n"
                "\n"
                EXEPROGNAME" - KMC COFF program loader v."EXEPROGVER"\n"
                "\n"
                "Usage: ./"EXEPROGNAME" <[path/]progname[.out]> [parameters ...]\n"
                "  or, if progname is symlinked to "EXEPROGNAME":\n"
                "  ./<progname> [parameters ...]\n"
#ifdef DEFAULT_BASE_PATH
                "\n"
                "Default Load Path: \"" DEFAULT_BASE_PATH "\"\n"
#endif
            );
            exit(2);
        }
    }
    else {
        if (argc > 1) {
            wp_args = malloc(512);
            join_args(wp_args, argc - 1, argv + 1);
        }
        else wp_args = NULL;
        wp_progname = fix_progname(argv[0]);
        is_exe32 = 0;
    }

}

#ifndef NDEBUG
static FILE *log_file = NULL;

void write_log(const char *format, ...) {
    va_list args;
    va_start(args, format);

    if (log_file != NULL)
        vfprintf(log_file, format, args);
    fflush(log_file);

    va_end(args);
}

static void init_log(void) {
#ifdef LOG_FILE
    log_file = fopen(LOG_FILE, "w");

    if (log_file == NULL) {
        fprintf(stderr, "Error opening log file, using stderr\n");
        log_file = stderr;
    }
#else
    log_file = stderr;
#endif
}
#endif

void free_all(void) {
    unlock_wait();
#ifndef NDEBUG
    if (log_file != NULL) 
        fclose(log_file);
#endif
    free(wp_progname);
    free(wp_args);
    free(wp_environ);
    free(exe32_dirpath);
    if (full_win32_path != NULL)
        free(full_win32_path);
}

int main(int argc, char *argv[]) {
    char *_exe32_lock = getenv("EXE32_LOCK");
    if (_exe32_lock) {
        if (_exe32_lock[0] == '1' && _exe32_lock[1] == '\0')
            exe32_lock = 1;
        unsetenv("EXE32_LOCK");
    }

#ifndef NDEBUG
    init_log();
#endif
    parse_args(argc, argv);
    build_flat_environ();

    atexit(free_all);
    init_allocate();
    load_and_exec_prog(wp_progname, wp_args, wp_environ);

    return 0;
}
