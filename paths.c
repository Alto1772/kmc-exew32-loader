#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include "common.h"

/* replace case insensitive path */
void replace_case_path(char *path) {
    int pl = 0;
    size_t cul, pathlen = strlen(path);
    char *curstr = path, *pret = path, *pbuf;
    DIR *d;
    struct dirent *dent = NULL;

    if (!access(path, R_OK)) {
        // exit if the path is already correct
        return;
    }

    PRINT_DBG("> replace_case_path: Original = %s\n", path);
    pbuf = malloc(pathlen + 1);
    if (path[0] == '/') {
        d = opendir("/");
        pbuf[0] = '/';
        pbuf[1] = '\0';
        pl = 1;
        path++;
    }
    else {
        d = opendir(".");
        pbuf[0] = '\0';
    }

    while ((curstr = strchr(path, '/'))) {
        cul = curstr - path;

        if (d != NULL) {
            while ((dent = readdir(d))) {
                if (strlen(dent->d_name) == cul &&
                        !strncasecmp(path, dent->d_name, cul)) {
                    strncpy(pbuf + pl, dent->d_name, cul + 1);
                    pbuf[pl+cul] = '/';
                    pbuf[pl+cul+1] = '\0';
                    closedir(d);
                    d = opendir(pbuf);
                    break;
                }
            }
        }
        if (d == NULL || dent == NULL) {
            strncpy(pbuf + pl, path, cul + 1);
            d = opendir(pbuf);
            if (errno == ENOENT) {
                strcpy(pbuf + pl, path);
                path += strlen(path);
                break;
            }
        }

        path += cul + 1;
        pl += cul + 1;
    }

    if (*path != '\0') {
        cul = strlen(path);
        if (d != NULL) {
            while ((dent = readdir(d))) {
                if (strlen(dent->d_name) == cul &&
                        !strncasecmp(path, dent->d_name, cul)) {
                    strncpy(pbuf + pl, dent->d_name, cul);
                    break;
                }
            }
        }
        if (dent == NULL) {
            // strncpy(pbuf + pl, path, cul);
            memcpy(pbuf + pl, path, cul); /* that stupid stringop-trunctation warning is annoying to me! */
            pbuf[pl+cul] = '\0';
        }
    }

    if (d) closedir(d);
    strncpy(pret, pbuf, pathlen);
    PRINT_DBG ("> replace_case_path: Replaced = %s\n", pret);
    free(pbuf);
}

char *fix_win_path(char *path) {
    if ((path[0] >= 'A' || path[0] <= 'Z'
                || path[0] >= 'a' || path[0] <= 'z') && path[1] == ':') {
        path += 2;
    }
    if (path[0] == '/' && path[1] == '/') {
        path += 1;
    }
    return path;
}

void join_args(char *jpaths, int argc, char **argv) {
    int i;

    for (i = 0; i < argc; i++) {
        char *curarg = argv[i];
        int has_wspace = 0;

        if (strchr(curarg, '\t') || strchr(curarg, ' ')) has_wspace = 1;
        if (has_wspace) {
            *jpaths++ = '"';
        }
        while (*curarg != '\0') {
            *jpaths++ = *curarg++;
        }
        if (has_wspace) {
            *jpaths++ = '"';
        }
        if (i < argc-1) *jpaths++ = ' ';
    }

    *jpaths = '\0';
}

char *fix_progname(const char *progname) {
    char *new_progname;
    size_t prog_len = strlen(progname);

    if (strcasecmp(progname + prog_len - 4, ".out")) {
        new_progname = malloc(prog_len + 6);
        strcpy(new_progname, progname);
        strcat(new_progname, ".out");
    }
    else {
        new_progname = strdup(progname);
    }
    return new_progname;
}

