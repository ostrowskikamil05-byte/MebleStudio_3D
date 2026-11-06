@echo off
setlocal
cd /d "%~dp0"
py -3.13 -c "import sys,tkinter; assert sys.version_info[:2] == (3,13)" >nul 2>nul
if not errorlevel 1 goto py313
if exist ".venv\Scripts\python.exe" goto venv
set "MEBEL_TEST_PY=python"
goto run
:py313
set "MEBEL_TEST_PY=py -3.13"
goto run
:venv
set "MEBEL_TEST_PY=.venv\Scripts\python.exe"
:run
set "MEBEL_TEST_NATIVE="
if exist "..\MebelModel.dll" set MEBEL_TEST_NATIVE=--native-library "%~dp0..\MebelModel.dll"
%MEBEL_TEST_PY% sprawdz_regresje.py %MEBEL_TEST_NATIVE% %*
set "MEBEL_TEST_EXIT=%errorlevel%"
if not "%MEBEL_TEST_EXIT%"=="0" echo Testy nie przeszly. Sprawdz raport python_regression.json i log python_regression.log. Biblioteki paczki wymagaja systemowego Python 3.13.
exit /b %MEBEL_TEST_EXIT%
