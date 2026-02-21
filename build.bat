@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
cd /d "%ROOT%"

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
  set "CFG=--config Release"
  echo [INFO] Ninja not found. Using Visual Studio generator.
) else (
  set "GEN=-G Ninja"
  set "CFG="
  echo [INFO] Using Ninja generator.
)

if not exist build mkdir build

cmake -S . -B build %GEN%
if errorlevel 1 exit /b 1

cmake --build build --parallel %CFG% --target vkraw vkvsg
if errorlevel 1 exit /b 1

echo [OK] Build complete: vkraw + vkvsg
