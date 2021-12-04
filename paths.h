#ifndef EXE32_PATHS_H
#define EXE32_PATHS_H

char *fix_win_path(char *path);
void replace_case_path(char *path);
void join_args(char *jpaths, int argc, char **argv);
char *fix_progname(const char *progname);

#endif // EXE32_PATHS_H
