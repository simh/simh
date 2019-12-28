@echo off
:: Rebuild all of SIMH simulators using Visual Studio
::
:: If this procedure is not invoked from a Developer command prompt
:: then the VS2008 tools are preferred if VS2008 is installed, 
:: otherwise the installed Visual Studio tools will be used 
:: prefering newer Visual Studio versions over older ones.
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
:: simulator names on the command line invoking this procedure.
::
:: Individual simulator sources are in .\simulator_name
:: Individual simulator executables are produced in .\BIN\NT\Win32-{Debug or Release}\
::
::

:: Initialize target variables
set _BUILD_CONFIG=Release
set _BUILD_PROJECTS=
set _REBUILD_PROJECTS=
set _BUILD_PROJECT_DIR=%~dp0\Visual Studio Projects\
:_CheckArg
if "%1" == "" goto _DoneArgs
if /i "%1" == "Debug" set _BUILD_CONFIG=Debug& shift & goto _CheckArg
if /i "%1" == "Release" set _BUILD_CONFIG=Release& shift & goto _CheckArg
call :GetFileName "%_BUILD_PROJECT_DIR%%1.vcproj" _BUILD_PROJECT
if exist "%_BUILD_PROJECT_DIR%%1.vcproj" set _BUILD_PROJECTS=%_BUILD_PROJECTS%;%_BUILD_PROJECT%
if exist "%_BUILD_PROJECT_DIR%%1.vcproj" set _REBUILD_PROJECTS=%_REBUILD_PROJECTS%;%_BUILD_PROJECT%:Rebuild
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
call :FindVCVersion _VC_VER
if not "%_VC_VER%" == "" goto GotVC
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 9.0\VC\vcvarsall.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio 9.0\VC\vcvarsall.bat" 
call :FindVCVersion _VC_VER
if not "%_VC_VER%" == "" goto GotVC
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars32.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER
if not "%_VC_VER%" == "" goto GotVC
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars32.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars32.bat"
call :FindVCVersion _VC_VER
if not "%_VC_VER%" == "" goto GotVC
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" x86
call :FindVCVersion _VC_VER
if not "%_VC_VER%" == "" goto GotVC
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio 12.0\VC\vcvarsall.bat" x86
call :FindVCVersion _VC_VER
if not "%_VC_VER%" == "" goto GotVC
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" x86
call :FindVCVersion _VC_VER
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
for /f "tokens=3-9 delims=\" %%a in ("%_VC_CL_%") do call :VCCheck _VC_VER_NUM_ "%%a" "%%b" "%%c" "%%d" "%%e" "%%f" "%%g"
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
call :IsNumeric _VC_NUM_ %_VC_TMP_%
if "%_VC_NUM_%" neq "" set %_VC_TMP%=%~1
if "%_VC_NUM_%" neq "" goto _VCCheck_Done
goto _VCCheck_Next
:_VCCheck_Done
set _VC_TMP_=
set _VC_TMP=
set _VC_NUM_=
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
set _BUILD_PARALLEL=8
if %_BUILD_PARALLEL% GTR %NUMBER_OF_PROCESSORS% set _BUILD_PARALLEL=%NUMBER_OF_PROCESSORS%
SET _X_SLN_VERSION=
for /F "usebackq tokens=8" %%a in (`findstr /C:"Microsoft Visual Studio Solution File, Format Version" "%_BUILD_PROJECT_DIR%Simh.sln"`) do SET _X_SLN_VERSION=%%a

if not "%_VC_VER%" == "9" goto _DoMSBuild
if "%_BUILD_PROJECTS%" == "" vcbuild /nologo /M%_BUILD_PARALLEL% /useenv /rebuild "%_BUILD_PROJECT_DIR%Simh.sln" "%_BUILD_CONFIG%|Win32" & goto :EOF

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
if "%_X_SLN_VERSION%" == "10.00" echo Converting the VS2008 projects to VS%_VC_VER%, this will take several (3-5) minutes & DevEnv /Upgrade "%_BUILD_PROJECT_DIR%Simh.sln"
if "%_BUILD_PROJECTS%" == "" MSBuild /nologo "%_BUILD_PROJECT_DIR%Simh.sln" /maxCpuCount:%_BUILD_PARALLEL% /Target:Rebuild /Property:Configuration=%_BUILD_CONFIG% /Property:Platform=Win32 & goto :EOF

set _BUILD_PROJECTS=%_BUILD_PROJECTS:~1%
set _REBUILD_PROJECTS=%_REBUILD_PROJECTS:~1%
MSBuild /nologo "%_BUILD_PROJECT_DIR%Simh.sln" /maxCpuCount:%_BUILD_PARALLEL% /Target:%_REBUILD_PROJECTS% /Property:Configuration=%_BUILD_CONFIG% /Property:Platform=Win32 & goto :EOF
