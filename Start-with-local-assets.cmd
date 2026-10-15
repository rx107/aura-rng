@echo off
setlocal
if exist "%USERPROFILE%\Downloads\Chocolat.vrm" if exist "%USERPROFILE%\Downloads\Tides_of_the_Outer_Reach.mp3" (
  start "" "%~dp0StarRng.exe" "--vrm=%USERPROFILE%\Downloads\Chocolat.vrm" "--bgm=%USERPROFILE%\Downloads\Tides_of_the_Outer_Reach.mp3"
  exit /b
)
start "" "%~dp0StarRng.exe"
