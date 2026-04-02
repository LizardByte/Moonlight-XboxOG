@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "PROJECT_ROOT=%%~fI"
set "CALLER_DIR=%CD%"

call :has_explicit_target %*
if errorlevel 1 if not defined MOONLIGHT_XEMU_BUILD_DIR if not defined MOONLIGHT_XEMU_ISO_PATH if not defined MOONLIGHT_XEMU_TARGET_PATH (
    if /I not "%CALLER_DIR%"=="%PROJECT_ROOT%" (
        set "MOONLIGHT_XEMU_TARGET_PATH=%CALLER_DIR%"
    )
)

call "%SCRIPT_DIR%find-msys2.cmd"
if errorlevel 1 (
    exit /b 1
)

pushd "%PROJECT_ROOT%" >nul
call "%MOONLIGHT_MSYS2_BASH%" -lc "cd \"$1\" && shift && ./scripts/run-xemu.sh \"$@\"" bash "%CD%" %*
set "EXIT_CODE=%ERRORLEVEL%"
popd >nul

exit /b %EXIT_CODE%

:has_explicit_target
if "%~1"=="" exit /b 1
if /I "%~1"=="--check" (
    shift
    goto has_explicit_target
)
if /I "%~1"=="-h" exit /b 1
if /I "%~1"=="--help" exit /b 1
if /I "%~1"=="--build-dir" exit /b 0
if /I "%~1"=="--iso" exit /b 0
if /I "%~1"=="--" (
    shift
    if "%~1"=="" exit /b 1
    exit /b 0
)
set "ARG=%~1"
if "%ARG:~0,1%"=="-" (
    shift
    goto has_explicit_target
)
exit /b 0
