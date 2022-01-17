#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // for upper()
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include "common.h"
#include "main.h"
#include "load.h"
#include "coff.h"
#include "wrappers.h"
#include "fd.h"
#include "paths.h"
#include "memmap.h"

// lets set up a fake program path to fool that we are in win32 environment
// needed by ld.out
#define DEFAULT_DRIVE "C:\\"
char *full_win32_path = NULL;

#define fullpath_relative_to_exe32_dir(progname) concat(exe32_dirpath, progname)

#ifdef DEFAULT_BASE_PATH
#define fullpath_with_relative_base_path(progname) concat3(exe32_dirpath, DEFAULT_BASE_PATH, progname)
#endif

#define file_exists(file) !access(file, F_OK)

static char *find_program(char *progname) {
    char *new_path;

    if (is_exe32) {
        if (progname[0] != '/') {
            if (strchr(progname, '/') != NULL) {
                if (!file_exists(progname)) {
                    new_path = fullpath_relative_to_exe32_dir(progname);
                    if (file_exists(new_path)) {
                        return new_path;
                    }
                    else {
                        free(new_path);
                        return NULL;
                    }
                }
                else // relative to current work dir
                    return file_exists(progname) ? progname : NULL;
            }
            // else fallthrough
        }
        else // absolute
            return file_exists(progname) ? progname : NULL;
    }
    else {
        progname = basename(progname);
    }

    /**** find_program_basename ****/
    /* search progname in exe32's directory, base path relative to exe32's dir, and the paths in PATH environment */

    new_path = fullpath_relative_to_exe32_dir(progname);
    if (file_exists(new_path)) return new_path;
    free(new_path);

#ifdef DEFAULT_BASE_PATH
    new_path = fullpath_with_relative_base_path(progname);
    if (file_exists(new_path)) return new_path;
    free(new_path);
#endif

    {
        /**** full_path_from_path_env ****/

        char *paths = strdup(getenv("PATH")), *_paths, *pathtoken;

        for (_paths = paths ; ; _paths = NULL) {
            pathtoken = strtok(_paths, ":");
            if (pathtoken == NULL) break;

            new_path = concat(pathtoken, progname);
            if (file_exists(new_path)) {
                free(paths);
                return new_path;
            }
            else
                free(new_path);
        }

        free(paths);
    }

    // all else fails
    return NULL;
}

// load the program file into memory
void *load_coff_prg(const char *progname) {
    void *text_addr = NULL;
    FILE *fprg = fopen(progname, "rb");
    int i;
    struct CoffHdr_s prg_hdr;
    struct CoffSecHdr_s *prg_secs;

    if (fprg == NULL) {
        PRINT_ERR("Error opening \"%s\": %s\n", progname, strerror(errno));
        exit(10);
    }

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
        if (sec.s_flags & STYP_TEXT && text_addr == NULL) {
            text_addr = (init_first_t) sec.s_vaddr;
        }

        if (mem_map(sec.s_vaddr, sec.s_size)) {
            PRINT_ERR("Error: Cannot allocate virtual address at %p\n", sec.s_vaddr);
            exit(20);
        }

        if (!(sec.s_flags & STYP_BSS)) {
            fseek(fprg, sec.s_scnptr, SEEK_SET);
            fread(sec.s_vaddr, sec.s_size, 1, fprg);
            if (feof(fprg)) {
                PRINT_ERR("EOF while reading at %#x", sec.s_scnptr);
                exit(11);
            }
        }
    }

    free(prg_secs);
    return text_addr;
}

void *main_stack_ptr;

// restore stack pointer before exit
static int _exit_status;
static void _xexit(void) {
    exit(_exit_status);
}
void xexit(int status) {
    _exit_status = status;
    restore_stack_ptr();
    _xexit();
}

int get_stack_size(void) {
    struct rlimit rl;

    if (getrlimit(RLIMIT_STACK, &rl)) {
        PRINT_DBG("> get_stack_size: cannot get stack size, defaulting to 0x10000.\n");
        return 0x10000;
    }

    return rl.rlim_cur;
}

void load_and_exec_prog(char *progname, char *args, char *env) {
    char *new_progname;
    static struct wrapprog_exec_s wp_exec_info;
    init_first_t init_first_addr = NULL;

    // find the program file
    new_progname = find_program(progname);

    if (new_progname == NULL) {
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
        PRINT_DBG("Cannot load %s, trying upper cased (%s)\n", progname, caseprgdup);
        new_progname = find_program(caseprgdup);
        if (new_progname == NULL) {
            PRINT_ERR("Cannot load \"%s\": %s\n", caseprgdup, strerror(errno));
            exit(10);
        }
    }


    full_win32_path = concat(DEFAULT_DRIVE, new_progname);
    strrep_forwslashes(full_win32_path);
    init_first_addr = (init_first_t)load_coff_prg(new_progname);
    free(new_progname);
    lock_wait();
    init_fd_fptrs();
    wp_exec_info.wp_heap_start = get_heap_addr(); // this might be unused
    wp_exec_info.wp_name = full_win32_path;
    wp_exec_info.wp_args = args;
    wp_exec_info.wp_environ = env;

    // loaded program sets the stack ptr to 0x01080000 before calling any of the wrappers
    {
        int stack_size = get_stack_size();

        if (stack_size > 0x00010000) {
            stack_size = 0x00010000;
        }

        if (mem_map((void*) 0x01080000 - stack_size, stack_size)) {
            PRINT_ERR("Error: Cannot allocate stack address at 0x01070000\n");
            exit(20);
        }
    }

    exec_init_first(init_first_addr, &wp_exec_info);
}
