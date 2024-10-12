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
rem             These libraries currently include winpcap, pthreads, SDL
rem             and LIBPCRE.
rem     LIBSDL  To validate that the required dependent SDL libraries and include
rem             files are available in the directory ..\..\windows-build\
rem     LIBPCRE To validate that the required dependent PCRE libraries and include
rem             files are available in the directory ..\..\windows-build\
rem
rem  In addition to the optional activities mentioned above, other activities
rem  are also performed.  These include:
rem       - performing the activities which make confirm or generate the git 
rem         repository commit id available in an include file during compiles.
rem
rem

set _PDB=%~dpn1.pdb
if exist "%_PDB%" del/q "%_PDB%"
set _PDB=
set _ARG=%~1
set _TARGET=%RANDOM%
if /i "%_ARG:~-4%" equ ".exe" set _TARGET=%~n1
if /i "%_ARG:~-4%" equ ".exe" shift /1
set _ARG=

rem Everything implicitly requires BUILD to also be set to have 
rem any meaning, it always gets set.
set _X_BUILD=BUILD
set _X_REQUIRED_WINDOWS_BUILD=20241011
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
echo _VC_VER=%_VC_VER%
echo _MSVC_VER=%_MSVC_VER%
echo _MSVC_TOOLSET_VER=%_MSVC_TOOLSET_VER%
echo _MSVC_TOOLSET_DIR=%_MSVC_TOOLSET_DIR%

:_next_arg
if "%1" == "" goto _done_args
set _arg=
if /I "%1" == "ROM"      set _arg=ROM
if /I "%1" == "BUILD"    set _arg=BUILD
if /I "%1" == "LIBSDL"   set _arg=LIBSDL
if /I "%1" == "LIBPCRE"  set _arg=LIBPCRE
if "%_arg%" == ""        echo *** warning *** unknown parameter %1
if not "%_arg%" == ""    set _X_%_arg%=%_arg%
shift
goto _next_arg
:_done_args


:_do_rom
pushd ..
if "%_X_ROM%" == "" goto _done_rom
SET _BLD=
if exist BIN\NT\Win32-Debug\BuildTools\BuildROMs.exe SET _BLD=BIN\NT\Win32-Debug\BuildTools\BuildROMs.exe
if exist BIN\NT\Win32-Release\BuildTools\BuildROMs.exe SET _BLD=BIN\NT\Win32-Release\BuildTools\BuildROMs.exe
if "%_BLD%" == "" echo ************************************************
if "%_BLD%" == "" echo ************************************************
if "%_BLD%" == "" echo **  Project dependencies are not correct.     **
if "%_BLD%" == "" echo **  This project should depend on BuildROMs.  **
if "%_BLD%" == "" echo ************************************************
if "%_BLD%" == "" echo ************************************************
if "%_BLD%" == "" echo error: Review the Output Tab for more details.
if "%_BLD%" == "" exit 1
%_BLD%
if not errorlevel 1 goto _done_rom
if not exist "BIN\NT\Win32-Release\BuildTools\BuildROMs.exe" exit 1
del "BIN\NT\Win32-Release\BuildTools\BuildROMs.exe"
popd
goto _do_rom
:_done_rom
popd

:_CheckGit
if not exist ..\.git goto _done_git
call :FindGit _GIT_GIT
if "%_GIT_GIT%" neq "" goto _done_git
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo **                                                    **
echo **   Your local simh code is in a git repository,     **
echo **   however, the git program executable can not be   **
echo **   readily found on your system.                    **
echo **                                                    **
echo **   You should download and install git from:        **
echo **                                                    **
echo **        https://git-scm.com/download/win            **
echo **                                                    **
echo **   while installing git for windows, be sure to     **
echo **   select the option to "Use Git from the Windows   **
echo **   Command Prompt"                                  **
echo **                                                    **
echo **   You should logout and login again after initally **
echo ""   installing git to be sure that the installation  **
echo **   location is properly visible in your search path.**
echo **                                                    **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo error: Review the Output Tab for more details.
exit 1
:_done_git

