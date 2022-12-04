@echo off
:: Build Release simulators with Visual Studio and save the binaries 
:: in a Zip file.
::
pushd "%~p0"
cd ..
:: First build all the simulators
call build_vstudio.bat
if errorlevel 1 goto Done
cd "Visual Studio Projects"
:: determine the zip file name
for /F "usebackq tokens=2" %%i in (`findstr /C:SIM_GIT_COMMIT_ID .git-commit-id`) do set GIT_COMMIT_ID=%%i
for /F "usebackq tokens=2" %%i in (`findstr /C:SIM_GIT_COMMIT_TIME .git-commit-id`) do set GIT_COMMIT_TIME=%%i
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
set _ZipPath=BIN\NT\%_ZipName%
echo Creating Zip File: %_ZipPath%
Powershell -NoLogo -Command Compress-Archive -Path "BIN\NT\Win32-Release\*.exe" -DestinationPath "%_ZipPath%"
echo ZIPPATH=%_ZipPath% >> %GITHUM_ENV%

:Done
popd