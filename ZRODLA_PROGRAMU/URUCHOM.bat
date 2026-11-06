@echo off
setlocal
cd /d "%~dp0"
if exist "MebelStudio3D.exe" (
    start "" "MebelStudio3D.exe" %*
    exit /b
)
if exist "..\MebelStudio3D.exe" if exist "..\_internal\python313.dll" if exist "..\_internal\base_library.zip" (
    start "" "..\MebelStudio3D.exe" --host-app "%~dp0." %*
    exit /b
)
if exist ".venv\Scripts\python.exe" (
    ".venv\Scripts\python.exe" app.py %*
    goto result
)
py -3 -c "import sys, tkinter; assert sys.version_info >= (3,10)" >nul 2>nul
if not errorlevel 1 goto launcher
python -c "import sys, tkinter; assert sys.version_info >= (3,10)" >nul 2>nul
if not errorlevel 1 goto python
echo Mebel Studio 3D wymaga Python 3.10 lub nowszego z tkinter.
echo Pobierz Python dla Windows: https://www.python.org/downloads/windows/
echo Przy instalacji zaznacz Tcl/Tk oraz Add Python to PATH.
echo Potem uruchom ponownie ten plik.
pause
exit /b 1
:launcher
py -3 app.py %*
goto result
:python
python app.py %*
:result
if errorlevel 1 pause
endlocal