:_check_build
if "%_X_BUILD%" == "" goto _done_build
if not exist ..\..\windows-build-windows-build goto _check_files
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
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER _MSVC_TOOLSET_DIR
if not exist ..\..\windows-build goto _notice1
if not exist ..\..\windows-build/lib goto _notice2
set _X_WINDOWS_BUILD=
for /F "usebackq tokens=2" %%i in (`findstr /C:"WINDOWS-BUILD" ..\..\windows-build\Windows-Build_Versions.txt`) do SET _X_WINDOWS_BUILD=%%i
if "%_X_WINDOWS_BUILD%" LSS "%_X_REQUIRED_WINDOWS_BUILD%" goto _notice2
set _X_LAST_WINDOWS_BUILD=
if exist Pre-Build-Event.last-windows-build-version.txt for /F "usebackq tokens=2" %%i in (`findstr /C:"WINDOWS-BUILD" Pre-Build-Event.last-windows-build-version.txt`) do SET _X_LAST_WINDOWS_BUILD=%%i
if "%_X_WINDOWS_BUILD%" EQU "%_X_LAST_WINDOWS_BUILD%" goto _new_or_same_windows_build
echo Library support has been updated, forcing clean version determination
if exist ../../windows-build/lib/Debug rmdir/s/q ..\..\windows-build\lib\Debug
if exist ../../windows-build/lib/Release rmdir/s/q ..\..\windows-build\lib\Release
if exist ../../windows-build/lib/VisualCVersionSupport.txt del ..\..\windows-build\lib\VisualCVersionSupport.txt
echo WINDOWS-BUILD           %_X_WINDOWS_BUILD% >Pre-Build-Event.last-windows-build-version.txt
:_new_or_same_windows_build
set _X_WINDOWS_BUILD=
set _X_LAST_WINDOWS_BUILD=
if not exist ../../windows-build/lib/VisualCVersionSupport.txt goto _find_vc_support

