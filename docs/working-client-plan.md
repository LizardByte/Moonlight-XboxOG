# Working Moonlight client plan

## Goals

Turn `Moonlight-XboxOG` into a usable Moonlight client for the original Xbox by building the client in small, testable milestones. The near-term work should focus on host-native coverage, controller-first UI flows, structured logging, pairing, host management, and a streaming pipeline that can later connect the Xbox runtime to `moonlight-common-c`.

## Architecture decisions

### Streaming core

- Use the vendored `third-party/moonlight-common-c` codebase as the transport, pairing, RTSP, ENet, control, and input foundation.
- Treat `moonlight-embedded` as a reference implementation, especially for host discovery, pairing, and session setup.
- Keep the first shipping codec target narrow: H.264 video plus stereo Opus audio.
- Delay FFmpeg integration until there is a clear Xbox-native decode gap that cannot be filled with a lighter custom H.264 path.

### UI stack

- Build a project-owned retained-mode UI layer on top of `SDL2`.
- Use `SDL_ttf`, which already exists in the vendored `nxdk` tree, for text rendering.
- Prefer a local icon atlas and project-owned widgets over adding a heavy UI submodule.
- Keep the UI model platform-neutral so controller navigation, focus, and menu state can be covered by host-native gtests.

### Input model

- Make the controller the primary navigation path.
- Map keyboard input into the same abstract UI commands so the host-native build and emulator workflows behave the same way.
- Treat mouse support as optional until nxdk input support is validated for the relevant devices.
- Allow a controller-driven virtual cursor mode later for streamed desktop interactions.

### Logging and observability

- Use structured log entries with severity, category, and message fields.
- Keep a ring buffer for on-screen diagnostics and crash-adjacent inspection.
- Mirror accepted log entries to the platform debug console.
- Build the statistics overlay from typed telemetry snapshots instead of formatting strings at capture sites.

### xemu networking

- Explicitly support launcher-controlled networking modes.
- Keep the default user-mode network path for simple outbound connectivity.
- Add tap networking support for LAN discovery and broadcast-sensitive workflows.
- Keep the xemu runtime state inside `.local/xemu` so pairing files, EEPROM data, and launcher config stay reproducible per workspace.

## Milestones

### M0: Test-first core shell

- Expand the host-native unit test surface with platform-neutral modules under `src/app/`, `src/input/`, `src/logging/`, and `src/streaming/`.
- Build the retained menu model, keyboard and controller command mapping, and overlay formatting in isolation.
- Add a proper logging core and use it from startup and future runtime services.

### M1: Rendered home shell on Xbox

- Render a real home screen with text, focus states, and controller navigation.
- Add placeholder screens for Hosts, Add Host, and Settings.
- Show the new logging buffer and stats overlay from the Xbox runtime.

### M2: Host records, discovery, and pairing

- Add persistent host records and pairing state.
- Start with manual IP entry and PIN pairing.
- Add LAN discovery after the manual path is working in both xemu and on hardware.
- Cover parsing, persistence, and state transitions with host-native tests.

### M3: Session control plane

- Query host capabilities, app lists, and current sessions.
- Support launch, resume, and quit flows.
- Add connection preflight checks using `moonlight-common-c` port helpers and surface actionable log messages in the UI.

### M4: Streaming pipeline

- Connect `moonlight-common-c` callbacks to Xbox-specific video, audio, and input backends.
- Start with H.264 and stereo Opus.
- Add frame pacing, reconnect handling, controller rumble, and proper cleanup.

### M5: User-visible polish

- Replace placeholder menus with full host and app detail screens.
- Flesh out settings, overlay customization, input tuning, and controller hotkeys.
- Add pause and status overlays, stream problem notifications, and better recovery flows.

## Testing strategy

- Keep all non-rendering business logic platform-neutral and covered by gtests.
- Prefer reducers, models, and adapters that can be exercised without SDL or nxdk.
- Add targeted emulator smoke tests for launcher behavior and runtime boot flow.
- Add parser, persistence, and connection-state tests before integrating network or pairing code into the Xbox runtime.
- Use xemu only for integration coverage after host-native tests already lock in behavior.

## Initial implementation in this change

This changeset starts M0 by adding:

- a structured in-memory logger;
- controller and keyboard command mapping for menus;
- a retained menu model that skips disabled entries and reports activations;
- an initial top-level client shell state machine for Home, Hosts, Add Host, and Settings;
- a typed streaming statistics overlay formatter;
- launcher groundwork for explicit xemu networking configuration and portable runtime state.

