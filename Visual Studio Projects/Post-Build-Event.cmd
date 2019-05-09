rem
rem  This script performs activities after a simulator build to run 
rem  simulator specific test activities.  Tests are only performed
rem  if a simulation test script is available.
rem
rem  There are 2 required parameters to this procedure:
rem    1 - The simulator source directory
rem    2   The compiled simulator binary path
rem  There are 2 optional parameters to this procedure:
rem    3   A specific test script name
rem    4   Optional parameters to invoke the specified script with
rem
rem

if exist %2 goto _check_script
echo error: Missing simulator binary: %2
exit /B 1

:_check_script
set _binary_name=%~n2
set _script_path=..\%1\tests\%3.ini
if exist "%_script_path%" goto _got_script
set _script_path=..\%1\tests\%_binary_name%_test.ini
if exist "%_script_path%" goto _got_script
echo No tests found for %_binary_name% simulator.
exit /B 0

:_got_script
%2 "%_script_path%" "%4"