set _X_VC_VER=
SET _X_MSVC_VER=
if "%_MSVC_TOOLSET_VER%" EQU "v140" set _VC_VER=2015
if "%_MSVC_TOOLSET_VER%" EQU "v141" set _VC_VER=2017
if "%_MSVC_TOOLSET_VER%" EQU "v142" set _VC_VER=2019
if "%_MSVC_TOOLSET_VER%" EQU "v143" set _VC_VER=2022
for /F "usebackq tokens=2*" %%i in (`findstr /C:"_VC_VER=%_VC_VER% " ..\..\windows-build\lib\VisualCVersionSupport.txt`) do SET _X_VC_VER=%%i %%j
for /F "usebackq delims== tokens=2" %%i in (`findstr /C:"_MSVC_VER=" ..\..\windows-build\lib\VisualCVersionSupport.txt`) do SET _X_MSVC_VER=%%i
echo _MSVC_VER=%_MSVC_VER%
echo _X_MSVC_VER=%_X_MSVC_VER%
if "%_X_VC_VER%" neq "" if "%_X_MSVC_VER%" neq "" echo Library support for %_X_VC_VER% (%_X_MSVC_VER%) is available
if "%_X_VC_VER%" neq "" if "%_X_MSVC_VER%" neq "" goto _done_libsdl
if "%_X_VC_VER%" neq "" echo Library support for %_X_VC_VER% is available
if "%_X_VC_VER%" neq "" goto _done_libsdl
:_find_vc_support
set _X_VC_VER_DIR=
for /d %%i in ("../../windows-build/lib/*") do call :CheckDirectoryVCSupport _X_VC_VER_DIR %%i "../../windows-build/lib/"
if "%_X_VC_VER_DIR%" equ "" goto _notice4
:_make_vc_support_active
for /F "usebackq tokens=2*" %%i in (`findstr /C:"_VC_VER=%_VC_VER% " "%_X_VC_VER_DIR%\VisualCVersionSupport.txt"`) do SET _X_VC_VER=%%i %%j
echo Enabling Library support for %_X_VC_VER%
call "%_X_VC_VER_DIR%\Install-Library-Support.cmd"
:_done_libsdl
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER _MSVC_TOOLSET_DIR
if not exist "..\..\windows-build\libpng-1.6.18\projects\Release Library" goto _setup_library
if not exist "..\..\windows-build\libpng-1.6.18\projects\Release Library\VisualC.version" set _LIB_VC_VER=9
if exist "..\..\windows-build\libpng-1.6.18\projects\Release Library\VisualC.version" for /f "usebackq delims=." %%v in (`type "..\..\windows-build\libpng-1.6.18\projects\Release Library\VisualC.version"`) do set _LIB_VC_VER=%%v
if %_LIB_VC_VER% EQU %_VC_VER% goto _done_library
if %_VC_VER% GEQ 14 goto _check_new_library
if %_LIB_VC_VER% LSS 14 goto _done_library
goto _setup_library
:_check_new_library
if %_LIB_VC_VER% GEQ 14 goto _done_library
:_setup_library
if %_VC_VER% LSS 14 set _VCLIB_DIR_=vstudio 2008
if %_VC_VER% GEQ 14 set _VCLIB_DIR_=vstudio
if exist "..\..\windows-build\libpng-1.6.18\projects\Release Library" rmdir/s/q "..\..\windows-build\libpng-1.6.18\projects\Release Library"
if exist "..\..\windows-build\libpng-1.6.18\projects\Debug Library"   rmdir/s/q "..\..\windows-build\libpng-1.6.18\projects\Debug Library"
xcopy /S /I "..\..\windows-build\libpng-1.6.18\projects\%_VCLIB_DIR_%\Release Library\*" "..\..\windows-build\libpng-1.6.18\projects\Release Library\" > NUL 2>&1
xcopy /S /I "..\..\windows-build\libpng-1.6.18\projects\%_VCLIB_DIR_%\Debug Library\*"   "..\..\windows-build\libpng-1.6.18\projects\Debug Library\"   > NUL 2>&1
set _VCLIB_DIR_=
set _LIB_VC_VER=
:_done_library
goto _done_build
:_notice1
if "%_TRIED_CLONE%" neq "" goto _notice1_announce
if "%_GIT_GIT%" equ "" goto _notice1_announce
echo *****************************************************
echo *****************************************************
echo **                                                 **
echo ** The required build support is not yet available.**
echo **                                                 **
echo ** Using git to acquire a local copy of the        **
echo ** windows-build repository from:                  **
echo **                                                 **
echo **    https://github.com/simh/windows-build        **
echo **                                                 **
echo ** This may take a minute or so.  Please wait...   **
echo **                                                 **
echo *****************************************************
echo *****************************************************
:_try_clone
pushd ..\..
"%_GIT_GIT%" clone https://github.com/simh/windows-build windows-build
popd
set _TRIED_CLONE=1
goto _check_build
:_notice1_announce
if "%_TRIED_CURL%" neq "" goto _notice3_announce
call :FindCurl _CURL_CURL
if "%_CURL_CURL%" equ "" goto _notice3_announce
call :FindTar _TAR_TAR
if "%_TAR_TAR%" equ "" goto _notice3_announce
echo *****************************************************
echo *****************************************************
echo **                                                 **
echo ** The required build support is not yet available.**
echo **                                                 **
echo ** Using curl and tar to acquire and expand a      **
echo ** local copy of the windows-build repository      **
echo ** in archive form from:                           **
echo **                                                 **
echo **    https://github.com/simh/windows-build        **
echo **                                                 **
echo ** This may take a minute or so.  Please wait...   **
echo **                                                 **
echo *****************************************************
echo *****************************************************
pushd ..\..
%_CURL_CURL% --location https://github.com/simh/windows-build/archive/windows-build.tar.gz --output windows-build.tar.gz
%_TAR_TAR% -xzf windows-build.tar.gz
del windows-build.tar.gz
popd
set _TRIED_CURL=1
goto _check_build
:_notice3_announce
echo *****************************************************
echo *****************************************************
echo **  The required build support is not available.   **
echo *****************************************************
echo *****************************************************
set _exit_reason=The required build support is not available.
goto _ProjectInfo
:_notice2
if "%_TRIED_PULL%" neq "" goto _notice2_announce
if "%_GIT_GIT%" equ "" goto _notice2_announce
if exist ..\..\windows-build\.git goto _try_pull
echo *****************************************************
echo *****************************************************
echo **                                                 **
echo **  The required build support is out of date      **
echo **  and currently isn't a git repository.          **
echo **                                                 **
echo **  Removing the current windows-build support     **
echo **  in preparation for cloning the current         **
echo **  windows-build git repository.                  **
echo **                                                 **
echo **  This may take several minutes...               **
echo **                                                 **
echo *****************************************************
echo *****************************************************
rmdir /s /q ..\..\windows-build
goto _try_clone
:_try_pull
echo *****************************************************
echo *****************************************************
echo **                                                 **
echo **  The required build support is out of date.     **
echo **                                                 **
echo **  Attempting update of your local windows-build  **
echo **  git repository.  This may take a minute...     **
echo **                                                 **
echo *****************************************************
echo *****************************************************
pushd ..\..\windows-build
"%_GIT_GIT%" pull https://github.com/simh/windows-build
popd
set _TRIED_PULL=1
goto _check_build
:_notice2_announce
if "%_TRIED_CURL%" neq "" goto _notice4_announce
call :FindCurl _CURL_CURL
if "%_CURL_CURL%" equ "" goto _notice4_announce
call :FindTar _TAR_TAR
if "%_TAR_TAR%" equ "" goto _notice4_announce
echo *****************************************************
echo *****************************************************
echo **                                                 **
echo ** The required build support is out of date.      **
echo **                                                 **
echo ** Using curl and tar to acquire and expand a      **
echo ** local copy of the windows-build repository      **
echo ** in archive form from:                           **
echo **                                                 **
echo **    https://github.com/simh/windows-build        **
echo **                                                 **
echo ** This may take a minute or so.  Please wait...   **
echo **                                                 **
echo *****************************************************
echo *****************************************************
pushd ..\..
%_CURL_CURL% --location https://github.com/simh/windows-build/archive/windows-build.tar.gz --output windows-build.tar.gz
%_TAR_TAR% -xzf windows-build.tar.gz
del windows-build.tar.gz
popd
set _TRIED_CURL=1
goto _check_build
:_notice4_announce
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
:_notice4
echo *********************************
echo *********************************
echo **  Visual Studio Version: %_VC_VER%  Compiler Version: %_MSVC_VER% Toolset Version: %_MSVC_TOOLSET_VER% **
echo **  Visual Studio Version: %_VC_VER%  Compiler Version: %_MSVC_VER% Toolset Version: %_MSVC_TOOLSET_VER% **
echo **  Visual Studio Version: %_VC_VER%  Compiler Version: %_MSVC_VER% Toolset Version: %_MSVC_TOOLSET_VER% **
echo **  Visual Studio Version: %_VC_VER%  Compiler Version: %_MSVC_VER% Toolset Version: %_MSVC_TOOLSET_VER% **
echo *****************************************************
echo *****************************************************
echo **  Windows Build support for your Microsoft       **
echo **  Visual Studio version is not available yet.    **
echo **  Please create a new issue at:                  **
echo **  https://github.com/simh/simh/issues describing **
echo **  what you've done and support should be added   **
echo **  soon.  Otherwise, you can install an earlier   **
echo **  version of Microsoft Visual Studio and use     **
echo **  that.                                          **
echo *****************************************************
echo *****************************************************
goto _ProjectInfo
:_ProjectInfo
type 0ReadMe_Projects.txt
echo error: %_exit_reason%
echo error: Review the Output Tab for more details.
exit 1
:_done_build

