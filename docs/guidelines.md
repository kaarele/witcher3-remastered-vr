# Project guidelines

> Inherited from [joschi127/witcher3-vr](https://github.com/joschi127/witcher3-vr/tree/remaster),
> the Remastered port this repository continues. The local-notes, build and Git workflow sections
> describe that fork's setup. This repository's own rules are in [CONTRIBUTING.md](../CONTRIBUTING.md),
> and the Linux build is in [linux/BUILD-LINUX.md](../linux/BUILD-LINUX.md).

Guidelines for everyone working on this fork: developers and AI coding agents alike. Agent
entry files (`CLAUDE.md`, `AGENTS.md`, `.junie/guidelines.md`,
`.github/copilot-instructions.md`) only point here; keep the content in this file.

Fork of [tig3rmast3r/witcher3-vr](https://github.com/tig3rmast3r/witcher3-vr) to port the mod
from The Witcher 3 Next-Gen (DX12, patch 4.04) to the newer "Remaster" build (GOG edition
`remasteredEdition`, `witcher3.exe` 5.0.0.x). Hobby project. Upstream README/CONTRIBUTING
remain authoritative for everything not port-specific.

"The maintainer" below is the repository owner, who decides on side effects, pushes and
upstream contact.

## Local notes (git-ignored)

Machine-specific paths, the current state and the to-do list live outside git, under `local/`
(git-ignored as a whole). Read them first if present; they are not part of the repository.

- `local/NOTES.md`: current state only. Environment, game installations, installed builds,
  test commands, open points and the to-do list for the next session. Keep it short and up
  to date; replace outdated statements instead of appending.
- `local/HISTORY.md`: older status snapshots and completed steps, in chronological order. When
  a section of `NOTES.md` is no longer current, move it here (dated) rather than deleting it.
- `local/README.md`: what else is stored under `local/` (runs, backups, tools, settings copies).
- `CLAUDE.local.md` (repo root, git-ignored) only imports `local/NOTES.md` for Claude Code;
  other tools and developers read `local/NOTES.md` directly.

The notes may contain local paths and machine details; that is why they stay out of git. Their
language is the maintainer's choice; committed files are in English.

## Build

Run from the **Developer PowerShell for VS** (cmake/cl are not on the plain PATH). The presets
use the "Visual Studio 17 2022" generator. With only VS 2026 installed, add a git-ignored
`CMakeUserPresets.json` preset that inherits `release` with generator "Visual Studio 18 2026"
and its own `binaryDir`. Upstream builds cleanly that way (MSVC 14.51, all tests pass).

```powershell
.\scripts\setup-dependencies.ps1   # sparse-clones pinned deps into external/ (network)
cmake --preset release             # VS 17 2022, x64, build/release
cmake --build --preset release
ctest --preset release
```

Output is `dxgi.dll` (proxy, `src/dxgi_proxy.def`) plus `Witcher3VRLauncher.exe`.
`scripts/package-release.ps1` shows the install layout (`bin/x64_dx12`, `mods/`, `dlc/`,
`bin/config/r4game/user_config_matrix/pc`).

A full package cannot be built from this repo alone. `package-release.ps1` requires
prebuilt third-party binaries under `runtime/` and `external/` that are not in git:
PureDark AFW plugin, OFXR API layer, the ReShade and OptiScaler forks, RenderDoc.

### Releases of this fork

`scripts/package-fork-release.ps1 -Version <name> -ReleaseNotes docs/releases/<name>.md` builds the
release zip from the pinned original release (downloaded with `gh`, SHA256-checked; third-party
runtime files and game scripts unchanged) plus this repository's `dxgi.dll`, launcher, documents and
example INI. It leaves out PureDark's `PDAFWPlugin.dll` and the Fast Transitions DLC (not
redistributed, see `THIRD_PARTY_NOTICES.md`). Output goes to `local/releases/`.

Procedure when the maintainer asks for a release:
1. `remaster` clean and pushed; Release build and CTest green.
2. Write `docs/releases/<name>.md` (user-facing changes, not included parts, known issues);
   naming `v<original version>-remastered.<n>`, e.g. `v0.9.8v2-remastered.1`.
3. Run the script; check the zip (no `PDAFWPlugin.dll`, no `dlc`, our DLL/launcher hashes in
   `Witcher3VR/BUILD.txt`).
4. Install test from the zip on 5.x (and 4.04 when 4.04 code changed), launcher included, with the
   maintainer's go-ahead for game-directory changes.
5. With the maintainer's go-ahead: tag the commit and create the GitHub release with
   `gh release create <name> --target <commit> --title ... --notes-file docs/releases/<name>.md`
   plus the zip and SHA256 file, marked as pre-release.

When the original project publishes a new release, update `-UpstreamTag`, `-UpstreamAsset` and
`-UpstreamSha256` defaults in the script (asset digest from `gh release view <tag> -R
tig3rmast3r/witcher3-vr --json assets`).

Dev install procedure:
1. Install the official release once into the target game.
2. Replace only the self-built files: `dxgi.dll` goes to
   `bin\x64_dx12\witcher3vr-mod-reference\dxgi.dll`, and the launcher goes next to it in
   `bin\x64_dx12`. The launcher creates the active root copies from the reference folders,
   so a `dxgi.dll` placed directly in `bin\x64_dx12` may be overwritten.
3. Replace changed `support/` scripts in `mods\` as needed.

Windows Defender may flag self-built `dxgi.dll` files as `Trojan:Win32/Wacatac.C!ml` (ML false
positive for an unsigned hooking proxy DLL) and quarantine them from the build output and the
game folder. Add Defender exclusions for the build directory and the installed `dxgi.dll` paths.

## Tests: what they do and do not cover

- `tests/*.cpp`: unit tests of header-only policy modules (eye geometry, AER scheduling,
  HUD/transport policies, etc.). No game involved.
- `tests/*.cmake` "contract" tests: `file(READ)` + `string(FIND)` over source text. They assert
  that code fragments exist or were retired. They check text, not behaviour. Editing an
  asserted fragment breaks them, which may be intended. Update them deliberately.
- `launcher/tests.cpp` etc.: settings/path logic against a fake layout.
- `swapchain_identity_tests` calls `CreateDXGIFactory1` from the build output directory, so
  Windows loads the freshly built proxy `dxgi.dll` next to it. Older builds then wrote
  `[Hidden] uiHorizontalFrameScale/uiVerticalFrameScale` into the real
  `%USERPROFILE%\Documents\The Witcher 3\dx12user.settings`. Since `host_is_game_process()` the
  proxy writes it only inside `witcher3.exe`; builds before that commit still have the side effect.
- **Nothing tests compatibility with a real `witcher3.exe`.** A green `ctest` says nothing
  about whether hooks, offsets or shader hashes match a game build. That needs the smoke test
  and finally a headset run by the maintainer.
- Smoke test: `scripts/smoke-test.ps1 -GameDirectory <bin\x64_dx12> [-Dll <built dxgi.dll>]`
  installs the DLL and input script, points `[test]` in `witcher3vr.ini` at the script (restored
  afterwards), starts the game, waits for it to exit, collects logs and captures under
  `build\smoke-runs\<run>` and writes `summary.txt`. Exit code 0 = passed (script loaded, all
  captures written, no hook failure, no timeout). Default script:
  `scripts/test-input/smoke-continue-remaster.txt` (5.01, ~90 s); 4.04 uses `smoke-continue.txt`.
  The run starts the real game, so it needs the maintainer's go-ahead like any game start.
  `-IniSet 'openxr.enabled=1,openxr.hmd_roll_scale=0'` plus
  `-OpenXrRuntimeJson <SteamVR>\steamxr_win64.json` runs the full OpenXR/dual-render path
  without a headset through the SteamVR null driver (setup in `docs/remaster-port-status.md`,
  2026-10-13). A `flight_dump` script action writes the flight recorders and, with
  `[debug] thread_stack_sampler=1`, a render-thread CPU profile to the log. An `hmd <yaw> <pitch>`
  action turns the located OpenXR head pose (degrees, held until changed), so head-motion bugs can
  be reproduced with the static null HMD. Captures include the
  two images submitted to OpenXR (`*_xr_eye0/1.png`) when a stereo pair is active.
- In-game test mode (`[test]` in `witcher3vr.ini`, logic in `src/test_input_script.h`):
  `record_input=1` records the physical gamepad to `witcher3vr-input-recording.txt`,
  `input_script=<file>` replays a script (same format) through the XInput hook (4.04) or
  GameInput (5.01), also without a controller (5.01: virtual GameInput gamepad; a connected
  gamepad disables it), and its `quit` action closes the game window. Times are ms since the
  first Present. Diagnostic census and inventory files from `[debug]` complement it.

## Launcher behaviour worth knowing

"Configure Settings for VR" (`ConfigureGameSettingsForVr`, `launcher/config.cpp`) replaces
`Documents\The Witcher 3\dx12user.settings` with the bundled `launcher/vr_dx12user.settings`.
Only the `[Galaxy]` and `[Localization]` sections and the camera inversion keys in `[Input]`
(`InvertCameraX/Y`, `...OnMouse`) are preserved. Other user preferences are reset. A one-time
original backup is created and is restored by "Restore Original Settings".

## Architecture (short)

Almost everything is in `src/dxgi_proxy.cpp` (~55k lines). Entry: `DllMain` → `ensure_initialized`
(loads `witcher3vr.ini`, MinHook, then `install_engine_*` hooks). D3D12/DXGI vtable hooks track
resources/PSOs (FNV-1a hash of shader bytecode), game-internal hooks drive the camera/stereo
logic, OpenXR session + D3D12 swapchains submit in Present. Modes: Mono, Stereo (engine frame
factory called twice per Present), AER+AFW (alternate eye + PureDark reprojection). Witcher
scripts in `support/mod*` talk to the DLL through hooked native script functions.
Build-dependent addresses, signatures and layout values: `src/engine_addresses.h`; shader
hashes per build: `src/shader_fingerprints.h`. Details: `docs/remaster-porting-analysis.md`.

## Porting rules

- **Never run unmodified upstream builds on the Remaster.** Upstream has no version check.
  Its game hooks are raw RVAs patched blindly, and six are installed unconditionally at
  startup. This fork's build guard (`src/game_build.h`) disables engine hooks on unknown or
  unsupported builds. Only builds that contain it may be used on 5.01. Progress log:
  `docs/remaster-port-status.md`; current known problems and untested areas:
  `docs/remaster-open-issues.md`.
- Keep the two game installations strictly separate. Never copy mod files into the wrong one.
  Both share `Documents\The Witcher 3` (saves, `dx12user.settings`), so back it up before
  switching versions or using the launcher's "Configure Settings for VR".
- Keep 4.04 working: per-build address tables with 5.01 signature checks, 4.04 values unchanged.
- Agents ask the maintainer before side effects: installs, pushes, deletes, anything in game
  directories or `Documents\The Witcher 3`.
- No credentials or tokens.

## Data hygiene (what may be committed)

Allowed in git: source, tests, and docs whose content is derivable from the public source
(constant names, hook lists, methodology, plans).

Never commit (keep under `local/` or in the local notes file, both git-ignored):
- game files/assets, game or NVIDIA DLLs, save files
- user settings files from `Documents\The Witcher 3`: `dx12user.settings` contains an account
  refresh token (`[Galaxy] tokenRefA`)
- runtime logs (`witcher3vr.log`, OpenXR API dumps, crash dumps), screenshots with HUD/game content
- reverse-engineering artifacts: Ghidra/IDA projects, decompiled code, byte dumps, exe hashes
- machine/user data: local paths, user names, exact hardware identification (driver versions,
  serial numbers, device IDs), AI agent session transcripts

The test setup may be named at the level needed to interpret a result: GPU vendor or model,
headset model, OpenXR runtime, driver module names (e.g. "AMD GPU, Pico 4 over Virtual Desktop").

Address constants and byte patterns needed by the code are fine in source (upstream does the
same). The material they were derived from is not.

## Git workflow

- Remotes: `origin` = the fork (https://github.com/joschi127/witcher3-vr), default push target.
  `upstream` = tig3rmast3r/witcher3-vr, fetch only. Never push to `upstream`.
- `main` mirrors `upstream/main` and gets fast-forwards only, no fork commits.
- `remaster` is the working branch for the port and the fork's default branch on GitHub (its
  README, issue templates and the fork's issues are what visitors see). Commit directly to it in
  small, self-contained commits (easy to cherry-pick for upstream PRs later). Every commit must
  build.
- `backport/*`: fixes offered upstream, based on `main`, one commit each; never merged into
  `remaster`, which carries the same fixes in its own form.
- Which commit was verified in the game (5.01, plus a 4.04 regression check where relevant) is
  recorded in `docs/remaster-port-status.md`, including open checks. That is the reference for
  "known good".
- Separate branches only for experiments that will likely be thrown away.
- **No AI attribution in commits or PRs** (no `Co-Authored-By: <AI agent>` trailers, no
  "generated with" lines). Commits are authored by the repository owner only. The point is not
  to hide the use of AI tools; GitHub would list the tool as a contributor otherwise.
- Check before every push: no files from the data-hygiene "never commit" list, no binaries,
  no local paths or exact hardware identification in the outgoing commits.

## Port roadmap (agreed with the maintainer)

- **Build guard and address tables:** engine hooks run only on recognized builds; 5.01 hooks
  come from `src/engine_addresses.h` with signature checks, enabled hook by hook with log and
  smoke-test verification.
- **Launcher "Configure Settings for VR" must become build-aware.** Whatever is changed by hand
  from the game defaults during the port must later be applied automatically by that button,
  per detected build. This includes settings switched off to stay close to 4.04 (RT/path
  tracing, ray reconstruction, frame generation, Anti-Lag/low latency, HDR, editor preset
  detection) and AA/AO values whose meaning changed in 5.x (`AAMode`: 1 = FXAA on 5.x,
  `SSAOEnabled`). The current 4.04 profile (`launcher/vr_dx12user.settings`) must never be
  applied to 5.x. Keep the list of manual deviations up to date until the launcher implements it.
  Implemented on 2026-10-17: on 5.x the button calls `ConfigureRemasteredGameSettingsForVr`
  (`launcher/config.cpp`), which sets the list below in the existing settings file and keeps
  everything else; new 5.x findings go into that table and this list.

  5.x VR settings the button must write (verified so far; details in
  `docs/remaster-port-status.md`):
  - `[PostProcess] AAMode=1` (FXAA; 5.x values: 0 off, 1 FXAA, 5 FSR, 6 DLSS, 7 XeSS). The mod
    runs the No-AA route on 5.x; TAAU does not exist there. DLSS modes write `AAMode=6`,
    `AllowDLSS=true`, `DLSSQuality` (5.x: 1 Quality, 2 Balanced, 3 Performance, 4 Ultra
    Performance, 5 DLAA), `AllowDLSSG=false` and `AllowReflex=false`; the game runs its own DLSS
    and the DLL gives each eye its own Streamline viewport (DLSS: verified under Proton on NVIDIA
    only).
  - Sharpening off (`[PostProcess] SharpenAmount=0`, `[Rendering/TAA] EnableCAS=false`).
  - Close exploration and combat cameras (`[Gameplay] EnableAlternateExplorationCamera=1`,
    `EnableAlternateCombatCamera=1`), as on 4.04: the 5.x Modern camera re-frames while walking,
    which reads as zooming with the game pitch locked.
  - `[PostProcess] AllowShafts=false`: 5.x light shafts veil the whole image whenever the sun is
    in view, which the wide VR field of view makes almost permanent (washed-out colours).
  - Ray tracing off (`[Rendering/RT] EnableRT`, `RTAOEnabled`, `RTGIPreset`, `Shadows`,
    `EnableRtRadiance`, `BalanceModeRT`; `[Rendering/RT/PathTracer] PTEnable`), ray
    reconstruction off (`[PostProcess] EnableDLSSRR`, `[Rendering] AllowDLSSRR`), frame
    generation off (`FSRFramegen=0`, `XessFrameGeneration=false`, `DLSSGMode=0`), low-latency
    modes off (`XeLowLatency=false`, `[Viewport] AMDAntiLag=0`, `ReflexMode=0`), HDR off
    (`[Visuals] HdrEnabled=false`), editor preset detection off
    (`[Rendering] ShouldDetectEditorGraphicsPreset=false`).
  - The colour-blindness filter must stay off (the mod's replacement HUD composite does not
    reproduce it).
  - Per-eye render size = the OpenXR runtime's recommended size (INI `render_width/height`; the
    mod forces it on 5.x too); `native_stereo` per runtime (symmetric FOVs: 0).
  - Keep the user's camera inversion keys, as the 4.04 button does.

## Open decisions

- One codebase for 4.04 and the Remaster (current approach, working since 2026-10-13) or a
  separate Remaster-only fork. To be settled with upstream.

## Upstream relationship

- Upstream currently supports only Next-Gen 4.04 (latest Next-Gen update), not the Remaster.
  CONTRIBUTING's "latest official PC version only" rule predates the Remaster and should not
  be read as Remaster support. The upstream README (2026-10-13) says Remastered support is not
  yet planned.
- Remaster support was asked about upstream in
  [issue #27](https://github.com/tig3rmast3r/witcher3-vr/issues/27); on 2026-10-13 the maintainer
  reported the first playable 5.01 version there and asked how to proceed (fork review, summary
  or PR). Check it for news before larger work. On 2026-10-14 the author replied there that he
  had not looked at the code yet, is open to the fork continuing, is unsure about supporting both
  game versions in one mod, and sees the Remaster as a chance to clean up the code.
- On 2026-10-07 the author had no plan yet for a Remaster port, "not this month" (#27). In
  [#31](https://github.com/tig3rmast3r/witcher3-vr/issues/31) he wrote that he would not add or
  change anything else in the Next-Gen version, but later released Next-Gen hotfixes, so this is
  not settled. See `docs/remaster-research.md` for the public information collected so far.
- Fixes found in the fork that also apply to 4.04 are offered upstream as issues with backport
  branches (`backport/*`, based on `main`, never merged into `remaster`):
  [#33](https://github.com/tig3rmast3r/witcher3-vr/issues/33) (Full VR cutscenes: head-locked
  sky/trees and camera lights, posted 2026-10-17).
- Upstream's scope rules apply to bug reports: do not report problems from this fork or from
  Remaster builds upstream as regular bugs.
- Coordination with the upstream author is the maintainer's decision; agents never contact
  upstream on the maintainer's behalf. Keep changes focused and upstream-friendly: small
  commits, English, upstream code style, no unrelated refactors.
