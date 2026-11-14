# Video experiments

These optional Xbox programs exercise video presentation and fractional sampling independently
of a streaming host. They use the production presenter and are disabled in the default build.

## Build and run

From the repository root, using the normal build prerequisites and toolchain:

```bash
cmake -S . -B cmake-build-experiments -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCS=OFF -DMOONLIGHT_BUILD_VIDEO_EXPERIMENTS=ON
cmake --build cmake-build-experiments
```

On Windows, use an MSYS2 MinGW64 shell and add `-G "MinGW Makefiles"` to the configure command.
The Xbox child build selects Unix Makefiles automatically. The generated files are:

| Probe             | ISO                                                        | XBE                                                                  |
| ----------------- | ---------------------------------------------------------- | -------------------------------------------------------------------- |
| Presentation      | `cmake-build-experiments/xbox/PvideoPresentationProbe.iso` | `cmake-build-experiments/xbox/pvideo-presentation-probe/default.xbe` |
| MPEG-2 prediction | `cmake-build-experiments/xbox/Mpeg2MotionProbe.iso`        | `cmake-build-experiments/xbox/mpeg2-motion-probe/default.xbe`        |

Launch an ISO with the repository's configured xemu installation, for example:

```bash
./scripts/run-xemu.sh --iso cmake-build-experiments/xbox/PvideoPresentationProbe.iso
```

From a Windows command prompt, use `scripts\run-xemu.cmd --iso` with the same ISO path.
The probes use GPU presentation directly; `MOONLIGHT_ENABLE_GPU_PRESENTATION` controls the
streaming client independently.

## Presentation probe

The probe checks numeric statistics formatting, displays a gray CPU framebuffer, then runs two
overlay sessions. Each shows rotating red, green, blue and white bars scaled from an HD source.
After both sessions, **PVIDEO PROBE COMPLETE** appears on the restored CPU framebuffer.

Inspect the colors, scaling, buffer updates and framebuffer restoration. Submission or teardown
failures display an error. In xemu, use a graphical display: display updates drive its emulated
vertical blank, so a headless run cannot validate the presentation path.

## MPEG-2 prediction probe

The probe cycles integer and half-pixel motion offsets. A CPU-computed luma reference appears on
the left, and GPU sampling appears on the right. Compare the two while checking the displayed
motion vector. The sequence repeats until the program is closed.

This is a visual comparison of one prediction stage, not a compressed-video decoder or a
bit-exact reconstruction test. The overlay is composed during scanout and cannot be read back
through the RGB framebuffer. Console testing is needed to assess image correctness and
performance on hardware; emulator output alone cannot establish those properties.