:_CheckGit
if not exist ..\.git goto _done_id
call :FindGit _GIT_GIT
if "%_GIT_GIT%" neq "" goto _SetId
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo **                                                    **
echo **   Your local simh code is in a git repository,     **
echo **   however, the git program executable can not be   **
echo **   readily found on your system.                    **
echo **                                                    **
echo **   You should download and install git from:        **
echo **                                                    **
echo **        https://git-scm.com/download/win            **
echo **                                                    **
echo **   while installing git for windows, be sure to     **
echo **   select the option to "Use Git from the Windows   **
echo **   Command Prompt"                                  **
echo **                                                    **
echo **   You should logout and login again after initally **
echo ""   installing git to be sure that the installation  **
echo **   location is properly visible in your search path.**
echo **                                                    **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo error: Review the Output Tab for more details.
exit 1
:_done_git

:_SetId
rem
rem A race condition exists while creating the .git-commit-id.h file.
rem This race can happen at the beginning of a parallel build where 
rem several projects can start execution at almost the same time.
rem
SET ACTUAL_GIT_COMMIT_ID=
SET ACTUAL_GIT_COMMIT_TIME=
SET ACTUAL_GIT_COMMIT_EXTRAS=
SET GIT_COMMIT_ID=
SET GIT_COMMIT_TIME=
for /F "usebackq tokens=1" %%i in (`git update-index --refresh --`) do SET ACTUAL_GIT_COMMIT_EXTRAS=+uncommitted-changes
for /F "usebackq tokens=1" %%i in (`git log -1 "--pretty=%%H"`) do SET ACTUAL_GIT_COMMIT_ID=%%i%ACTUAL_GIT_COMMIT_EXTRAS%
for /F "usebackq tokens=1" %%i in (`git log -1 "--pretty=%%aI"`) do SET ACTUAL_GIT_COMMIT_TIME=%%i
if exist ..\.git-commit-id for /F "usebackq tokens=2" %%i in (`findstr /C:SIM_GIT_COMMIT_ID ..\.git-commit-id`) do SET GIT_COMMIT_ID=%%i
if exist ..\.git-commit-id for /F "usebackq tokens=2" %%i in (`findstr /C:SIM_GIT_COMMIT_TIME ..\.git-commit-id`) do SET GIT_COMMIT_TIME=%%i
if "%ACTUAL_GIT_COMMIT_ID%" neq "%GIT_COMMIT_ID%" "%_GIT_GIT%" log -1 --pretty="SIM_GIT_COMMIT_ID %ACTUAL_GIT_COMMIT_ID%%%nSIM_GIT_COMMIT_TIME %ACTUAL_GIT_COMMIT_TIME%" >..\.git-commit-id
SET GIT_COMMIT_ID=%ACTUAL_GIT_COMMIT_ID%
SET GIT_COMMIT_TIME=%ACTUAL_GIT_COMMIT_TIME%
SET ACTUAL_GIT_COMMIT_ID=
SET ACTUAL_GIT_COMMIT_TIME=
:_VerifyGitCommitId.h
SET OLD_GIT_COMMIT_ID=
if not exist .git-commit-id.h echo.>.git-commit-id.h
for /F "usebackq tokens=3" %%i in (`findstr /C:SIM_GIT_COMMIT_ID .git-commit-id.h`) do SET OLD_GIT_COMMIT_ID=%%i
if "%GIT_COMMIT_ID%" equ "%OLD_GIT_COMMIT_ID%" goto _IdGood
echo Generating updated .git-commit-id.h containing id %GIT_COMMIT_ID%
echo #define SIM_GIT_COMMIT_ID %GIT_COMMIT_ID% >.git-commit-id.h
echo #define SIM_GIT_COMMIT_TIME %GIT_COMMIT_TIME% >>.git-commit-id.h
if errorlevel 1 echo Retrying...
if errorlevel 1 goto _SetId
:_IdGood
:_done_id
if not exist .git-commit-id.h echo. >.git-commit-id.h
goto :EOF


