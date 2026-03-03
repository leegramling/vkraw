@echo off
setlocal

set ROOT_DIR=%~dp0..
set PROJECT_DIR=%ROOT_DIR%\docs\naturaldocs
set OUTPUT_DIR=%ROOT_DIR%\docs\site

set "ND_CMD=NaturalDocs"
where NaturalDocs >nul 2>nul
if errorlevel 1 (
    if exist "%ROOT_DIR%\NaturalDocs\NaturalDocs.exe" (
        set "ND_CMD=%ROOT_DIR%\NaturalDocs\NaturalDocs.exe"
    ) else (
        echo [ERROR] NaturalDocs not found in PATH and local .\NaturalDocs\NaturalDocs.exe is missing.
        exit /b 1
    )
)

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

%ND_CMD% ^
    -i "%ROOT_DIR%\src" ^
    -o HTML "%OUTPUT_DIR%" ^
    -p "%PROJECT_DIR%"

if errorlevel 1 exit /b 1

echo [OK] Documentation generated at %OUTPUT_DIR%
exit /b 0
