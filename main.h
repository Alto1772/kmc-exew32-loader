#ifndef EXE32_MAIN_H
#define EXE32_MAIN_H

extern char *exe32_dirpath;
extern int is_exe32;
extern int exe32_lock;

void lock_wait(void);
void unlock_wait(void);

#endif // EXE32_MAIN_H