:WhereInPath
if "%~$PATH:1" NEQ "" exit /B 0
exit /B 1

:WhichInPath
if "%~$PATH:1" EQU "" exit /B 1
set %2=%~$PATH:1
exit /B 0

:FindGit
set _GIT_TMP=%1
call :WhichInPath git.exe _GIT_TMP_
if "%_GIT_TMP_%" neq "" goto GitFound
call :WhichInPath cl.exe _VC_CL_
for /f "tokens=1-4 delims=\" %%a in ("%_VC_CL_%") do set _GIT_BASE_="%%a\%%b\%%c\%%d\"
for /r %_GIT_BASE_% %%a in (git.exe) do if exist "%%a" set _GIT_TMP_=%%a
if "%_GIT_TMP_%" neq "" call :AddToPath "%_GIT_TMP_%"
:GitFound
if "%_GIT_TMP_%" equ "" echo *** git not found
if "%_GIT_TMP_%" neq "" set %_GIT_TMP%=%_GIT_TMP_%
set _VC_CL_=
set _GIT_BASE_=
set _GIT_TMP_=
set _GIT_TMP=
exit /B 0

:AddToPath
set _TO_PATH=%~p1
PATH %PATH%;%_TO_PATH%
set _TO_PATH=
exit /B 0

:FindTar
set _TAR_TMP=%1
call :WhichInPath tar.exe _TAR_TMP_
set %_TAR_TMP%=%_TAR_TMP_%
set _TAR_TMP_=
set _TAR_TMP=
exit /B 0

