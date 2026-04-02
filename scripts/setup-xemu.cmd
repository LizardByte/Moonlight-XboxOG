@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
call "%SCRIPT_DIR%find-msys2.cmd"
if errorlevel 1 (
    exit /b 1
)

pushd "%SCRIPT_DIR%.." >nul
call "%MOONLIGHT_MSYS2_SHELL%" -defterm -here -no-start -mingw64 -c "./scripts/setup-xemu.sh %*"
set "EXIT_CODE=%ERRORLEVEL%"
popd >nul

exit /b %EXIT_CODE%
