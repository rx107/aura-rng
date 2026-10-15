@echo off
setlocal
if exist "%USERPROFILE%\Downloads\Chocolat.vrm" (
  start "" "%~dp0StarRng.exe" "--vrm=%USERPROFILE%\Downloads\Chocolat.vrm"
  exit /b
)
start "" "%~dp0StarRng.exe"
