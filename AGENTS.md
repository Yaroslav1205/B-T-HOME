# Project instructions

This is B-T HOME firmware, initially for ESP32-C6. Read README.md and
docs/ARCHITECTURE.md before making structural changes. User-facing documentation
is Ukrainian; code, identifiers and code comments are English.

## Boundaries

- ESP-IDF native CMake components; validated SDK baseline is v5.4. C++17-compatible
  application code, no exceptions or RTTI required. Do not introduce Arduino or
  PlatformIO without a concrete need.
- `main/application.cpp` is the composition root. Construct and register modules
  explicitly, with providers before consumers. Keep app_main.cpp minimal.
- `bth_core` and portable module logic must compile without ESP-IDF. Inject narrow
  interfaces into modules. Put ESP-IDF implementations in `bth_platform` or a
  dedicated adapter component. Do not build a global service locator.
- Board data belongs in `boards/<profile>/` and `bth_board`. Never guess GPIO,
  active polarity, flash capacity, PSRAM or capabilities from the chip name.
- Public includes go under `include/bth/<module>/`. Declare public dependencies
  with REQUIRES, implementation-only ones with PRIV_REQUIRES. Avoid cycles.
- Keep explicit source lists in CMake. Do not use globbing or auto-registration.

## Lifecycle and reliability

- Runtime is single-task and non-owning, with 16 fixed slots. Declare modules and
  their injected dependencies before Runtime; all referenced IDs/arrays must
  remain valid and immutable for the Runtime lifetime.
- `init` acquires resources, `start` begins work, `tick` is short and nonblocking.
  `stop` and `deinit` must be idempotent and safe after partially failed operations.
- Stop/join owned tasks, timers and callbacks before releasing their resources.
  Do not call Runtime from callbacks/ISRs. Use bounded queues for cross-task work.
- A non-ok lifecycle result is fatal to the assembled application: Runtime rolls
  back all modules. Expected temporary conditions such as reconnects stay inside
  a module state machine; do not return a fatal error for normal recovery.
- No automatic NVS erase, guessed pin initialization, or reboot loops on failure.
- Add abstractions only when a real consumer needs them. Do not prebuild unused
  network stacks, event buses, persistence, OTA or tasks.

## Change workflow

- See docs/ADDING_MODULE.md and `python scripts/new_module.py <name>`.
- Add meaningful host tests for changed lifecycle/state logic. Run
  `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1` on Windows.
- Build affected targets with `scripts/idf.ps1`; ESP32-C6 is the default.
  This workspace contains spaces: the wrapper uses SUBST and direct CMake.
- Each profile has its own `build/<profile>/sdkconfig`. Persist intended settings
  in shared/profile defaults or Kconfig; defaults do not override an existing
  sdkconfig. Never commit generated build outputs or credentials.
- Update docs when contracts, commands or configuration change. Distinguish
  host tests, successful compilation and physical device validation in reports.
- Do not flash hardware unless the user requests it. Do not modify installed
  SDKs, global Git settings or unrelated files to fix a project-local problem.
