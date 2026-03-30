On Windows we use msys2 and mingw64 to compile.
You need to prefix commands with `C:\msys64\msys2_shell.cmd -defterm -here -no-start -mingw64 -c`.

Prefix build directories with `cmake-build-`.

The project uses gtest as a test framework.

Always follow the style guidelines defined in .clang-format for c/c++ code.
