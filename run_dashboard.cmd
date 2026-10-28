@echo off
rem AcoustiScan dashboard - double-click to start. Closes when this window closes.
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" (
  echo Creating the Python environment ...
  py -3.14 -m venv .venv || (echo Python 3.14 not found. Install it from python.org. & pause & exit /b 1)
  ".venv\Scripts\python.exe" -m pip install -r server\requirements.txt || (pause & exit /b 1)
)
".venv\Scripts\python.exe" server\app.py --host 0.0.0.0 %*
pause
