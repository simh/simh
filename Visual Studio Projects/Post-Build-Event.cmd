rem
rem  This script performs activities after a simulator build to run 
rem  simulator specific test activities.  
rem  The test activities performed include:
rem   1) a sanity check on the simh REGister definitions for each
rem      device in the simulator.  If the register sanity check fails, 
rem      the build fails and the simulator binary is deleted - since 
rem      this failure is akin to a compile time error the simulator 
rem      should be fixed before it can be used.
rem   2) An optional simulator test which is only performed
rem      if a simulation test script is available.
rem
rem  There are 2 required parameters to this procedure:
rem    1 - The simulator source directory
rem    2   The compiled simulator binary path
rem  There are 2 optional parameters to this procedure:
rem    3   A specific test script name
rem    4   Optional parameters to invoke the specified script with
rem
rem  These tests will be skipped if there is a file named Post-Build-Event.Skip
rem  in the same directory as this procedure.
rem

if exist %2 goto _check_skip_tests
echo error: Missing simulator binary: %2
exit /B 1

:_check_skip_tests
set _binary_name=%~n2
set _script_name=%~dpn0
if exist "%_script_name%.Skip" echo '%_script_name%.Skip' exists - Testing Disabled & exit /B 0

:_do_reg_sanity
rem Run internal register sanity checks
%2 RegisterSanityCheck
echo.
rem if the register sanity checks fail then this is a failed build 
rem so delete the simulator binary.
if ERRORLEVEL 1 del %2 & exit /B 1

:_check_script
set _script_path=..\%1\tests\%3.ini
if exist "%_script_path%" goto _got_script
set _script_path=..\%1\tests\%_binary_name%_test.ini
if exist "%_script_path%" goto _got_script
echo  Simulator specific tests not found for %_binary_name% simulator.
exit /B 0

:_got_script
%2 "%_script_path%" "%4"
