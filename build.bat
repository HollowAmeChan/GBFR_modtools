@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] vswhere.exe was not found: !VSWHERE!
  exit /b 1
)

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo [ERROR] Visual Studio with the MSVC x64 workload was not found.
  exit /b 1
)

call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1

set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%CMAKE%" set "CMAKE=cmake"
if not exist "%NINJA%" set "NINJA=ninja"

set "GIT_CONFIG_COUNT=1"
set "GIT_CONFIG_KEY_0=http.sslBackend"
set "GIT_CONFIG_VALUE_0=openssl"

set "ACTION=%~1"
set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=RelWithDebInfo"
if /i "%ACTION%"=="clean" (
  if exist "%ROOT%out" rmdir /s /q "%ROOT%out"
  echo [OK] Removed out\
  exit /b 0
)
if /i "%ACTION%"=="test" set "CONFIG=RelWithDebInfo"
if /i "%ACTION%"=="run" set "CONFIG=RelWithDebInfo"

if /i "%CONFIG%"=="debug" set "PRESET=windows-debug"
if /i "%CONFIG%"=="release" set "PRESET=windows-release"
if /i "%CONFIG%"=="relwithdebinfo" set "PRESET=windows-relwithdebinfo"
if not defined PRESET (
  echo [ERROR] Unknown configuration/action: %ACTION%
  echo Usage: build.bat [Debug^|Release^|RelWithDebInfo^|clean^|test^|run]
  exit /b 1
)

echo [INFO] VS:     %VSROOT%
echo [INFO] CMake: %CMAKE%
echo [INFO] Ninja: %NINJA%
echo [INFO] Preset: %PRESET%

set "BUILD_DRIVE=R:"
if exist "%BUILD_DRIVE%\" (
  echo [ERROR] Temporary build drive %BUILD_DRIVE% is already in use.
  exit /b 1
)
subst %BUILD_DRIVE% "%ROOT:~0,-1%"
if not "!errorlevel!"=="0" (
  echo [ERROR] Could not map %BUILD_DRIVE% to the repository.
  exit /b 1
)
pushd %BUILD_DRIVE%\

"%CMAKE%" --preset %PRESET% -DCMAKE_MAKE_PROGRAM="%NINJA%"
set "RESULT=!errorlevel!"
if not "!RESULT!"=="0" (
  echo [ERROR] CMake configure failed with exit code !RESULT!.
  popd
  subst %BUILD_DRIVE% /d
  exit /b !RESULT!
)
"%CMAKE%" --build --preset %PRESET%
set "RESULT=!errorlevel!"
if not "!RESULT!"=="0" (
  echo [ERROR] Build failed with exit code !RESULT!.
  popd
  subst %BUILD_DRIVE% /d
  exit /b !RESULT!
)

if /i "%ACTION%"=="test" (
  "%CMAKE%" --build "%BUILD_DRIVE%\out\build\%PRESET%" --target test
  set "RESULT=!errorlevel!"
  popd
  subst %BUILD_DRIVE% /d
  exit /b !RESULT!
)
if /i "%ACTION%"=="run" (
  popd
  subst %BUILD_DRIVE% /d
  start "" "%ROOT%out\bin\%CONFIG%\GBFRModtools.exe" "%ROOT%explore_output\manifest.md"
  exit /b 0
)

popd
subst %BUILD_DRIVE% /d
echo [OK] %ROOT%out\bin\%CONFIG%\GBFRModtools.exe
exit /b 0