:FindCurl
set _CURL_TMP=%1
call :WhichInPath curl.exe _CURL_TMP_
set %_CURL_TMP%=%_CURL_TMP_%
set _CURL_TMP_=
set _CURL_TMP=
exit /B 0

:FindVCVersion
call :WhichInPath cl.exe _VC_CL_
for /f "tokens=3-10 delims=\" %%a in ("%_VC_CL_%") do call :VCCheck _VC_VER_NUM_ "%%a" "%%b" "%%c" "%%d" "%%e" "%%f" "%%g" "%%h"
for /f "delims=." %%a in ("%_VC_VER_NUM_%") do set %1=%%a
set _VC_CL_STDERR_=%TEMP%\cl_stderr%_TARGET%.tmp
set VS_UNICODE_OUTPUT=
"%_VC_CL_%" /? 2>"%_VC_CL_STDERR_%" 1>NUL 
for /f "usebackq tokens=4-9" %%a in (`findstr Version "%_VC_CL_STDERR_%"`) do call :MSVCCheck _MSVC_VER_NUM_ "%%a" "%%b" "%%c" "%%d" "%%e"
if "%4" NEQ "" set %4=%_MSVC_TOOLSET_%
if "%_MSVC_TOOLSET_%" NEQ "" set _MSVC_TOOLSET_=v%_MSVC_TOOLSET_:~0,2%%_MSVC_TOOLSET_:~3,1%
if "%3" NEQ "" set %3=%_MSVC_TOOLSET_%
set _MSVC_TOOLSET_=
set %2=%_MSVC_VER_NUM_%
set _MSVC_VER_NUM_=
for /f "delims=." %%a in ("%_MSVC_VER_NUM_%") do set %2=%%a
del %_VC_CL_STDERR_%
set _VC_CL_STDERR_=
set _VC_CL=
exit /B 0

