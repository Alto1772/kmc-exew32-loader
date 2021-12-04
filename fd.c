#include <stdio.h>
#include <string.h>
#include "common.h"
#include "fd.h"

FILE *fd_fileptrs[NUM_FILEPTRS];

int append_fd(FILE *fstream) {
    int i;
    for (i = 0; i < NUM_FILEPTRS; i++) {
        if (fd_fileptrs[i] == NULL) {
            fd_fileptrs[i] = fstream;
            return i;
        }
    }

    return -1;
}

void init_fd_fptrs(void) {
    int i;

    fd_fileptrs[0] = stdin;
    fd_fileptrs[1] = stdout;
    fd_fileptrs[2] = stderr;

    fd_fileptrs[3] = stderr;
    fd_fileptrs[4] = stderr;
    for (i = 5; i < NUM_FILEPTRS; i++) {
        fd_fileptrs[i] = NULL;
    }
}
