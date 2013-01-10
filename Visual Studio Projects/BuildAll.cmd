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
rem  is under 7MB in size and the plan is to delete and recreate the whole
rem  Win32-Development-Binaries repository at least every few months.
rem
rem  If this script is invoked with a single parameter "reset", the local
rem  repository will be wiped out and reset.  This should be done AFTER
rem  the github one is deleted and recreated.
rem
rem

set BIN_REPO=Win32-Development-Binaries
if exist "%ProgramFiles%\Microsoft Visual Studio 9.0\VC\bin\vcvars32.bat" call "%ProgramFiles%\Microsoft Visual Studio 9.0\VC\bin\vcvars32.bat"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 9.0\VC\bin\vcvars32.bat" call "%ProgramFiles(x86)%\Microsoft Visual Studio 9.0\VC\bin\vcvars32.bat"
cd %~p0
SET GIT_COMMIT_ID=
if not exist ..\.git-commit-id goto _NoId
for /F %%i in (..\.git-commit-id) do set GIT_COMMIT_ID=%%i
for /F "tokens=3 delims=/" %%i in ("%DATE%") do set D_YYYY=%%i
for /F "tokens=2 delims=/ " %%i in ("%DATE%") do set D_MM=%%i
for /F "tokens=2 delims=/" %%i in ("%DATE%") do set D_DD=%%i
for /F "usebackq tokens=3" %%i in (`findstr/C:"#define SIM_MAJOR" ..\sim_rev.h`) do set _SIM_MAJOR=%%i
for /F "usebackq tokens=3" %%i in (`findstr/C:"#define SIM_MINOR" ..\sim_rev.h`) do set _SIM_MINOR=%%i
for /F "usebackq tokens=3" %%i in (`findstr/C:"#define SIM_PATCH" ..\sim_rev.h`) do set _SIM_PATCH=-%%i
for /F "usebackq tokens=3" %%i in (`findstr/C:"#define SIM_VERSION_MODE" ..\sim_rev.h`) do set _SIM_VERSION_MODE=-%%~i
if "%_SIM_PATCH%" equ "-0" set _SIM_PATCH=
set _ZipName=simh-%_SIM_MAJOR%.%_SIM_MINOR%%_SIM_PATCH%%_SIM_VERSION_MODE%--%D_YYYY%-%D_MM%-%D_DD%-%GIT_COMMIT_ID:~0,8%.zip
set _ZipPath=..\..\%BIN_REPO%\
vcbuild Simh.sln "Release|Win32"
if exist "%ProgramFiles%\7-Zip\7z.exe" "%ProgramFiles%\7-Zip\7z.exe" a -tzip "%_ZipPath%%_ZipName%" "..\BIN\NT\Win32-Release\*.exe"
if exist "%ProgramFiles(x86)%\7-Zip\7z.exe" "%ProgramFiles(x86)%\7-Zip\7z.exe" a -tzip "%_ZipPath%%_ZipName%" "..\BIN\NT\Win32-Release\*.exe"
if exist "%PROGRAMW6432%\7-Zip\7z.exe" "%PROGRAMW6432%\7-Zip\7z.exe" a -tzip "%_ZipPath%%_ZipName%" "..\BIN\NT\Win32-Release\*.exe"

pushd %_ZipPath%
where git.exe >NUL
if %ERRORLEVEL% equ 0 goto GitOK
if exist "%ProgramFiles%\Git\bin\git.exe" path %USERPROFILE%\bin;%ProgramFiles%\Git\local\bin;%ProgramFiles%\Git\mingw\bin;%ProgramFiles%\Git\bin\;%Path%
if exist "%ProgramFiles(x86)%\Git\bin\git.exe" path %USERPROFILE%\bin;%ProgramFiles(x86)%\Git\local\bin;%ProgramFiles(x86)%\Git\mingw\bin;%ProgramFiles(x86)%\Git\bin\;%Path%
:GitOK
if "%1" neq "reset" goto GitAddNew
:GitSetup
if exist .git rmdir/s .git
git init
git add README.md
git commit -m "Initializing the Windows Binary repository"
git remote add origin git@github.com:simh/%BIN_REPO%.git
git branch -m master %BIN_REPO%
git push -u origin %BIN_REPO%

:GitAddNew
git add %_ZipName%
git commit -m "Build results on %D_YYYY%-%D_MM%-%D_DD% for Commit Id %GIT_COMMIT_ID%"
git push -u origin %BIN_REPO%

popd