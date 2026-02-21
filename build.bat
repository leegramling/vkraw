@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
cd /d "%ROOT%"

set "BUILD_TYPE=Release"
if not "%~1"=="" set "BUILD_TYPE=%~1"

if /I not "%BUILD_TYPE%"=="Debug" if /I not "%BUILD_TYPE%"=="Release" if /I not "%BUILD_TYPE%"=="RelWithDebInfo" (
  echo [ERROR] Invalid build type "%BUILD_TYPE%".
  echo         Usage: build.bat [Debug^|Release^|RelWithDebInfo]
  exit /b 1
)

if /I "%BUILD_TYPE%"=="Release" (
  if exist "%ROOT%..\vsg_deps\install\lib\vsgd.lib" if not exist "%ROOT%..\vsg_deps\install\lib\vsg.lib" (
    echo [WARN] Found debug-only VSG libraries in ..\vsg_deps\install\lib.
    echo [WARN] Switching build type to Debug to match dependencies.
    set "BUILD_TYPE=Debug"
  )
)

set "SDK_ROOT=C:\VulkanSDK"
set "VULKAN_BIN="

if exist "%SDK_ROOT%\Bin\glslc.exe" (
  set "VULKAN_SDK=%SDK_ROOT%"
  set "VULKAN_BIN=%SDK_ROOT%\Bin"
) else (
  for /f "delims=" %%D in ('dir /b /ad /o-n "%SDK_ROOT%" 2^>nul') do (
    if exist "%SDK_ROOT%\%%D\Bin\glslc.exe" (
      set "VULKAN_SDK=%SDK_ROOT%\%%D"
      set "VULKAN_BIN=%SDK_ROOT%\%%D\Bin"
      goto :sdk_found
    )
  )
)

:sdk_found
if not defined VULKAN_SDK (
  echo [ERROR] Could not find Vulkan SDK under %SDK_ROOT%.
  echo         Expected glslc at %SDK_ROOT%\Bin\glslc.exe
  echo         or in a version folder like %SDK_ROOT%\1.x.x.x\Bin\glslc.exe
  exit /b 1
)

set "PATH=%VULKAN_BIN%;%PATH%"

echo [INFO] Using VULKAN_SDK=%VULKAN_SDK%

where cmake >nul 2>&1
if errorlevel 1 (
  echo [ERROR] cmake not found in PATH.
  exit /b 1
)

where git >nul 2>&1
if errorlevel 1 (
  echo [ERROR] git not found in PATH.
  exit /b 1
)

echo [INFO] Syncing submodules...
git submodule update --init --recursive
if errorlevel 1 (
  echo [ERROR] Failed to initialize submodules.
  exit /b 1
)

where ninja >nul 2>&1
if errorlevel 1 (
  set "GEN=-G Visual Studio 17 2022 -A x64"
  set "CFG=--config %BUILD_TYPE%"
  set "TYPE_ARG="
  echo [INFO] Ninja not found. Using Visual Studio generator.
) else (
  set "GEN=-G Ninja"
  set "CFG="
  set "TYPE_ARG=-DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
  echo [INFO] Using Ninja generator.
)

echo [INFO] Build type: %BUILD_TYPE%
set "BUILD_DIR=build-%BUILD_TYPE%"
echo [INFO] Build dir: %BUILD_DIR%

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -S . -B "%BUILD_DIR%" %GEN% %TYPE_ARG%
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --parallel %CFG% --target vkraw vkvsg
if errorlevel 1 exit /b 1

echo [OK] Build complete: vkraw + vkvsg
