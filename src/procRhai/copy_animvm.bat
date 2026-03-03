@echo off
setlocal EnableExtensions

set "ROOT_DIR=%~dp0..\.."
set "ANIMVM_DIR=%ROOT_DIR%\src\procRhai\animvm"
set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%ROOT_DIR%\build-Release"

if not exist "%BUILD_DIR%" (
    echo [ERROR] Build directory not found: %BUILD_DIR%
    echo Usage: copy_animvm.bat [build_dir]
    exit /b 1
)

where cargo >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cargo not found in PATH.
    exit /b 1
)

echo [INFO] Building animvm (Rust cdylib)...
cargo build --release --manifest-path "%ANIMVM_DIR%\Cargo.toml"
if errorlevel 1 exit /b 1

set "SRC_LIB=%ANIMVM_DIR%\target\release\animvm.dll"
if not exist "%SRC_LIB%" (
    echo [ERROR] Built library not found: %SRC_LIB%
    exit /b 1
)

copy /Y "%SRC_LIB%" "%BUILD_DIR%\animvm.dll" >nul
if errorlevel 1 exit /b 1

echo [OK] Copied animvm.dll to %BUILD_DIR%
exit /b 0
