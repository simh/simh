@echo off
:: Rebuild all of SIMH simulators using Visual Studio
::
:: If this procedure is not invoked from a Developer command prompt
:: then the VS2008 tools are preferred if VS2008 is installed, 
:: otherwise the installed Visual Studio tools will be used 
:: preferring newer Visual Studio versions over older ones.
::
:: If this is invoked with Visual Studio 2022 or 2026 installed 
:: along with the "C++ for Windows Support for VS 2017 (v141) tools"
:: option installed, then the project files will be converted, if 
:: needed, to leverage the stable windows_build support that doesn't 
:: change at least every month.
::
:: If this procedure is invoked from a Developer command prompt
:: then the tool chain provided with the command prompt is used
:: to build the simh projects.
::
:: A single argument to this procedure may be the word Debug, which 
:: will cause Debug binaries to be built rather than the Release 
:: binaries which is the default.
::
:: The default is to build all simulators mentioned in the simh solution.
:: Optionally, individual simulators may be built by listing the specific
:: simulator name(s) on the command line invoking this procedure.
::
:: Individual simulator sources are in .\simulator_name
:: Individual simulator executables are produced in .\BIN\NT\Win32-{Debug or Release}\
::
::

:: Initialize target variables
set _BUILD_CONFIG=Release
set _BUILD_PROJECTS=
set _REBUILD_PROJECTS=
set _BUILD_PROJECT_NAMES=
set _BUILD_PROJECT_DIR=%~dp0Visual Studio Projects\
:_CheckArg
if "%1" == "" goto _DoneArgs
if /i "%1" == "Debug" set _BUILD_CONFIG=Debug& shift & goto _CheckArg
if /i "%1" == "Release" set _BUILD_CONFIG=Release& shift & goto _CheckArg
call :GetFileName "%_BUILD_PROJECT_DIR%%1.vcproj" _BUILD_PROJECT
if exist "%_BUILD_PROJECT_DIR%%1.vcproj" set _BUILD_PROJECTS=%_BUILD_PROJECTS%;%_BUILD_PROJECT%
if exist "%_BUILD_PROJECT_DIR%%1.vcproj" set _REBUILD_PROJECTS=%_REBUILD_PROJECTS%;%_BUILD_PROJECT%:Rebuild
if exist "%_BUILD_PROJECT_DIR%%1.vcproj" set _BUILD_PROJECT_NAMES=%_BUILD_PROJECT_NAMES% %_BUILD_PROJECT%
if exist "%_BUILD_PROJECT_DIR%%1.vcproj" shift & goto _CheckArg
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR
echo **
echo ** No such project: %1
echo **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR
exit /b 1

:_DoneArgs
set _VC_VER=
if not "%VSINSTALLDIR%" == "" set _VC_DIR=%VSINSTALLDIR%
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
set _VC_DIR=%ProgramFiles(x86)%\Microsoft Visual Studio 9.0
if exist "%_VC_DIR%\VC\vcvarsall.bat" call "%_VC_DIR%\VC\vcvarsall.bat" 
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
set _VC_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise
if exist "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat" call "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
set _VC_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Professional
if exist "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat" call "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
set _VC_DIR=%ProgramFiles%\Microsoft Visual Studio\2022\Community
if exist "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat" call "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
set _VC_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise
if exist "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat" call "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
set _VC_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Professional
if exist "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat" call "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
set _VC_DIR=%ProgramFiles%\Microsoft Visual Studio\18\Community
if exist "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat" call "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
set _VC_DIR=%ProgramFiles%\Microsoft Visual Studio\2019\Community
if exist "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat" call "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
set _VC_DIR=%ProgramFiles%\Microsoft Visual Studio\2017\Community
if exist "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat" call "%_VC_DIR%\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x86
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" x86
call :FindVCVersion _VC_VER _MSVC_VER _MSVC_TOOLSET_VER  _MSVC_TOOLSET_DIR
if not "%_VC_VER%" == "" goto GotVC

echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo **                                                    **
echo **   I can't find a Visual Studio version installed   **
echo **   in the default location on this system.          **
echo **                                                    **
echo **   If you haven't installed any version of Visual   **
echo **   Studio yet, you must install one before this     **
echo **   procedure can be used.  The earliest Visual      **
echo **   Studio version that is supported is VS2008 which **
echo **   can be downloaded and installed from:            **
echo **                                                    **
echo http://download.microsoft.com/download/E/8/E/E8EEB394-7F42-4963-A2D8-29559B738298/VS2008ExpressWithSP1ENUX1504728.iso
echo **                                                    **
echo **   Newer versions of Visual Studio are also         **
echo **   supported, but the initial build will have to    **
echo **   convert the VS2008 project definitions.          **
echo **                                                    **
echo **   If you installed a version of Visual Studio C++  **
echo **   in a non default location, then you must invoke  **
echo **   this procedure from a developer command prompt   **
echo **   for the version of Visual Studio you have        **
echo **   installed.                                       **
echo **                                                    **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
echo ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR ** ERROR **
exit /b 1

:WhichInPath
if "%~$PATH:1" EQU "" exit /B 1
set %2=%~$PATH:1
exit /B 0

:FindVCVersion
call :WhichInPath cl.exe _VC_CL_
for /f "tokens=3-10 delims=\" %%a in ("%_VC_CL_%") do call :VCCheck _VC_VER_NUM_ "%%a" "%%b" "%%c" "%%d" "%%e" "%%f" "%%g" "%%h"
for /f "delims=." %%a in ("%_VC_VER_NUM_%") do set %1=%%a
set _VC_CL_STDERR_=%TEMP%\cl_stderr%_TARGET%.tmp
set VS_UNICODE_OUTPUT=
"%_VC_CL_%" /? 2>"%_VC_CL_STDERR_%" 1>NUL <NUL
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
if "%~1" equ "18" set %_VC_TMP%=2026
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
if "%~1" equ "18" set %_VCTMP%=2026
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

