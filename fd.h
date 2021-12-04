#ifndef EXE32_FD_H
#define EXE32_FD_H

#include <stdio.h>

#define NUM_FILEPTRS 20

extern FILE *fd_fileptrs[NUM_FILEPTRS];

void init_fd_fptrs(void);
int append_fd(FILE *);

#endif // EXE32_FD_H
