# Sunshine pairing research notes

## Current findings

- The vendored `third-party/moonlight-common-c` tree exposes streaming, connection testing, and input APIs through `Limelight.h`.
- The current public API surface does not expose a ready-made host pairing entry point.
- The library clearly expects host metadata from `/serverinfo` and session startup through `/launch` and `/resume`, but pairing is not currently wrapped by an app-facing helper in this tree.
- The current shell UI was previously pretending pairing existed. That is why the pair action remains disabled until a real backend is added.

## Practical implication for this project

A real Sunshine pairing flow in this codebase should be implemented as a project-owned adapter instead of another reducer-only placeholder.

## Recommended adapter boundary

Create a narrow module that owns these responsibilities:

1. Open an HTTPS connection to the target host.
2. Query `/serverinfo` and validate the host response.
3. Create or load the client certificate/key material used for pairing.
4. Start the Sunshine pairing request with the generated four-digit PIN.
5. Poll or continue the handshake until Sunshine reports success or failure.
6. Persist the resulting client identity and update the saved host record to `paired` only after host-confirmed success.

## Proposed implementation order

1. Add a platform-neutral `src/network/sunshine_pairing.h/.cpp` adapter interface.
2. Implement host-native parsing and state tests for the adapter responses before wiring it into SDL.
3. Use the vendored OpenSSL already present in the Xbox build to handle TLS and certificate material.
4. Add reducer events for:
   - pairing requested;
   - pairing started;
   - PIN ready;
   - pairing succeeded;
   - pairing failed;
   - pairing cancelled.
5. Re-enable the `Pair Selected Host` and `Start Pairing` actions only after the adapter can report real host-backed success.

## Non-goals for the first pairing slice

- LAN discovery
- unpair support
- host app-list browsing
- session launch or resume changes beyond what pairing needs

## Why this note exists

This project is at the point where the UI shell is ready for a real backend, but `moonlight-common-c` does not currently give this repository a drop-in pairing call. The next change should therefore add a small, testable Sunshine-specific adapter rather than extending the placeholder reducer state again.

