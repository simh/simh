rem
rem  This script will build all the simulators in the current branch 
rem  (presumed to be master) and package the resulting windows binaries
rem  into a zip file which will be named for the revision, build date and 
rem  git commit id.  The resulting zip file will be pushed to the github
rem  Win32-Development-Binaries repository for public access.
rem
rem  We're using a github repository for this purpose since github no longer
rem  supports a Download files facility since some folks were using it to 
rem  contain large binary files.  The typical set of simh windows binaries
rem  is about 35MB in size and the plan is to delete and recreate the whole
rem  Win32-Development-Binaries repository at least every few months.
rem
rem  If this script is invoked with a single parameter "reset", the local
rem  repository will be wiped out and reset.  This should be done AFTER
rem  the github one is deleted and recreated.  The github repo should be
rem  deleted by visiting https://github.com/simh/Win32-Development-Binaries/settings
rem  and clicking on the "Delete this repository" button and entering
rem  Win32-Development-Binaries
rem  and then clicking on the "I understand the consequences, delete this repository" button.
rem  It should be recreated by visiting https://github.com/new and selecting
rem  simh in the Owner drop down and entering Win32-Development-Binaries in
rem  the repository name text box.  Then enter "Current Windows Binaries" in
rem  the Description text box and click on the green "Create repository" button.
rem
rem  This procedure depends on:
rem     - Visual Studio 2008 (Express) tools to compile the desired 
rem       simulators.  The compiler and its related pieces
rem       must be installed in the windows %ProgramFiles% directory.
rem     - git.exe (installed as part of GitExtensions)
rem     - git credentials available which have write access to the 
rem       github simh/Win32-Development-Binaries repository.
rem     - 7-Zip (7z.exe) to package the compiled simulators into
rem       a zip file.
rem
rem

set BIN_REPO=Win32-Development-Binaries
set REMOTE_REPO=git@github.com:simh/%BIN_REPO%.git
call :WhereInPath git.exe > NUL 2>&1
if %ERRORLEVEL% equ 0 goto GitOK
if exist "%ProgramFiles%\Git\bin\git.exe"      set PATH=%USERPROFILE%\bin;%ProgramFiles%\Git\local\bin;%ProgramFiles%\Git\mingw\bin;%ProgramFiles%\Git\bin\;%PATH%&goto GitOK
if exist "%ProgramFiles(x86)%\Git\bin\git.exe" set PATH=%USERPROFILE%\bin;%ProgramFiles(x86)%\Git\local\bin;%ProgramFiles(x86)%\Git\mingw\bin;%ProgramFiles(x86)%\Git\bin\;%PATH%&goto GitOK
@echo off
echo **** ERROR ****  git is unavailable
exit /B 1
:GitOK

if "" equ "%1" goto ArgOK
if "reset" equ "%1" goto ArgOK
@echo off
echo **** ERROR **** Invalid argument
echo.
echo Usage: %0 {reset}
echo.
echo   invoking with the parameter "reset" (no quotes) will clean out 
echo   the local repository and push a newly create repo to github.
echo   This should be done AFTER the github one is deleted and recreated.
echo   See the comments at the beginning of %0 for complete 
echo   instructions about how to setup the github repo.
exit /B 1
:ArgOK

call :WhereInPath 7z.exe > NUL 2>&1
if %ERRORLEVEL% equ 0 goto ZipOK
if exist "%ProgramFiles%\7-Zip\7z.exe"      set PATH=%PATH%;%ProgramFiles%\7-Zip\&goto ZipOK
if exist "%ProgramFiles(x86)%\7-Zip\7z.exe" set PATH=%PATH%;%ProgramFiles(x86)%\7-Zip\&goto ZipOK
if exist "%PROGRAMW6432%\7-Zip\7z.exe"      set PATH=%PATH%;%PROGRAMW6432%\7-Zip\&goto ZipOK
@echo off
echo **** ERROR **** 7-Zip is unavailable
exit /B 1
:ZipOK

rem  Now locate the Visual Studio (VC++) 2008 components and setup the environment variables to use them
if exist "%VS90COMNTOOLS%\..\..\VC\bin\vcvars32.bat" goto VS2008Found
@echo off
echo **** ERROR **** Visual Studio 2008 is unavailable
exit /B 1
:VS2008Found
call "%VS90COMNTOOLS%\..\..\VC\bin\vcvars32.bat"
rem  If later versions of Visual Studio are also installed, there can be some confusion about 
rem  where the Windows SDK is located.
call :WhereInInclude winsock2.h > NUL 2>&1
if %ERRORLEVEL% equ 0 goto VSOK
rem  need to fixup the Windows SDK reference to use the SDK which was packaged with Visual VC++ 2008
if exist "%PROGRAMW6432%\Microsoft SDKs\Windows\v6.0A\include\winsock2.h" set INCLUDE=%INCLUDE%%PROGRAMW6432%\Microsoft SDKs\Windows\v6.0A\include;
if exist "%PROGRAMW6432%\Microsoft SDKs\Windows\v6.0A\lib\wsock32.lib" set LIB=%LIB%%PROGRAMW6432%\Microsoft SDKs\Windows\v6.0A\LIB;
if exist "%PROGRAMW6432%\Microsoft SDKs\Windows\v6.0A\bin\mt.exe" set PATH=%PATH%;%PROGRAMW6432%\Microsoft SDKs\Windows\v6.0A\bin;
call :WhereInInclude winsock2.h > NUL 2>&1
if %ERRORLEVEL% equ 0 goto VSOK
@echo off
echo **** ERROR **** Can't locate the Windows SDK include and library files
exit /B 1
:VSOK