:: Scan the elements of the file path of cl.exe to determine the Visual
:: Studio Version and potentially the toolset version
:VCCheck
set _VC_TMP=%1
set _VC_TOOLSET=
:_VCCheck_Next
shift
set _VC_TMP_=%~1
if "%_VC_TMP_%" equ "" goto _VCCheck_Done
if "%_VC_TMP_:~0,24%" EQU "Microsoft Visual Studio " set %_VC_TMP%=%_VC_TMP_:Microsoft Visual Studio =%
call :IsNumeric _VC_NUM_ %_VC_TMP_%
if "%_VC_NUM_%" neq "" set %_VC_TMP%=%~1
if "%_VC_NUM_%" neq "" goto _VCCheck_Done
goto _VCCheck_Next
:_VCCheck_Done
set _VC_TMP=_MSVC_TOOLSET_
:_VCTSCheck_Next
shift
set _VC_TMP_=%~1
if "%_VC_TMP_%" equ "" goto _VCTSCheck_Done
call :IsNumeric _VC_NUM_ %_VC_TMP_%
if "%_VC_NUM_%" neq "" set %_VC_TMP%=%~1
if "%_VC_NUM_%" neq "" goto _VCTSCheck_Done
goto _VCTSCheck_Next
:_VCTSCheck_Done
set _VC_TMP_=
set _VC_TMP=
set _VC_NUM_=
exit /B 0

:MSVCCheck
set _MSVC_TMP=%1
:_MSVCCheck_Next
shift
set _MSVC_TMP_=%~1
if "%_MSVC_TMP_%" equ "" goto _VCCheck_Done
call :IsNumeric _MSVC_NUM_ %_MSVC_TMP_%
if "%_MSVC_NUM_%" neq "" set %_MSVC_TMP%=%~1
if "%_MSVC_NUM_%" neq "" goto _MSVCCheck_Done
goto _MSVCCheck_Next
:_MSVCCheck_Done
set _MSVC_TMP_=
set _MSVC_TMP=
set _MSVC_NUM_=
exit /B 0

:CheckDirectoryVCSupport
set _VC_Check_Path=%~3%~2/
set _VC_Check_Path=%_VC_Check_Path:/=\%
set _X_VC_VER=
set _XX_VC_VER_DIR=lib-VC%_VC_VER%
if "%_XX_VC_VER_DIR%" equ "lib-VC9" set _XX_VC_VER_DIR=lib-VC2008
if exist "%_VC_Check_Path%\VisualCVersionSupport.txt" for /F "usebackq tokens=2*" %%i in (`findstr /C:"_VC_VER=%_VC_VER% " "%_VC_Check_Path%\VisualCVersionSupport.txt"`) do SET _X_VC_VER=%%i %%j
if "%_XX_VC_VER_DIR%" neq "%2" exit /B 0
if "%_VC_VER%" equ "2022" set _VC_Check_Path=%_VC_Check_Path%%_MSVC_VER%\
if not exist "%_VC_Check_Path%VisualCVersionSupport.txt" exit /B 1
for /F "usebackq tokens=2*" %%k in (`findstr /C:"_VC_VER=%_VC_VER% " "%_VC_Check_Path%VisualCVersionSupport.txt"`) do set %1=%_VC_Check_Path%
exit /B 0

:IsNumeric
set _Numeric_TMP_=%~1
set _Numeric_Test_=%2
set _Numeric_Test_=%_Numeric_Test_:~0,1%
set %_Numeric_TMP_%=
if "%_Numeric_Test_%"=="0" set %_Numeric_TMP_%=1
if "%_Numeric_Test_%"=="1" set %_Numeric_TMP_%=1
if "%_Numeric_Test_%"=="2" set %_Numeric_TMP_%=1
if "%_Numeric_Test_%"=="3" set %_Numeric_TMP_%=1
if "%_Numeric_Test_%"=="4" set %_Numeric_TMP_%=1
if "%_Numeric_Test_%"=="5" set %_Numeric_TMP_%=1
if "%_Numeric_Test_%"=="6" set %_Numeric_TMP_%=1
if "%_Numeric_Test_%"=="7" set %_Numeric_TMP_%=1
if "%_Numeric_Test_%"=="8" set %_Numeric_TMP_%=1
if "%_Numeric_Test_%"=="9" set %_Numeric_TMP_%=1
set _Numeric_TMP_=
set _Numeric_Test_=
exit /B 0
