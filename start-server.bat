rem The Crew Reborn - Knutwurst
rem https://thecrewreborn.github.io - https://ko-fi.com/knutwurst
@echo off
setlocal
cd /d "%~dp0"
set "PYVER=3.12.7"

call :find_python
if defined PYEXE goto :venv

echo Installing Python %PYVER%...
winget install -e --id Python.Python.3.12 --scope user --silent --accept-package-agreements --accept-source-agreements >nul 2>nul
call :find_python
if defined PYEXE goto :venv

echo Downloading Python %PYVER% from python.org...
powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing 'https://www.python.org/ftp/python/%PYVER%/python-%PYVER%-amd64.exe' -OutFile \"$env:TEMP\python-installer.exe\""
"%TEMP%\python-installer.exe" /quiet InstallAllUsers=0 PrependPath=1 Include_launcher=1
call :find_python
if defined PYEXE goto :venv
echo Python could not be installed. Install Python 3.10 or newer from https://www.python.org and start this file again.
pause
exit /b 1

:venv
if not exist ".venv\Scripts\python.exe" (
    echo Setting up the server environment...
    "%PYEXE%" -m venv .venv
)
".venv\Scripts\python.exe" -m pip install -q --disable-pip-version-check -r requirements-server.txt
echo Starting The Crew Reborn server. Stop it with Ctrl+C.
".venv\Scripts\python.exe" -m tcreborn %*
pause
exit /b

:find_python
set "PYEXE="
for %%P in ("%LOCALAPPDATA%\Programs\Python\Python313\python.exe" "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" "%ProgramFiles%\Python313\python.exe" "%ProgramFiles%\Python312\python.exe") do (
    if not defined PYEXE if exist "%%~P" set "PYEXE=%%~P"
)
if defined PYEXE exit /b
for /f "delims=" %%P in ('py -3 -c "import sys; print(sys.executable if sys.version_info >= (3, 10) else '')" 2^>nul') do set "PYEXE=%%P"
exit /b
