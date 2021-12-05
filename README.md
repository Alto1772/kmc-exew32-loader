# kmc-exew32-loader

exew32 implementation on linux, without requiring to patch the entered program (the loaded program will take care of patching itself).

The exe32 program can be symlinked or as a replacement of exew32.exe. The symlinks must not be in the directory where .out programs are located. All the other programs work correctly in this program except MILD.OUT, which had trouble making the n64 rom.

A Windows port of this might be easy, but exew32.exe is a windows program so I don't need to.

# Notes

## MAKE.OUT

requires replacing exew32.exe with this program in order to work properly. The clean action does not work and it requires patching the makefile and using the host make program instead.

## GCC.OUT

doesn't require the GCCDIR environment variable, but if it is required then remember to avoid putting some files on the GCCDIR/mipse/bin like cpp, as, or ld (without the .out extension), as that will confuse GCC.OUT to open a file that does not exist.

## the symlinks

just do "make symlinks" and it will create symbolic links to exe32-linux so it can command without having to enter "./exe32-linux gcc".

## `make DEBUG=1`

activates debug mode and logs the wrapper actions in stderr. Add `WITH_LOG_FILE=1` to put those in log.txt, but when exe32-linux is about to spawn (fork and exec) another exe32-linux process the newly created child process will overwrite the same log file, so avoid using the flag if you want to view the logs. (The only programs that execute the spawnve wrapper are MILD.OUT and MAKE.OUT)
