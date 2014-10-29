rem
rem  This script performs several maintenance functions prior to building
rem  simh projects.  Some of these funtions are optional and depend on the
rem  needs of the project being built, and others are generic and are always
rem  performed.
rem
rem  The optional activities are invoked by passing parameters to this 
rem  procedure.  The parameters are:
rem     ROM     To run the BuildROMs program prior to executing a project
rem             build.  This program verifies that the include files containing
rem             ROM images are consistent with the ROM images from which they
rem             are derived.
rem     BUILD   To validate that the required dependent libraries and include
rem             files are available in the directory ..\..\windows-build\
rem             These libraries currently include winpcap and pthreads and 
rem             optionally SDL and LIBPCRE.
rem     LIBSDL  To validate that the required dependent SDL libraries and include
rem             files are available in the directory ..\..\windows-build\
rem     LIBPCRE To validate that the required dependent PCRE libraries and include
rem             files are available in the directory ..\..\windows-build\
rem
rem  In addition to the optional activities mentioned above, other activities
rem  are also performed.  These include:
rem       - confirming that if the current source is a clone of the simh
rem         git repository, then then git hooks which manage making the
rem         git commit hash available during builds are properly installed
rem         in the repository hooks directory.  When the githooks are installed
rem         the current commit id is generated if git.exe is available in the
rem         current path.
rem       - performing the activities which make the git repository commit id
rem         available in an include file during compiles.
rem
rem

:_next_arg
if "%1" == "" goto _done_args
set _arg=
if /I "%1" == "ROM"     set _arg=ROM
if /I "%1" == "BUILD"   set _arg=BUILD
if /I "%1" == "LIBSDL"  set _arg=LIBSDL
if /I "%1" == "LIBPCRE" set _arg=LIBPCRE
if "%_arg%" == ""       echo *** warning *** unknown parameter %0
if not "%_arg%" == ""   set _X_%_arg%=%_arg%
shift
goto _next_arg
:_done_args
rem some arguments implicitly require BUILD to also be set to have 
rem any meaning.  These are LIBSDL and LIBPCRE
if not "%_X_LIBSDL%"  == "" set _X_BUILD=BUILD
if not "%_X_LIBPCRE%" == "" set _X_BUILD=BUILD


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
if not exist ../../windows-build-windows-build goto _check_files
rem This is a newly extracted windows-build.zip file with the
rem top level directory named as it existed in the zip file.
rem We rename that top level directory.  If a previous one already
rem exists, that will be an older version, so we try to remove 
rem that one first.
if exist ..\..\windows-build rmdir /s /q ..\..\windows-build
ren ..\..\windows-build-windows-build windows-build
if errorlevel 1 goto _notice3
if exist ../../windows-build-windows-build goto _notice3
:_check_files
if not exist ../../windows-build/winpcap/Wpdpack/Include/pcap.h goto _notice1
if not exist ../../windows-build/pthreads/pthread.h goto _notice1
if "%_X_LIBSDL%" == "" goto _done_libsdl
if not exist ../../windows-build/libSDL/SDL2-2.0.3/include/SDL.h goto _notice2
if not exist "../../windows-build/libSDL/Microsoft DirectX SDK (June 2010)/Lib/x86/dxguid.lib" goto _notice2
:_done_libsdl
if "%_X_LIBPCRE%" == "" goto _done_libpcre
if not exist ../../windows-build/PCRE/include/pcreposix.h goto _notice2
:_done_libpcre
goto _done_build
:_notice1
echo *****************************************************
echo *****************************************************
echo **  The required build support is not available.   **
echo *****************************************************
echo *****************************************************
set _exit_reason=The required build support is not available.
goto _ProjectInfo
:_notice2
echo *****************************************************
echo *****************************************************
echo **  The required build support is out of date.     **
echo *****************************************************
echo *****************************************************
set _exit_reason=The required build support is out of date.
goto _ProjectInfo
:_notice3
echo *****************************************************
echo *****************************************************
echo **  Can't rename ../../windows-build-windows-build **
echo **            to ../../windows-build               **
echo *****************************************************
echo *****************************************************
set _exit_reason=Can't rename ../../windows-build-windows-build to ../../windows-build
goto _ProjectInfo
:_ProjectInfo
type 0ReadMe_Projects.txt
echo error: %_exit_reason%
echo error: Review the Output Tab for more details.
exit 1
:_done_build

:_GitHooks
if not exist ..\.git goto _done_hooks
if exist ..\.git\hooks\post-commit goto _done_hooks
echo *****************************************************
echo *****************************************************
echo ** Installing git hooks in newly cloned repository **
echo *****************************************************
echo *****************************************************
copy /y git-hooks\post* ..\.git\hooks\
call :WhereInPath git.exe > NUL 2>&1
if %ERRORLEVEL% neq 0 goto _done_hooks
pushd ..
git log -1 "--pretty=%%H" >.git-commit-id
popd
:_done_hooks

:_SetId
rem
rem A race condition exists while creating the .git-commit-id.h file.
rem This race can happen at the beginning of a parallel build where 
rem several projects can start execution at almost the same time.
rem
SET GIT_COMMIT_ID=
if not exist ..\.git-commit-id goto _NoId
for /F %%i in (..\.git-commit-id) do SET GIT_COMMIT_ID=%%i
:_NoId
SET OLD_GIT_COMMIT_ID=
if not exist .git-commit-id.h echo.>.git-commit-id.h
for /F "tokens=3" %%i in (.git-commit-id.h) do SET OLD_GIT_COMMIT_ID=%%i
if "%GIT_COMMIT_ID%" equ "%OLD_GIT_COMMIT_ID%" goto _IdGood
echo Generating updated .git-commit-id.h containing id %GIT_COMMIT_ID%
echo #define SIM_GIT_COMMIT_ID %GIT_COMMIT_ID% >.git-commit-id.h
if errorlevel 1 echo Retrying...
if errorlevel 1 goto _SetId
:_IdGood
:_done_id
goto :EOF

:WhereInPath
if "%~$PATH:1" NEQ "" exit /B 0
exit /B 1
