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
rem       - Converting Visual Studio Projects to a form which will produce 
rem         binaries which run on Windows XP if the current build environment 
rem         supports it and the correct components are installed.
rem         This activity is triggered by the first argument being the name
rem         of a the current Visual Studio project file.  This argument MUST 
rem         only be provided on a single project which invokes this procedure
rem         AND that project should be one which all other projects in a 
rem         solution are dependent on.
rem
rem

if "%~x1" == ".vcproj" goto _done_xp_check
if not "%~x1" == ".vcxproj" goto _done_project
if exist PlatformToolset.fix goto _project_cleanup
findstr PlatformToolset %1 >NUL
if ERRORLEVEL 1 goto _next_arg
findstr PlatformToolset %1 | findstr _xp >NUL
if not ERRORLEVEL 1 goto _done_xp_check
echo warning: The %~n1.exe binary can't run on windows XP.
set _XP_Support_Available=
for /r "%PROGRAMDATA%" %%z in (packages\XPSupport\Win_XPSupport.msi) do if exist "%%z" set _XP_Support_Available=1
if "" == "%_XP_Support_Available%" goto _done_xp_check
if exist PlatformToolset.fix exit /b 1
echo.                                                                              >>PlatformToolset.fix
if ERRORLEVEL 1 exit /B 1
echo warning: Adding Windows XP suppport to all project files at %TIME%

call :FindVCVersion _VC_VER
echo Set objFSO = CreateObject("Scripting.FileSystemObject")                           >>%1.fix.vbs
echo Set objFile = objFSO.OpenTextFile(Wscript.Arguments(0), 1)                        >>%1.fix.vbs
echo.                                                                                  >>%1.fix.vbs
echo strText = objFile.ReadAll                                                         >>%1.fix.vbs
echo objFile.Close                                                                     >>%1.fix.vbs
echo strText = Replace(strText, "</PlatformToolset>", "_xp</PlatformToolset>")         >>%1.fix.vbs
echo.                                                                                  >>%1.fix.vbs
if %_VC_VER% GEQ 14 echo strText = Replace(strText,                                   _>>%1.fix.vbs
if %_VC_VER% GEQ 14 echo          "__CLEANUP_C</PreprocessorDefinitions>",            _>>%1.fix.vbs
if %_VC_VER% GEQ 14 echo "__CLEANUP_C;_USING_V110_SDK71_</PreprocessorDefinitions>")  _>>%1.fix.vbs
if %_VC_VER% GEQ 14 echo.                                                              >>%1.fix.vbs
echo Set objFile = objFSO.OpenTextFile(Wscript.Arguments(0), 2)                        >>%1.fix.vbs
echo objFile.Write strText                                                             >>%1.fix.vbs
echo objFile.Close                                                                     >>%1.fix.vbs

call :_Fix_PlatformToolset %1 %1
for %%f in (*.vcxproj) do call :_Fix_PlatformToolset %1 %%f
call :_GitHooks
del %1.fix.vbs
rem wait a bit here to allow a parallel build of the to complete additional projects
echo Error: Reload the changed projects and start the build again
exit /B 1
:_Fix_PlatformToolset
findstr PlatformToolset %2 >NUL
if ERRORLEVEL 1 exit /B 0
findstr PlatformToolset %2 | findstr _xp >NUL
if not ERRORLEVEL 1 exit /B 0
echo Adding XP support to project %2
cscript %1.fix.vbs %2 > NUL 2>&1
exit /B 0
:_done_xp_check
shift
goto _done_project
:_project_cleanup
shift
del PlatformToolset.fix 
:_done_project
if exist PlatformToolset.fix echo error: Reload any changed projects and rebuild again,
if exist PlatformToolset.fix exit /b 1


