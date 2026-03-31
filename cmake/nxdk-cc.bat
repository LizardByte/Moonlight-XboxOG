@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "NXDK_DIR=%PROJECT_ROOT%\third-party\nxdk"
set "BASH_EXE=C:\msys64\usr\bin\bash.exe"
set "HELPER_SCRIPT=%SCRIPT_DIR%run-nxdk-tool.sh"

if not exist "%BASH_EXE%" (
    echo MSYS2 bash not found at %BASH_EXE%>&2
    exit /b 1
)

if not exist "%NXDK_DIR%\bin\nxdk-cc" (
    echo nxdk compiler wrapper not found at %NXDK_DIR%\bin\nxdk-cc>&2
    exit /b 1
)

"%BASH_EXE%" "%HELPER_SCRIPT%" "%NXDK_DIR%" nxdk-cc %*
exit /b %ERRORLEVEL%
