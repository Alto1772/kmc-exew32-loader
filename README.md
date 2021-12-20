# kmc-exew32-loader

exew32 implementation on linux, without requiring to patch the specified .out program (the loaded program will take care of patching itself).

The exe32 program can be symlinked or as a replacement of exew32.exe. The symlinks must not be in the directory where .out programs are located. All the programs now work correctly, but they need some tweaks so expect some bugs.

A Windows port of this might be easy, but exew32.exe is a windows program so I don't need to.

# Notes

## MAKE.OUT

requires replacing exew32.exe with this program in order to work properly. The "make clean" action does not work so use the host make for that.

## GCC.OUT

doesn't require the GCCDIR environment variable, but if it is required then remember to avoid putting some non-programs on the GCCDIR/mipse/bin like cpp, as, or ld (without the .out extension), as that will confuse GCC.OUT to open a file that does not exist.

## the symlinks

just do "make symlinks" and it will create symbolic links to exe32-linux so it can command without having to enter "./exe32-linux gcc".

## `make DEBUG=1`

activates debug mode and logs the wrapper actions in stderr. Add `WITH_LOG_FILE=1` to write in log.txt, but when exe32-linux is about to spawn (fork and exec) another exe32-linux process the newly created child process will overwrite the same log file, so avoid using the flag if you want to view the logs. (The only programs that I know of are MILD.OUT and MAKE.OUT)

## `make NOBASEPATH=1`

removes the "default base path" (first path to search for .out). Use it if you wanna move exe32-linux to other places, and make sure the PATH variable contains a path to bin .out programs.
