@echo off
setlocal
cd /d "%~dp0"
rem Compile custom source. See docs/development-guide.md for the Japanese walkthrough.
call "%~dp0build.cmd" --build-only
if errorlevel 1 (
  echo.
  echo Build failed. Read the first error above, then check docs/development-guide.md.
  pause
  exit /b 1
)
rem Use a separate development save and open the F1 panel immediately.
start "" "%~dp0build-release\StarRng.exe" --debug "--save-dir=%~dp0local-save"
