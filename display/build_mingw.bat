@echo off
rem $Id: build_mingw.bat,v 1.1 2004/01/25 17:48:03 phil Exp $
rem Compile all test programs using MINGW make and gcc environment
rem
rem If needed, define the path for the MINGW bin directory.
rem (this should already be set if MINGW was installed correctly)
rem
gcc -v 1>NUL 2>NUL
if ERRORLEVEL 1 path C:\MinGW\bin;D:\MinGW\bin;E:\MinGW\bin;%path%
gcc -v 1>NUL 2>NUL
if ERRORLEVEL 1 echo "MinGW Environment Unavailable"
mingw32-make WIN32=1 -f gmakefile %1 %2 %3 %4
