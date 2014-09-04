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
rem  is under 10MB in size and the plan is to delete and recreate the whole
rem  Win32-Development-Binaries repository at least every few months.
rem
rem  If this script is invoked with a single parameter "reset", the local
rem  repository will be wiped out and reset.  This should be done AFTER
rem  the github one is deleted and recreated.
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
echo **** ERROR ****  git is unavailable
exit /B 1
:GitOK

call :WhereInPath 7z.exe > NUL 2>&1
if %ERRORLEVEL% equ 0 goto ZipOK
if exist "%ProgramFiles%\7-Zip\7z.exe"      set PATH=%PATH%;%ProgramFiles%\7-Zip\&goto ZipOK
if exist "%ProgramFiles(x86)%\7-Zip\7z.exe" set PATH=%PATH%;%ProgramFiles(x86)%\7-Zip\&goto ZipOK
if exist "%PROGRAMW6432%\7-Zip\7z.exe"      set PATH=%PATH%;%PROGRAMW6432%\7-Zip\&goto ZipOK
echo **** ERROR **** 7-Zip is unavailable
exit /B 1
:ZipOK

rem  Now locate the Visual Studio (VC++) 2008 components and setup the environment variables to use them
if not exist "%VS90COMNTOOLS%\..\..\VC\bin\vcvars32.bat" echo **** ERROR **** Visual Studio 2008 is unavailable
if not exist "%VS90COMNTOOLS%\..\..\VC\bin\vcvars32.bat" exit /B 1
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
echo **** ERROR **** Can't locate the Windows SDK include and library files
exit /B 1
:VSOK

rem  This procedure must be located in the "Visual Studio Projects" directory.  
rem  The git repository directory (.git) is located relative to that directory.
cd %~p0
SET GIT_COMMIT_ID=
if not exist ..\.git\hooks\post-commit copy git-hooks\post* ..\.git\hooks\ > NUL 2>&1
pushd ..
git checkout -q master
git fetch -q origin master
git log -1 "--pretty=%%H" >.git-commit-id
popd
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
vcbuild /M2 /useenv /rebuild Simh.sln "Release|Win32"
7z a -tzip "%_ZipPath%%_ZipName%" "..\BIN\NT\Win32-Release\*.exe"
pushd %_ZipPath%
if "%1" neq "reset" goto GitAddNew
:GitSetup
if exist .git rmdir/s .git
git init
git add README.md
git commit -m "Initializing the Windows Binary repository"
git remote add origin "%REMOTE_REPO%"
git branch -m master %BIN_REPO%
git push -u origin %BIN_REPO%

:GitAddNew
if not exist .git git clone "%REMOTE_REPO%" ./
git pull
git add %_ZipName%
git commit -m "Build results on %D_YYYY%-%D_MM%-%D_DD% for Commit Id %GIT_COMMIT_ID%"
git push -u origin %BIN_REPO%

popd
goto :EOF

:WhereInPath
if "%~$PATH:1" NEQ "" exit /B 0
exit /B 1

:WhereInInclude
if "%~$INCLUDE:1" NEQ "" exit /B 0
exit /B 1