rem
rem  This script performs several maintenance functions prior to building
rem  simh projects.  Some of these funtions are optional and depend on the
rem  needs of the project being built, and others are generic and are always
rem  performed.
rem
rem  The optional activities are invoked by passing parameters to this 
rem  procedure.  The parameters are:
rem     ROM    To run the BuildROMs program prior to executing a project
rem            build.  This program verifies that the include files containing
rem            ROM images are consistent with the ROM images from which they
rem            are derived.
rem     BUILD  To validate that the required dependent libraries and include
rem            files are available in the directory ..\..\windows-build\
rem            These libraries currently include winpcap and pthreads and 
rem            optionally SDL.
rem     LIBSDL To validate that the required dependent SDL libraries and include
rem            files are available in the directory ..\..\windows-build\
rem
rem  In addition to the optional activities mentioned above, other activities
rem  are also performed.  These include:
rem       - confirming that if the current source is a clone of the simh
rem         git repository, then then git hooks which manage making the
rem         git commit hash available during builds are properly installed
rem         in the repository hooks directory.
rem       - performing the activities which make the git repository commit id
rem         available in an include file during compiles.
rem
rem

:_next_arg
if "%1" == "" goto _done_args
set _arg=
if /I "%1" == "ROM"    set _arg=ROM
if /I "%1" == "BUILD"  set _arg=BUILD
if /I "%1" == "LIBSDL" set _arg=LIBSDL
if "%_arg%" == ""      echo *** warning *** unknown parameter %0
if not "%_arg%" == ""  set _X_%_arg%=%_arg%
shift
goto _next_arg
:_done_args

:_do_rom
if "%_X_ROM%" == "" goto _done_rom
pushd ..
SET _BLD=
if exist BIN\NT\Win32-Debug\BuildROMs.exe SET _BLD=BIN\NT\Win32-Debug\BuildROMs.exe
if exist BIN\NT\Win32-Release\BuildROMs.exe SET _BLD=BIN\NT\Win32-Release\BuildROMs.exe
if "%_BLD%" == "" echo ************************************************
if "%_BLD%" == "" echo ************************************************
if "%_BLD%" == "" echo **  Project dependencies not correct.         **
if "%_BLD%" == "" echo **  This project should depend on BuildROMs.  **
if "%_BLD%" == "" echo ************************************************
if "%_BLD%" == "" echo ************************************************
if "%_BLD%" == "" exit 1
%_BLD%
popd
:_done_rom

:_check_build
if "%_X_BUILD%" == "" goto _done_build
if exist ../../windows-build-windows-build move ../../windows-build-windows-build ../../windows-build >NUL
if not exist ../../windows-build/winpcap/Wpdpack/Include/pcap.h goto _notice1
if not exist ../../windows-build/pthreads/pthread.h goto _notice1
if "%_X_LIBSDL%" == "" goto _done_build
if not exist ../../windows-build/libSDL/SDL2-2.0.0/include/SDL.h goto _notice2
if not exist "../../windows-build/libSDL/Microsoft DirectX SDK (June 2010)/Lib/x86/dxguid.lib" goto _notice2
goto _done_build
:_notice1
echo ****************************************************
echo ****************************************************
echo **  The required build support is not available.  **
echo ****************************************************
echo ****************************************************
goto _ProjectInfo
:_notice2
echo ****************************************************
echo ****************************************************
echo **  The required build support is out of date.    **
echo ****************************************************
echo ****************************************************
goto _ProjectInfo
:_ProjectInfo
type 0ReadMe_Projects.txt
exit 1
:_done_build

if not exist ..\.git goto _done_hooks
if exist ..\.git\hooks\post-commit goto _done_hooks
echo *****************************************************
echo *****************************************************
echo ** Installing git hooks in newly cloned repository **
echo *****************************************************
echo *****************************************************
copy git-hooks\post* ..\.git\hooks\
:_done_hooks

:_SetId
SET GIT_COMMIT_ID=
if not exist ..\.git-commit-id goto _NoId
for /F %%i in (..\.git-commit-id) do SET GIT_COMMIT_ID=%%i
:_NoId
SET OLD_GIT_COMMIT_ID=
if not exist .git-commit-id.h echo.>.git-commit-id.h
for /F "tokens=3" %%i in (.git-commit-id.h) do SET OLD_GIT_COMMIT_ID=%%i
if "%GIT_COMMIT_ID%" equ "%OLD_GIT_COMMIT_ID%" goto _IdGood
echo #define SIM_GIT_COMMIT_ID %GIT_COMMIT_ID% >.git-commit-id.h
:_IdGood
:_done_id