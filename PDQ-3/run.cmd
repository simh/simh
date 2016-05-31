@echo off
if not exist "..\BIN\NT\Win32-Debug\pdq3.exe" goto try_mingw
..\BIN\NT\Win32-Debug\pdq3.exe testhdt.sim
goto done

:try_mingw
.\PDQ3.exe testhdt.sim
goto done


:done
