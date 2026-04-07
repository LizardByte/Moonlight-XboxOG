On Windows we use msys2 and mingw64 to compile.
You need to prefix commands with `C:\msys64\msys2_shell.cmd -defterm -here -no-start -mingw64 -c`.

Prefix build directories with `cmake-build-`.

The xbox build uses Unix Makefiles.

The host native tests will use MinGW Makefiles on Windows, but Unix Makefiles on other platforms.

The project uses gtest as a test framework.

Always add or update doxygen documentation.

The project requires that everything be documented in doxygen or the build will fail.

Inline doxygen comments should use `///< ...` instead of `/**< ... */`.

Always follow the style guidelines defined in .clang-format for c/c++ code.
