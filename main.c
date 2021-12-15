#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h>
#include "common.h"
#include "load.h"
#include "paths.h"
#include "memmap.h"

#ifndef EXEPROGNAME
#define EXEPROGNAME "exe32-linux"
#endif
#ifndef EXEPROGVER
#define EXEPROGVER "unknown"
#endif

char *exe32_dirpath = NULL;
int is_exe32 = 0;

static char *wp_progname;
static char *wp_args;
static char *wp_environ;

extern char **environ;

void build_flat_environ(void) {
    size_t flatenv_size = 0;
    char *wpenv_ptr;
    int i;

    for (i = 0; environ[i] != NULL; i++) {
        flatenv_size += strlen(environ[i]) + 1;
    }

    wp_environ = malloc(++flatenv_size);
    wpenv_ptr = wp_environ;
    for (i = 0; environ[i] != NULL; i++) {
        size_t entry_size = strlen(environ[i]);
        strcpy(wpenv_ptr, environ[i]);
        if (strstr(wpenv_ptr, "PATH=") == wpenv_ptr) strrep(wpenv_ptr, ':', ';');
        wpenv_ptr += entry_size + 1;
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
#ifndef NDEBUG
    if (log_file != NULL) 
        fclose(log_file);
#endif
    mem_unmap_all();
    free(wp_progname);
    free(wp_args);
    free(wp_environ);
    free(exe32_dirpath);
    if (full_win32_path != NULL)
        free(full_win32_path);
}

int main(int argc, char *argv[]) {
#ifndef NDEBUG
    init_log();
#endif
    parse_args(argc, argv);
    build_flat_environ();

    atexit(free_all);
    load_and_exec_prog(wp_progname, wp_args, wp_environ);

    return 0;
}