:GetFileName
set %2=%~n1
exit /B 0

:GotVC
if "%_BUILD_PROJECT_NAMES%" == "" echo Building All Projects with %_BUILD_CONFIG% Configuration
if not "%_BUILD_PROJECT_NAMES%" == "" echo Building%_BUILD_PROJECT_NAMES% Projects with %_BUILD_CONFIG% Configuration
echo Building with Visual Studio Components from %_VC_DIR%
if "%_VC_VER%" == "18" set _VC_VER=2026
set _BUILD_PARALLEL=8
if %_BUILD_PARALLEL% GTR %NUMBER_OF_PROCESSORS% set _BUILD_PARALLEL=%NUMBER_OF_PROCESSORS%
set _SLN_FILE=%_BUILD_PROJECT_DIR%Simh.sln
if exist "%_BUILD_PROJECT_DIR%Simh-%_VC_VER%.sln" set _SLN_FILE=%_BUILD_PROJECT_DIR%Simh-%_VC_VER%.sln
SET _X_SLN_VERSION=
echo _SLN_FILE=%_SLN_FILE%
for /F "usebackq tokens=8" %%a in (`findstr /C:"Microsoft Visual Studio Solution File, Format Version" "%_SLN_FILE%"`) do SET _X_SLN_VERSION=%%a

if not "%_VC_VER%" == "9" goto _DoMSBuild
if "%_BUILD_PROJECTS%" == "" vcbuild /nologo /M%_BUILD_PARALLEL% /useenv /rebuild "%_SLN_FILE%" "%_BUILD_CONFIG%|Win32" & goto :EOF

set _BUILD_PROJECTS=%_BUILD_PROJECTS:~1%
:_NextProject
set _BUILD_PROJECT=
for /f "tokens=1* delims=;" %%a in ("%_BUILD_PROJECTS%") do set _BUILD_PROJECT=%%a& set _BUILD_PROJECTS=%%b
if "%_BUILD_PROJECT%" == "" goto :EOF
echo.
echo Building %_BUILD_PROJECT%
vcbuild /nologo /useenv /rebuild "%_BUILD_PROJECT_DIR%%_BUILD_PROJECT%.vcproj" "%_BUILD_CONFIG%|Win32" 
goto _NextProject

:_DoMSBuild
if "%_X_SLN_VERSION%" == "10.00" set _NEW_SLN_FILE=%_BUILD_PROJECT_DIR%Simh-%_VC_VER%.sln
if "%_X_SLN_VERSION%" == "10.00" copy /y "%_SLN_FILE%" "%_NEW_SLN_FILE%" >NUL & echo Converting the VS2008 projects to VS%_VC_VER%, this will take several (5-8) minutes... & echo Project conversion starting at %TIME% & DevEnv /Upgrade "%_NEW_SLN_FILE%" & set _SLN_FILE=%_NEW_SLN_FILE%
if not "%_NEW_SLN_FILE%" == "" echo Project conversion completed at %TIME%
set _NEW_SLN_FILE=
if not "%_VC_VER%" == "2022" if not "%_VC_VER%" == "2026" goto _RunBuild
if exist "%_VC_DIR%\MSBuild\Microsoft\VC\v150\Platforms\Win32\PlatformToolsets\v141" goto _DoV141Convert
for /F "usebackq tokens=8" %%a in (`findstr /C:"Microsoft Visual Studio Solution File, Format Version" "%_SLN_FILE%"`) do SET _X_SLN_VERSION=%%a
if "%_X_SLN_VERSION%" == "10.00" goto _DoV141Convert
goto _RunBuild

:_DoV141Convert
set _X_PROJS_CONVERTED=
for /F "usebackq tokens=1" %%a in (`findstr /C:"<WindowsTargetPlatformVersion>10." "%_BUILD_PROJECT_DIR%BuildROMs.vcxproj"`) do set _X_PROJS_CONVERTED=%%a
if not "%_X_PROJS_CONVERTED%" == "" goto _RunBuild
echo v141 Convert starting at %TIME%
echo Converting the VS2022 or VS2026 projects to used the 2017 support libraries
Powershell -NoLogo -File "%~dp0\Visual Studio Projects\ConvertToV141Project.ps1" "%_SLN_FILE%"
echo v141 Convert completed at %TIME%
set _X_PROJS_CONVERTED=

:_RunBuild
if "%_BUILD_PROJECTS%" == "" MSBuild /nologo "%_SLN_FILE%" /maxCpuCount:%_BUILD_PARALLEL% /Target:Rebuild /Property:Configuration=%_BUILD_CONFIG% /Property:Platform=Win32 /fileLogger "/fileLoggerParameters:LogFile=%_BUILD_PROJECT_DIR%Build-VS%_VC_VER%.log" & goto :EOF
set _BUILD_PROJECTS=%_BUILD_PROJECTS:~1%
set _REBUILD_PROJECTS=%_REBUILD_PROJECTS:~1%
MSBuild /nologo "%_SLN_FILE%" /maxCpuCount:%_BUILD_PARALLEL% /Target:%_REBUILD_PROJECTS% /Property:Configuration=%_BUILD_CONFIG% /Property:Platform=Win32 "/fileLoggerParameters:LogFile=%_BUILD_PROJECT_DIR%Build-VS%_VC_VER%.log" & goto :EOF
set _SLN_FILE=
