#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "main.h"
#include "coff.h"
#include "wrappers.h"
#include "fd.h"
#include "paths.h"

// lets set up a fake program path to fool that we are in win32 environment
// needed by ld.out
#define DEFAULT_DRIVE "C:\\"
char *full_win32_path = NULL;

static void set_full_path(const char *prog_path) {
    if (full_win32_path != NULL) free(full_win32_path);
    full_win32_path = malloc(sizeof(DEFAULT_DRIVE) + strlen(prog_path));
    strcpy(full_win32_path, DEFAULT_DRIVE);
    strcat(full_win32_path, prog_path);
    strrep_forwslashes(full_win32_path);
}

static FILE *load_program_relative(const char *progname) {
    FILE *f_ret;
    char *fullpath;

    fullpath = malloc(strlen(exe32_dirpath) + strlen(progname) + 1);
    strcpy(fullpath, exe32_dirpath);
    strcat(fullpath, progname);
    f_ret = fopen(fullpath, "rb");
    if (f_ret) set_full_path(fullpath);
    free(fullpath);
    return f_ret;
}

#ifdef DEFAULT_BASE_PATH
static FILE *load_program_from_base_path(const char *progname) {
    FILE *f_ret;
    char *new_path = malloc(sizeof(DEFAULT_BASE_PATH) + strlen(progname));
    strcpy(new_path, DEFAULT_BASE_PATH);
    strcat(new_path, progname);

    PRINT_DBG("> base_path: %s\n", new_path);
    f_ret = load_program_relative(new_path);
    free(new_path);

    return f_ret;
}
#endif

static FILE *load_program_from_paths(const char *progname) {
    char *env_path = getenv("PATH");
    size_t prgname_len = strlen(progname);
    FILE *f_ret;

    PRINT_DBG("> from_paths: PATH=%s\n", env_path);
    while (env_path) {
        char *colon_delim;
        size_t path_len;

        if ((colon_delim = strchr(env_path, ':')) == NULL) {
            path_len = strlen(env_path);
        } else {
            path_len = colon_delim - env_path;
        }

        char *new_progname = malloc(path_len + prgname_len + 2);
        memcpy(new_progname, env_path, path_len); /* that stupid stringop-trunctation warning is annoying to me! */
        new_progname[path_len] = '/';
        strcpy(new_progname + path_len + 1, progname);

        f_ret = fopen(new_progname, "rb");
        if (f_ret) set_full_path(new_progname);

        PRINT_DBG("> from_paths: %s\n", new_progname);
        if (f_ret) {
            free(new_progname);
            return f_ret;
        }
        else {
            free(new_progname);
            env_path = colon_delim ? colon_delim + 1 : NULL;
        }
    }

    return NULL;
}

// search progname in exe32's directory, base path relative to exe32's dir, and in paths
static FILE *load_program_basename(const char *progname) {
    FILE *f_ret = load_program_relative(progname);
    if (f_ret == NULL) {
#ifdef DEFAULT_BASE_PATH
        f_ret = load_program_from_base_path(progname);
        if (f_ret == NULL)
#endif
            f_ret = load_program_from_paths(progname);
    }
    return f_ret;
}

static FILE *load_program(const char *progname) {
    if (progname[0] != '/') {
        char *prg_slashpos = strchr(progname, '/');
        if (prg_slashpos == NULL) {
            return load_program_basename(progname);
        }
        else {
            return load_program_relative(progname);
        }
    }
    else { // load_program_absolute
        FILE *f_ret = fopen(progname, "rb");
        if (f_ret) set_full_path(progname);
        return f_ret;
    }
}

static struct wrapprog_exec_s wp_exec_info;

void load_and_exec_prog(char *progname, char *args, char *env) {
    FILE *fprg;
    init_first_t init_first_addr = NULL;

    // find the program file
    fprg = is_exe32 ? load_program(progname) : load_program_basename(basename(progname));

    if (fprg == NULL) {
        char *caseprgname = strdup(progname), *caseprgdup = caseprgname;
        if (progname[0] == '/') { // if it's absolute
            replace_case_path(caseprgname);
        }
        else { // capitalize progname
            while (*caseprgname) {
                *caseprgname = toupper((unsigned char) *caseprgname);
                caseprgname++;
            }
        }
        PRINT_DBG("Cannot access %s, trying upper cased (%s)\n", progname, caseprgdup);

        fprg = is_exe32 ? load_program(caseprgdup) : load_program_basename(basename(caseprgdup));
        free(caseprgdup);

        if (fprg == NULL) {
            PRINT_ERR("Cannot load \"%s\": ", progname);
            perror(NULL);
            exit(10);
        }
    }

    // load the program file into memory
    {
        int i;
        struct CoffHdr_s prg_hdr;
        struct CoffSecHdr_s *prg_secs;

        fread(&prg_hdr, sizeof(struct CoffHdr_s), 1, fprg);
        if (feof(fprg) && prg_hdr.f_magic != 0x014c) {
            PRINT_ERR("\"%s\" is not a COFF Program!\n", progname);
            exit(10);
        }
        if (prg_hdr.f_nscns < 1) {
            PRINT_ERR("\"%s\" has no sections!\n", progname);
            exit(10);
        }
        if (prg_hdr.f_opthdr != 0x1c) {
            PRINT_ERR("Optional header size not 0x1c\n");
            exit(11);
        }
        fseek(fprg, 0x1c, SEEK_CUR);

        prg_secs = malloc(sizeof(struct CoffSecHdr_s) * prg_hdr.f_nscns);
        fread(prg_secs, prg_hdr.f_nscns, sizeof(struct CoffSecHdr_s), fprg);
        if (feof(fprg)) {
            PRINT_ERR("EOF while reading section headers\n");
            exit(11);
        }

        for (i = 0; i < prg_hdr.f_nscns; i++) {
            struct CoffSecHdr_s sec = prg_secs[i];
            if (init_first_addr == NULL) {
                init_first_addr = (init_first_t) sec.s_vaddr;
            }

            if (sec.s_scnptr != 0) {
                fseek(fprg, sec.s_scnptr, SEEK_SET);
                fread((void*)sec.s_vaddr, sec.s_size, 1, fprg);
                if (feof(fprg)) {
                    PRINT_ERR("EOF while reading at %#x", sec.s_scnptr);
                    exit(11);
                }
            }
        }

        free(prg_secs);
        fclose(fprg);
    }

    lock_wait();
    init_fd_fptrs();
    wp_exec_info.wp_heap_start = (void *)0x01000000; // this might be unused
    wp_exec_info.wp_name = full_win32_path;
    wp_exec_info.wp_args = args;
    wp_exec_info.wp_environ = env;

    exec_init_first(init_first_addr, &wp_exec_info);
}
