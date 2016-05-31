@echo off
rem Built in Ethernet support (requires WinPcap installed).
rem The normal Windows build always builds with Ethernet support
rem so, this procedure is un-necessary.  Just call the normal build
rem
%~p0\build_mingw.bat %*
