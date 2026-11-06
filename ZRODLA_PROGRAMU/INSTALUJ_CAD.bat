@echo off
setlocal
cd /d "%~dp0"
echo Instalacja lokalnego modulu CAD: zaokraglenia bryl i eksport STEP.
echo Pobieranie bibliotek z pip wymaga internetu. Pozniejsza praca jest offline.
py -3 -c "import sys,tkinter; assert sys.version_info >= (3,10)" >nul 2>nul
if not errorlevel 1 (
    set "MEBEL_PY=py -3"
) else (
    set "MEBEL_PY=python"
)
if not exist ".venv\Scripts\python.exe" %MEBEL_PY% -m venv .venv
if errorlevel 1 goto fail
".venv\Scripts\python.exe" -m pip install -r requirements-cad.txt
if errorlevel 1 goto fail
".venv\Scripts\python.exe" -c "import cadquery,tkinter; print('Modul CAD gotowy')"
if errorlevel 1 goto fail
echo Uruchom teraz URUCHOM.bat.
pause
exit /b 0
:fail
echo Instalacja nie powiodla sie. Podstawowy edytor nadal dziala bez modulu CAD.
pause
exit /b 1
