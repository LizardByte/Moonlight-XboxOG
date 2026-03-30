@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "MSYS2_SHELL=C:\msys64\msys2_shell.cmd"

if not exist "%MSYS2_SHELL%" (
    echo MSYS2 shell not found at %MSYS2_SHELL%
    exit /b 1
)

pushd "%SCRIPT_DIR%" >nul
call "%MSYS2_SHELL%" -defterm -here -no-start -mingw64 -c "./build.sh %*"
set "EXIT_CODE=%ERRORLEVEL%"
popd >nul

exit /b %EXIT_CODE%
