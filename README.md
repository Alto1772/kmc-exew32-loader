# kmc-exew32-loader

allows you to run KMC programs without using WINE loader.

The exew32-linux loader doesn't do any runtime hacks as the loaded program will take care of anyting.

The exe32 program can be symlinked into another program, or as a replacement of EXEW32.EXE. The symlinks won't be in the same directory where .out programs are located. All the programs now work correctly, but they need some tweaks so expect some bugs.

A Windows port of this might be easy, but exew32.exe is a windows program so I don't need to.

# Build Flags

## `make DEBUG=1`

activates debug mode and logs the wrapper actions in stderr. Add `WITH_LOG_FILE=1` to write the debug logs in log.txt, and do not run other programs that has an ability to spawn (fork and exec) a new exe32-linux process because that newly created child process will overwrite the same log file, so avoid using the flag if you try to execute some programs like MILD.OUT and MAKE.OUT and want to view log.txt file.

## `make NOBASEPATH=1`

removes the "default base path" (first path to search for .out, mostly set in the makefile as `kmc/gcc/mipse/bin`). Use it if you wanna move exe32-linux to other places, and also make sure the PATH environment contains the directory where the .out programs are found plus the exe32 itself of where you put it.

# Notes

## MAKE.OUT

requires replacing exew32.exe with this program in order to work properly. The "make clean" action does not work so use the host make for that.

## GCC.OUT

doesn't require the GCCDIR environment variable, but if it is required then remember to avoid putting some non-programs on the GCCDIR/mipse/bin like cpp, as, or ld (without the .out extension), as that will confuse GCC.OUT to open a file that does not exist.

## the symlinks

just do "make symlinks" and it will create symbolic links to exe32-linux so it can command without having to enter "./exe32-linux gcc".
