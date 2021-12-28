#ifndef EXE32_LOAD_H
#define EXE32_LOAD_H

void load_and_exec_prog(char *, char *, char *);
void xexit(int);

extern void *main_stack_ptr;
#define save_stack_ptr() __asm__("mov %%esp, %0" : "=r" (main_stack_ptr))
#define restore_stack_ptr() __asm__("mov %0, %%esp\n" :: "r" (main_stack_ptr))

extern char *full_win32_path;

#endif // EXE32_LOAD_H