:_next_arg
if "%1" == "" goto _done_args
set _arg=
if /I "%1" == "ROM"      set _arg=ROM
if /I "%1" == "BUILD"    set _arg=BUILD
if /I "%1" == "LIBSDL"   set _arg=LIBSDL
if /I "%1" == "LIBPCRE"  set _arg=LIBPCRE
if /I "%1" == "FINDFONT" set _arg=FINDFONT
if "%_arg%" == ""        echo *** warning *** unknown parameter %1
if /I "%1" == "FINDFONT" set _X_FontName=%2
if /I "%1" == "FINDFONT" set _X_FontIncludeName=%3
if /I "%_arg%" == "FINDFONT" shift
if /I "%_arg%" == "FINDFONT" shift
if not "%_arg%" == ""    set _X_%_arg%=%_arg%
shift
goto _next_arg
:_done_args
rem some arguments implicitly require BUILD to also be set to have 
rem any meaning.  These are LIBSDL, LIBPCRE and FINDFONT
if not "%_X_FINDFONT%"  == "" set _X_BUILD=BUILD
if not "%_X_LIBSDL%"    == "" set _X_BUILD=BUILD
if not "%_X_LIBPCRE%"   == "" set _X_BUILD=BUILD


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
findstr "/c:_MSC_VER >= 1900" ..\..\windows-build\pthreads\pthread.h >NUL
if ERRORLEVEL 1 goto _notice2
if "%_X_LIBSDL%" == "" goto _done_libsdl
if not exist ../../windows-build/libSDL/SDL2-2.0.5/include/SDL.h goto _notice2
if not exist "..\..\windows-build\libpng-1.6.18\projects\vstudio\Release Library\*" goto _notice2
if not exist "../../windows-build/libSDL/Microsoft DirectX SDK (June 2010)/Lib/x86/dxguid.lib" goto _notice2
findstr "/c:LIBSDL_FTOL2_SSE" ..\..\windows-build\Windows-Build_Versions.txt >NUL
if ERRORLEVEL 1 goto _notice2
findstr "/c:LIBSDL_ALLMUL" ..\..\windows-build\Windows-Build_Versions.txt >NUL
if ERRORLEVEL 1 goto _notice2
findstr "/c:LIBSDL_ALLSHR" ..\..\windows-build\Windows-Build_Versions.txt >NUL
if ERRORLEVEL 1 goto _notice2
:_done_libsdl
if "%_X_LIBPCRE%" == "" goto _done_libpcre
if not exist ../../windows-build/PCRE/include/pcreposix.h goto _notice2
:_done_libpcre
if "%_X_FINDFONT%" == "" goto _done_findfont
if not exist ../../windows-build/libSDL/SDL2_ttf-2.0.12/SDL_ttf.h goto _notice2
if "%_X_FontName%" == "" goto _done_findfont
echo. >%_X_FontIncludeName%.temp
set FONTFILE=%windir%\Fonts\%_X_FontName%
if not exist "%FONTFILE%" echo Can't find font %_X_FontName%
if not exist "%FONTFILE%" goto _done_findfont
set FONTFILE=%FONTFILE:\=/%
echo #define FONTFILE %FONTFILE% >>%_X_FontIncludeName%.temp
if not exist %_X_FontIncludeName% goto _found_font
fc %_X_FontIncludeName%.temp %_X_FontIncludeName% >NUL
if NOT ERRORLEVEL 1 goto _done_findfont
:_found_font
echo Found: %FONTFILE%
move /Y %_X_FontIncludeName%.temp %_X_FontIncludeName% >NUL
:_done_findfont
if exist %_X_FontIncludeName%.temp del %_X_FontIncludeName%.temp
call :FindVCVersion _VC_VER
if not exist "..\..\windows-build\libpng-1.6.18\projects\Release Library" goto _setup_library
if not exist "..\..\windows-build\libpng-1.6.18\projects\Release Library\VisualC.version" set _LIB_VC_VER=9
if exist "..\..\windows-build\libpng-1.6.18\projects\Release Library\VisualC.version" for /f "usebackq delims=." %%v in (`type "..\..\windows-build\libpng-1.6.18\projects\Release Library\VisualC.version"`) do set _LIB_VC_VER=%%v
if %_LIB_VC_VER% EQU %_VC_VER% goto _done_library
if %_VC_VER% GEQ 14 goto _check_new_library
if %_LIB_VC_VER% LSS 14 goto _done_library
goto _setup_library
:_check_new_library
if %_LIB_VC_VER% GEQ 14 godo _done_library
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

:WhichInPath
if "%~$PATH:1" EQU "" exit /B 1
set %2=%~$PATH:1
exit /B 0

:FindVCVersion
call :WhichInPath cl.exe _VC_CL_
for /f "tokens=2-8 delims=\" %%a in ("%_VC_CL_%") do call :VCCheck _VC_VER_NUM_ "%%a" "%%b" "%%c" "%%d" "%%e" "%%f" "%%g" 
for /f "delims=." %%a in ("%_VC_VER_NUM_%") do set %1=%%a
set _VC_CL=
exit /B 0

:VCCheck
set _VC_TMP=%1
:_VCCheck_Next
shift
set _VC_TMP_=%~1
if "%_VC_TMP_%" equ "" goto _VCCheck_Done
if "%_VC_TMP_:~0,24%" EQU "Microsoft Visual Studio " set %_VC_TMP%=%_VC_TMP_:Microsoft Visual Studio =%
goto _VCCheck_Next
:_VCCheck_Done
set _VC_TMP_=
set _VC_TMP=
exit /B 0
