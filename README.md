# Moonlight-XboxOG

[![GitHub stars](https://img.shields.io/github/stars/lizardbyte/moonlight-xboxog.svg?logo=github&style=for-the-badge)](https://github.com/LizardByte/Moonlight-XboxOG)
[![GitHub Releases](https://img.shields.io/github/downloads/lizardbyte/moonlight-xboxog/total.svg?style=for-the-badge&logo=github)](https://github.com/LizardByte/Moonlight-XboxOG/releases/latest)
[![GitHub Workflow Status (CI)](https://img.shields.io/github/actions/workflow/status/lizardbyte/moonlight-xboxog/ci.yml.svg?branch=master&label=CI%20build&logo=github&style=for-the-badge)](https://github.com/LizardByte/Moonlight-XboxOG/actions/workflows/CI.yml?query=branch%3Amaster)
[![Codecov](https://img.shields.io/codecov/c/gh/LizardByte/Moonlight-XboxOG?token=DoIh5pkEzA&style=for-the-badge&logo=codecov&label=codecov)](https://codecov.io/gh/LizardByte/Moonlight-XboxOG)

Port of Moonlight for the Original Xbox. Unlikely to ever actually work. Do NOT use!

Nothing works, except the splash screen.

![Splash Screen](./docs/images/loading.png)


## Build

### Prerequisites

1. Clone the repository with submodules, or update them after cloning.

   ```bash
   git submodule update --init --recursive
   ```

2. Install nxdk prerequisites.

#### Windows

> [!NOTE]
> You must use the mingw64 shell on Windows.

```bash
pacman -Syu
nxdk_dependencies=(
  "bison"
  "cmake"
  "flex"
  "git"
  "make"
  "mingw-w64-x86_64-clang"
  "mingw-w64-x86_64-gcc"
  "mingw-w64-x86_64-lld"
  "mingw-w64-x86_64-llvm"
)
moonlight_dependencies=(
  "mingw-w64-x86_64-doxygen"
  "mingw-w64-x86_64-graphviz"
  "mingw-w64-ucrt-x86_64-nodejs"
)
dependencies=("${nxdk_dependencies[@]}" "${moonlight_dependencies[@]}")
pacman -S "${dependencies[@]}"
```

#### Debian/Ubuntu Linux

```bash
nxdk_dependencies=(
  "bison"
  "build-essential"
  "clang"
  "cmake"
  "flex"
  "git"
  "lld"
  "llvm"
)
moonlight_dependencies=(
  "doxygen"
  "graphviz"
  "nodejs"
)
dependencies=("${nxdk_dependencies[@]}" "${moonlight_dependencies[@]}")
apt install "${dependencies[@]}"
```

#### macOS

```bash
nxdk_dependencies=(
  "cmake"
  "coreutils"
  "lld"
  "llvm"
)
moonlight_dependencies=(
  "doxygen"
  "graphviz"
  "node"
)
dependencies=("${nxdk_dependencies[@]}" "${moonlight_dependencies[@]}")
brew install "${dependencies[@]}"
```

### Pre-Build

1. Run the following from mingw64 or bash shell:

```bash
export NXDK_DIR="$(pwd)/third-party/nxdk"
eval "$(${NXDK_DIR}/bin/activate -s)"
cd "${NXDK_DIR}"
make NXDK_ONLY=y
make tools
```

### Configure

1. Create build directory

   ```bash
   mkdir -p build
   ```

2. Configure the project

   ```bash
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="${NXDK_DIR}/share/toolchain-nxdk.cmake"
   ```

### Build

```bash
cmake --build build
```

### Combined script

This script takes care of everything, except installing the prerequisites.

```bash
./build.sh
```

The default build directory is `build`. You can override it or force a clean build:

```bash
./build.sh --build-dir cmake-build-nxdk-release
./build.sh --clean
./build.sh build clean
```

To launch the same build from shells outside MSYS2 on Windows, use one of these wrappers:

```bat
build-mingw64.bat
```

```bash
./build-mingw64.sh
```

### Host-native unit tests

The Xbox executable cannot run directly on Windows, macOS, or Linux, so unit tests are built as a separate host-native target. Keep Xbox runtime code thin and move logic you want to test into platform-neutral sources that can be linked into `test_moonlight`.

#### Windows via MSYS2/mingw64

From `cmd.exe`, configure, build, and run the host tests with:

```bat
C:\msys64\msys2_shell.cmd -defterm -here -no-start -mingw64 -c "cd /c/Users/%USERNAME%/Dev/git/Moonlight-XboxOG && cmake --preset \"host-tests (mingw64)\" && cmake --build --preset \"host-tests (mingw64)\" && ctest --preset \"host-tests (mingw64)\""
```

If you are already inside a `mingw64` shell, the equivalent commands are:

```bash
cmake --preset "host-tests (mingw64)"
cmake --build --preset "host-tests (mingw64)"
ctest --preset "host-tests (mingw64)"
```

#### Linux or macOS

```bash
cmake -S . -B cmake-build-host-tests -DBUILD_TESTS=ON -DBUILD_DOCS=OFF
cmake --build cmake-build-host-tests --target test_moonlight
ctest --test-dir cmake-build-host-tests --output-on-failure
```

Coverage should come from this host-native test build instead of the cross-compiled Xbox build.

### CLion on Windows

The repository now includes CLion-friendly nxdk wrapper scripts in `cmake/` plus shared run configurations in `.run/`.

1. Open the project in CLion and let it import the `nxdk` preset from `CMakePresets.json`.
2. If CLion cached an older failed configure, reload the CMake project or remove `cmake-build-nxdk-release/CMakeCache.txt` once.
3. Use the normal build button with the `nxdk` profile selected.
4. Build `Moonlight.iso` first, then use the shared `Run Moonlight ISO in xemu` run configuration to launch it in xemu.

For the first xemu launch, you can either run the shared `Setup portable xemu` configuration or run the Windows wrapper manually:

```bat
scripts\setup-xemu.cmd
```

The shared CLion run configurations now call `scripts\setup-xemu.cmd` and `scripts\run-xemu.cmd` directly through `C:\Windows\System32\cmd.exe`. Those wrappers start MSYS2 with the expected `mingw64` environment and then launch the corresponding `.sh` scripts.

The setup script downloads xemu and the emulator support files into `.local/xemu`, then refreshes launcher manifests used by `scripts/run-xemu.sh`.

If you only want the emulator without the ROM/HDD support bundle, run:

```bat
scripts\setup-xemu.cmd --skip-support-files
```

## Todo

- Build
   - [x] Build in GitHub CI
   - [x] Build with CMake instead of Make, see https://github.com/Ryzee119/Xenium-Tools/blob/master/CMakeLists.txt and https://github.com/abaire/nxdk_pgraph_tests/blob/4b7934e6d612a6d17f9ec229a2d72601a5caefc4/CMakeLists.txt
   - [x] Get build environment working with CLion directly instead of using external terminal
      - [ ] debugger, see https://github.com/abaire/xbdm_gdb_bridge
   - [x] Add a run config for CLion, see https://github.com/Subtixx/XSampleProject
   - [x] Automatically run built xiso in Xemu
   - [x] Add unit testing framework
      - [x] Separate main build and unit test builds, due to cross compiling, see https://stackoverflow.com/a/64335131/11214013
      - [x] Get tests to properly compile
      - [x] Enable codecov
   - [x] Enable sonarcloud
   - [x] Build moonlight-common-c
      - [x] Build custom enet
- Menus / Screens
   - [x] Loading/splash screen
      - [x] Initial loading screen, see https://github.com/XboxDev/nxdk/blob/master/samples/sdl_image/main.c
      - [x] Set video mode based on the best available mode
      - [x] dynamic splash screen (size based on current resolution)
      - [x] simplify (draw background color and overlay logo) to reduce total size
   - [ ] Main/Home
   - [ ] Settings
   - [ ] Add Host
   - [ ] Game/App Selection
   - [ ] Host Details
   - [ ] App Details
   - [ ] Pause/Hotkey overlay
- Streaming
   - [ ] Video - https://www.xbmc4xbox.org.uk/wiki/XBMC_Features_and_Supported_Formats#Xbox_supported_video_formats_and_resolutions
   - [ ] Audio
      - [ ] Mono
      - [ ] Stereo
      - [ ] 5.1 Surround
      - [ ] 7.1 Surround
- Input
   - [ ] Gamepad Input
   - [ ] Keyboard Input
   - [ ] Mouse Input
   - [ ] Mouse Emulation via Gamepad
- Misc.
  - [ ] Save config and pairing states, probably use nlohmann/json
  - [ ] Host pairing
  - [ ] Possibly, GPU overclocking, see https://github.com/GXTX/XboxOverclock
  - [x] Docs via doxygen

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
