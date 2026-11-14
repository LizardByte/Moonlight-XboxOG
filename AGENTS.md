# Agent instructions

## Global instructions

Before working on a task, read `~/.agents/skills/AGENTS.md`, expanding `~` to the current user's home directory.
If that file is unavailable, report that the shared global instructions could not be loaded.

## Repository boundaries

This repository is the original Xbox Moonlight client, using nxdk, SDL and FFmpeg. The adjacent Sunshine
checkout is a separate project; use it as a reference when relevant, and keep changes in this checkout
unless the task explicitly includes another repository.

Preserve unrelated working-tree changes. Treat `third-party/` as vendored dependencies. Apply Xbox
compatibility adaptations through the project's adapters and build-copy mechanisms rather than editing
generated SDK copies or dependency outputs. Keep generated files and local emulator support data under
their existing build or ignored directories.

## Build

On Windows, compile with MSYS2 MinGW64. Prefix Windows build and test commands with
`C:\msys64\msys2_shell.cmd -defterm -here -no-start -mingw64 -c`.

Prefix local build directories with `cmake-build-`. Host builds and native tests use MinGW Makefiles on
Windows and Unix Makefiles on other platforms. Xbox builds use Unix Makefiles on every platform.

The normal top-level configure uses the host toolchain and delegates Xbox compilation to an nxdk child
build. Use `CMakePresets.json` and the README's build instructions; keep the Xbox toolchain separate from
host-native tests. The normal release ISO is `cmake-build-release/xbox/Moonlight.iso`, with the packaged
Xbox executable at `cmake-build-release/xbox/xbe/default.xbe`.

Video experiment builds are opt-in through `MOONLIGHT_BUILD_VIDEO_EXPERIMENTS`. Keep their instructions
with the probes in `experiments/README.md`.

## Tests and validation

The project uses GoogleTest. Tests must include `<lizardbyte/common/testing.h>` instead of including
GoogleTest directly. This supplies the shared test support and output capture; Sunshine's
`tests/tests_common.h` is not part of this repository.

Group test includes under `// test includes`, then `// standard includes`, `// lib includes` and
`// local includes`, with blank lines between groups and no empty groups. Keep platform-specific headers
inside their preprocessor guards. Preserve and explain intentional include-order dependencies.

Add or update meaningful regression tests for new or changed source behavior. Target full coverage of
changed testable source code. Keep platform-neutral logic accessible to native tests and document any
validation limits that require an emulator or console.

Run tests with `ctest --test-dir <build-directory> --output-on-failure` so every registered test executable
is included. Native test executables, including `test_moonlight` and `test_pvideo_presenter`, are under the
build directory's `tests/` folder. A host-only configuration can disable Xbox builds with `BUILD_XBOX=OFF`.

Build-system changes require appropriate configuration, build and lint checks rather than dedicated
build-system regression projects or fixtures. Documentation-only edits require a strict documentation
build and relevant formatting/link checks, rather than new source tests.

For presentation changes, check GPU and software paths. Distinguish native test results, emulator
behavior and real-console validation; emulator output does not establish console performance or GPU
image correctness. Preserve software fallback when GPU presentation cannot be used.

## Python tooling and formatting

From the repository root, use `uv sync --locked` to refresh `.venv` from `pyproject.toml` and `uv.lock`.
Re-sync after switching branches or changing those files or `third-party/lizardbyte-common`. Update the
lockfile with `uv lock` when changing dependency declarations or the shared dependency revision.

Follow `.clang-format` for C and C++. After native source changes, run
`uv run --locked lb-update-clang-format` from the repository root and inspect the diff for unrelated
formatting changes. Format changed C/C++ files in `experiments/` separately: the shared formatter does
not traverse that directory. Centrally managed style files should be updated through their owning
configuration repository rather than changed locally.

## Code documentation

Always add or update Doxygen documentation for source changes. Document new or changed functions,
classes, structures, members, test fixtures and tests. The strict documentation build fails on missing
required documentation.

Use primary Doxygen comments in this form:

```cpp
/**
 * @brief Describe the function, structure, etc.
 *
 * @param my_param Describe the parameter.
 * @return Describe the return.
 */
```

Use `///< ...` for inline Doxygen comments, rather than `/**< ... */`.

Keep implementation constraints and reasons for compatibility workarounds beside the code that depends
on them. Keep user guidance in the README and metric definitions in the corresponding API documentation.
Published documentation should describe supported behavior and operating instructions, rather than
debugging chronologies, session-specific frame counts or research transcripts.

Documentation is built through Dockle and Doxygen using `dockle.toml`. Validate documentation edits with
the strict `api` build; a CMake build configured with `BUILD_DOCS=ON` provides the `docs` target.

## Markdown style

Align the pipes in Markdown tables in the source. Pad each column to a consistent width across the
header, separator and body rows, preserving any column-alignment markers. Apply this to all first-party
Markdown files, including READMEs and this file.

Use relative repository links for local documentation. Do not add decorative separator comments or
dashed banners to group tests; use descriptive names and code structure.

## Issues, pull requests and reviews

Use applicable repository or inherited organization templates when creating issues or pull requests,
following the shared global instructions. For an issue-only request, investigate enough to verify and
describe the observed behavior, expected behavior and reproduction steps. Investigate root causes and
implement fixes fully when those are requested.

Review the current pull request head and verify mergeability against its target branch. Report concrete
introduced problems with their trigger, impact, location and a practical fix. Keep workflow status and
validation limitations separate from code findings. Pending maintainer approval or checks that have not
run are verification limits, not defects for the contributor to fix. Review actual failed checks for
change-related problems. An approval does not need an additional comment solely to announce it.
