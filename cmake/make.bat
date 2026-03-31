@echo off
setlocal EnableExtensions

set "MSYS2_SHELL=C:\msys64\msys2_shell.cmd"

if not exist "%MSYS2_SHELL%" (
    echo MSYS2 shell not found at %MSYS2_SHELL%>&2
    exit /b 1
)

set "MAKE_ARGS="
:collect_args
if "%~1"=="" goto run_make
set "ARG=%~1"
set "ARG=%ARG:'='\''%"
set "MAKE_ARGS=%MAKE_ARGS% '%ARG%'"
shift
goto collect_args

:run_make
call "%MSYS2_SHELL%" -defterm -here -no-start -mingw64 -c "make%MAKE_ARGS%"
exit /b %ERRORLEVEL%
