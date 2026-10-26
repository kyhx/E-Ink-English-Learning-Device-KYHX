@echo off
cd /d "%~dp0..\.."
if not exist ".venv-train\Scripts\python.exe" (
  echo [ERROR] .venv-train not found. Run:
  echo   pwsh -File tools\setup\install-train.ps1
  pause
  exit /b 1
)
".venv-train\Scripts\python.exe" "tools\pipeline\gui.py"
if errorlevel 1 pause
