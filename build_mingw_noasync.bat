@echo off
rem Build without SIM_ASYNCH_IO defined (avoiding the use of pthreads)
rem Compile all of SIMH using MINGW make and gcc environment
rem Individual simulator sources are in .\simulator_name
rem Individual simulator executables are to .\BIN
rem
rem If needed, define the path for the MINGW bin directory.
rem (this should already be set if MINGW was installed correctly)
rem
gcc -v 1>NUL 2>NUL
if ERRORLEVEL 1 path C:\MinGW\bin;%path%
if not exist BIN mkdir BIN
gcc -v 1>NUL 2>NUL
if ERRORLEVEL 1 echo "MinGW Environment Unavailable"
mingw32-make WIN32=1 NOASYNCH=1 -f makefile %*
