@echo off
rem Compile all of SIMH using MINGW make and gcc environment
rem Individual simulator sources are in .\simulator_name
rem Individual simulator executables are to .\bin
rem
rem If needed, define the path for the MINGW bin directory.
rem (this should already be set if MINGW was installed correctly)
rem
gcc -v 1>NUL 2>NUL
if ERRORLEVEL 1 path C:\MinGW\bin;%path%
if not exist BIN mkdir BIN
gcc -v 1>NUL 2>NUL
if ERRORLEVEL 1 echo "MinGW Environment Unavailable"
mingw32-make WIN32=1 -f makefile %1 %2 %3 %4
