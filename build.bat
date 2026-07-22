@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "ROOT=%~dp0"

if "%~1"=="" (
  call "%~f0" run
  set "RESULT=!errorlevel!"
  if not "!RESULT!"=="0" (
    echo.
    echo [ERROR] Setup or build failed with exit code !RESULT!. Press any key to close.
    pause >nul
  )
  exit /b !RESULT!
)

set "ACTION=%~1"
if /i "%ACTION%"=="clean" (
  if exist "%ROOT%out" rmdir /s /q "%ROOT%out"
  echo [OK] Removed out\. Dependency and tool caches were kept.
  exit /b 0
)

call :bootstrap_tools
if errorlevel 1 exit /b !errorlevel!
if /i "%ACTION%"=="tools" exit /b 0

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] Visual Studio Installer was not found.
  echo Install Visual Studio 2022 with "Desktop development with C++".
  exit /b 1
)

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo [ERROR] Visual Studio 2022 with the MSVC x64 workload was not found.
  echo Install the "Desktop development with C++" workload and retry.
  exit /b 1
)

set "VSLANG=1033"
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 (
  echo [ERROR] Could not initialize the Visual Studio x64 environment.
  exit /b 1
)

set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%CMAKE%" for /f "delims=" %%I in ('where cmake 2^>nul') do if not defined CMAKE_FALLBACK set "CMAKE_FALLBACK=%%I"
if not exist "%CMAKE%" set "CMAKE=!CMAKE_FALLBACK!"
if not exist "%NINJA%" for /f "delims=" %%I in ('where ninja 2^>nul') do if not defined NINJA_FALLBACK set "NINJA_FALLBACK=%%I"
if not exist "%NINJA%" set "NINJA=!NINJA_FALLBACK!"
if not exist "%CMAKE%" (
  echo [ERROR] CMake was not found. Add the CMake component in Visual Studio Installer.
  exit /b 1
)
if not exist "%NINJA%" (
  echo [ERROR] Ninja was not found. Add the CMake tools component in Visual Studio Installer.
  exit /b 1
)

for /f "delims=" %%I in ('where git 2^>nul') do if not defined GIT_EXE set "GIT_EXE=%%I"
if not defined GIT_EXE (
  echo [ERROR] Git was not found. It is required once to fetch C++ dependencies.
  echo Install Git for Windows, then rerun build.bat.
  exit /b 1
)
set "GIT_CONFIG_COUNT=1"
set "GIT_CONFIG_KEY_0=http.sslBackend"
set "GIT_CONFIG_VALUE_0=openssl"

set "CONFIG="
if /i "%ACTION%"=="debug" set "CONFIG=Debug"
if /i "%ACTION%"=="release" set "CONFIG=Release"
if /i "%ACTION%"=="relwithdebinfo" set "CONFIG=RelWithDebInfo"
if /i "%ACTION%"=="test" set "CONFIG=RelWithDebInfo"
if /i "%ACTION%"=="run" set "CONFIG=RelWithDebInfo"
if not defined CONFIG (
  echo [ERROR] Unknown action: %ACTION%
  echo Usage: build.bat [run^|test^|tools^|Debug^|Release^|RelWithDebInfo^|clean]
  exit /b 1
)
set "PRESET=windows-relwithdebinfo"
if /i "%CONFIG%"=="Debug" set "PRESET=windows-debug"
if /i "%CONFIG%"=="Release" set "PRESET=windows-release"

echo [INFO] VS:     %VSROOT%
echo [INFO] CMake: %CMAKE%
echo [INFO] Ninja: %NINJA%
echo [INFO] Git:    %GIT_EXE%
echo [INFO] Preset: %PRESET%

set "BUILD_DRIVE="
for %%D in (R: Q: P: O: N: M: L: K: J:) do if not defined BUILD_DRIVE if not exist "%%D\" set "BUILD_DRIVE=%%D"
if not defined BUILD_DRIVE (
  echo [ERROR] No free temporary drive letter was found in R: through J:.
  exit /b 1
)
subst %BUILD_DRIVE% "%ROOT:~0,-1%"
if errorlevel 1 (
  echo [ERROR] Could not map %BUILD_DRIVE% to the repository.
  exit /b 1
)
pushd %BUILD_DRIVE%\cmake

"%CMAKE%" --preset %PRESET% -DCMAKE_MAKE_PROGRAM="%NINJA%"
set "RESULT=!errorlevel!"
if not "!RESULT!"=="0" (
  echo [ERROR] CMake configure failed with exit code !RESULT!.
  goto :mapped_done
)

"%CMAKE%" --build --preset %PRESET%
set "RESULT=!errorlevel!"
if not "!RESULT!"=="0" (
  echo [ERROR] Build failed with exit code !RESULT!.
  goto :mapped_done
)

if /i "%ACTION%"=="test" (
  "%CMAKE%" --build "%BUILD_DRIVE%\out\build\%PRESET%" --target test
  set "RESULT=!errorlevel!"
)

:mapped_done
popd
subst %BUILD_DRIVE% /d
if not "%RESULT%"=="0" exit /b %RESULT%

if /i "%ACTION%"=="run" (
  start "" "%ROOT%out\bin\%CONFIG%\GBFRModtools.exe"
  echo [OK] Editor launched.
  exit /b 0
)

echo [OK] %ROOT%out\bin\%CONFIG%\GBFRModtools.exe
exit /b 0

:bootstrap_tools
set "BOOTSTRAP=%ROOT%scripts\bootstrap_tools.ps1"
if not exist "%BOOTSTRAP%" (
  echo [ERROR] Missing runtime tool bootstrap: %BOOTSTRAP%
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%BOOTSTRAP%"
if errorlevel 1 (
  echo [ERROR] Runtime tool setup failed. Check the network connection and retry.
  exit /b 1
)
exit /b 0
