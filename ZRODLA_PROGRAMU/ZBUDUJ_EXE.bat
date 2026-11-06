@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ZBUDUJ_EXE.ps1" %*
set "MEBEL_BUILD_EXIT=%errorlevel%"
if not "%MEBEL_BUILD_EXIT%"=="0" echo Budowanie nie powiodlo sie. Sprawdz komunikaty powyzej.
exit /b %MEBEL_BUILD_EXIT%
