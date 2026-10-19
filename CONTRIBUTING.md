# Contributing to Witcher 3 Remastered VR

Thanks for helping improve Witcher 3 Remastered VR.

## Bug reports

This project continues [tig3rmast3r/witcher3-vr](https://github.com/tig3rmast3r/witcher3-vr), the
original Witcher 3 VR mod, and the
[Remastered port by joschi127](https://github.com/joschi127/witcher3-vr/tree/remaster). It adds
Linux/Proton support and Remastered 5.01 VR improvements. Report problems with this project in
[its own issues](https://github.com/kaarele/witcher3-remastered-vr/issues) using the
[bug report form](https://github.com/kaarele/witcher3-remastered-vr/issues/new?template=bug_report.yml)
and provide enough information to reproduce the problem. Do not report them in the upstream
repositories.

Bug reports must be reproduced on The Witcher 3 Remastered, patch 5.01, the build this project
supports. For Next-Gen 4.04, use the [original mod](https://github.com/tig3rmast3r/witcher3-vr).
Other versions and rollback branches are outside the support scope. Reports from untested setups
(Windows, SteamVR, other GPUs and headsets) are welcome, whether the mod works there or not.

Third-party mods may be used, but mod compatibility is outside the current
support scope. Disable all other mods and reproduce the problem before opening
an issue. Reports for problems that occur only in a modded configuration will
be closed until the base project is stable enough for compatibility work.

For renderer bugs, include:

- Operating system (Windows, or the Linux distribution and Proton version)
- GPU and driver version
- Headset, connection method, and OpenXR runtime
- Game store and patch
- Render mode, anti-aliasing backend and DLSS mode
- Exact reproduction steps
- `witcher3vr.ini`
- A diagnostic log (`bin/x64_dx12/witcher3vr.log`), captured with **Diagnostic Logging** enabled
  in the launcher, or with `[debug] logging_enabled=1` and `runtime_diagnostics=1` in
  `witcher3vr.ini`
- A save file when the issue depends on a specific location or scene

Logs can contain system information and local file paths. Review attachments
before publishing them.

## Feature requests

Search existing issues before opening a request. Motion-controller support is
not currently planned, so duplicate requests for it will be closed.

## Code contributions

Discuss substantial renderer changes in an issue before starting a pull
request. Renderer fixes require in-game evidence; successful compilation alone
is not sufficient validation.

Before submitting:

1. Run `scripts/setup-dependencies.ps1`.
2. Configure and build the Release preset.
3. Run the automated tests.
4. Test the affected render routes in game.
5. Keep each change focused and document its runtime result.

```powershell
.\scripts\setup-dependencies.ps1
cmake --preset release
cmake --build --preset release
ctest --preset release
```

On Linux, `dxgi.dll` is cross-built with clang-cl and an xwin sysroot (see the README and
[linux/BUILD-LINUX.md](linux/BUILD-LINUX.md)); that build does not include the launcher or the
tests. Without a headset, the headless OpenXR runtime in `tools/w3vr-nullxr` runs the full VR path
and saves the submitted eye images. Headless results are useful evidence, but say in the pull
request whether a change was also checked in a headset.

Technical notes on how each 5.x feature works are in
[docs/remaster-port-status.md](docs/remaster-port-status.md); known problems are in
[docs/remaster-open-issues.md](docs/remaster-open-issues.md). Keep both current when a change
affects them. Code comments use the upstream marker tags (`[FEATURE:...]`, `[FIX:...]`,
`[PERF:...]`, `[DIAG:...]`, `[DEBUG:...]`).

Do not commit:

- Game files or assets
- NVIDIA or game runtime DLLs
- Personal configuration
- Save files
- Logs, dumps, symbols, or reverse-engineering artifacts
- Compiled DLLs, executables, or release archives

By contributing code, you agree that your contribution may be distributed
under the repository's MIT License, except in `tools/w3vr-nullxr`, which is
GPL-3.0-or-later (see its `LICENSE`). Third-party code must retain its original
license and be recorded in `THIRD_PARTY_NOTICES.md`.
