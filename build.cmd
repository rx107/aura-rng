@echo off
setlocal
cd /d "%~dp0"
set "AURA_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%AURA_VSWHERE%" (
  echo Install Visual Studio with Desktop development with C++ and CMake tools.
  exit /b 1
)
for /f "usebackq delims=" %%i in (`"%AURA_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "AURA_VS=%%i"
if not defined AURA_VS (
  echo The Visual C++ x64 tools are missing.
  exit /b 1
)
set "AURA_CMAKE=%AURA_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%AURA_CMAKE%" set "AURA_CMAKE=cmake"
set "AURA_CTEST=%AURA_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
if not exist "%AURA_CTEST%" set "AURA_CTEST=ctest"
call "%AURA_VS%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
"%AURA_CMAKE%" -S . -B build-release -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
"%AURA_CMAKE%" --build build-release
if errorlevel 1 exit /b 1
rem Copy CSV/audio even when only data changed and the EXE did not need relinking.
"%AURA_CMAKE%" -E copy_directory data build-release/data
if errorlevel 1 exit /b 1
rem Develop.cmd uses this mode to try custom tuning without baseline-value regression checks.
if /I "%~1"=="--build-only" goto build_ready
"%AURA_CMAKE%" -E chdir build-release "%AURA_CTEST%" --output-on-failure
if errorlevel 1 exit /b 1
:build_ready
echo.
echo Ready: build-release\StarRng.exe