rem  This procedure must be located in the "Visual Studio Projects\Win32-Development-Binaries" directory.
rem  The git repository directory (.git) is located relative to that directory.
cd %~p0
SET GIT_COMMIT_ID=
SET GIT_COMMIT_TIME=
pushd ..\..
git update-index --refresh -- | findstr update > NUL
if not ERRORLEVEL 1 echo **** ERROR **** the local repo has uncommitted files & popd & goto :EOF
git remote | findstr origin > NUL
if ERRORLEVEL 1 echo **** ERROR **** missing 'origin' remote in this repo & popd & goto :EOF
git checkout --quiet master
git fetch origin master
git log -1 --pretty="SIM_GIT_COMMIT_ID %%H%%nSIM_GIT_COMMIT_TIME %%aI" >.git-commit-id
popd
pushd ..
for /F "usebackq tokens=2" %%i in (`findstr /C:SIM_GIT_COMMIT_ID ..\.git-commit-id`) do set GIT_COMMIT_ID=%%i
for /F "usebackq tokens=2" %%i in (`findstr /C:SIM_GIT_COMMIT_TIME ..\.git-commit-id`) do set GIT_COMMIT_TIME=%%i
for /F "tokens=1 delims=-T" %%i in ("%GIT_COMMIT_TIME%") do set D_YYYY=%%i
for /F "tokens=2 delims=-T" %%i in ("%GIT_COMMIT_TIME%") do set D_MM=%%i
for /F "tokens=3 delims=-T" %%i in ("%GIT_COMMIT_TIME%") do set D_DD=%%i
set _SIM_MAJOR=
set _SIM_MINOR=
set _SIM_PATCH=
set _SIM_VERSION_MODE=
for /F "usebackq tokens=3" %%i in (`findstr/C:"#define SIM_MAJOR" ..\sim_rev.h`) do set _SIM_MAJOR=%%i
for /F "usebackq tokens=3" %%i in (`findstr/C:"#define SIM_MINOR" ..\sim_rev.h`) do set _SIM_MINOR=%%i
for /F "usebackq tokens=3" %%i in (`findstr/C:"#define SIM_PATCH" ..\sim_rev.h`) do set _SIM_PATCH=-%%i
for /F "usebackq tokens=3" %%i in (`findstr/C:"#define SIM_VERSION_MODE" ..\sim_rev.h`) do set _SIM_VERSION_MODE=-%%~i
if "%_SIM_PATCH%" equ "-0" set _SIM_PATCH=

set _ZipName=simh-%_SIM_MAJOR%.%_SIM_MINOR%%_SIM_PATCH%%_SIM_VERSION_MODE%--%D_YYYY%-%D_MM%-%D_DD%-%GIT_COMMIT_ID:~0,8%.zip
set _ZipPath=..\..\%BIN_REPO%
if not exist "%_ZipPath%" mkdir "%_ZipPath%"
set _ZipPath=%_ZipPath%\

popd
pushd ..\%_ZipPath%
if "%1" neq "reset" goto GitAddNew
:GitSetup
if exist .git rmdir/q/s .git
if exist *.zip del *.zip
copy /y "%~p0Win32-Development-Binaries-README.md" .\README.md
git init
git add README.md
git commit -m "Initializing the Windows Binary repository"
git remote add origin "%REMOTE_REPO%"
git branch -m master %BIN_REPO%
git push --force -u origin %BIN_REPO%

:GitAddNew
if not exist .git git clone "%REMOTE_REPO%" ./
git pull 
set _BINARIES_ALREADY_BUILT=
for /F "usebackq" %%i in (`git log ^| findstr %GIT_COMMIT_ID%`) do set _BINARIES_ALREADY_BUILT=1
if "%_BINARIES_ALREADY_BUILT%" == "" goto DoBuild
:AlreadyBuilt
popd
echo Git Commit: %GIT_COMMIT_ID% - %GIT_COMMIT_TIME% has already been published
goto :EOF

:DoBuild
pushd %~p0\..
set _SIM_PARALLEL=8
if %_SIM_PARALLEL% GTR %NUMBER_OF_PROCESSORS% set _SIM_PARALLEL=%NUMBER_OF_PROCESSORS%
if not exist "%_ZipPath%%_ZipName%" vcbuild /M%_SIM_PARALLEL% /useenv /rebuild Simh.sln "Release|Win32"
7z a -tzip -x!BuildROMs.exe "%_ZipPath%%_ZipName%" "..\BIN\NT\Win32-Release\*.exe"
popd

git add %_ZipName%
git commit -m "Build results on %D_YYYY%-%D_MM%-%D_DD% for simh Commit: https://github.com/simh/simh/commit/%GIT_COMMIT_ID%"
git push -u origin %BIN_REPO%

popd
goto :EOF

:WhereInPath
if "%~$PATH:1" NEQ "" exit /B 0
exit /B 1

:WhereInInclude
if "%~$INCLUDE:1" NEQ "" exit /B 0
exit /B 1