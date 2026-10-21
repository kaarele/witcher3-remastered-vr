# Remaster port: status log

Chronological record of what has been verified. Details live in `remaster-porting-analysis.md` (code
dependencies, runtime findings) and `remaster-research.md` (public information). Raw logs,
reverse-engineering output and machine-specific data stay out of the repository.
Current known problems and untested areas: `remaster-open-issues.md`.

## 2026-10-10

### Environment and baseline
- Two game installations side by side on GOG: Next-Gen 4.04b (separate GOTY product) as the
  reference, and Remaster 5.01. Both run without the mod.
- Upstream v0.9.8v2 builds with Visual Studio 2026 (MSVC 14.51) through a local
  `CMakeUserPresets.json` that inherits `release` with the "Visual Studio 18 2026" generator.
  All upstream tests pass.
- Baseline on 4.04 with a Pico 4 over Virtual Desktop (VDXR), Stereo + TAAU, AMD GPU:
  - **Run 1: official release.** Works. No hook or OpenXR errors.
  - **Run 2: self-built `dxgi.dll` from the same commit.** Behaves identically (same OpenXR setup,
    same set of log message kinds, comparable frame pacing).
  - So the local toolchain is a valid base for the port.
- Observations on this setup:
  - VDXR reports the system name "Oculus Quest2" for the Pico 4.
  - With Virtual Desktop's FOV-tangent reduction, both eyes get a symmetric FOV, so asymmetric
    projection has no effect.
  - DLSS/DLAA are unavailable on AMD, so TAAU is the reference AA path on 4.04.

### Settings comparison (fresh defaults 4.04 vs 5.01)
See `remaster-porting-analysis.md` §6. In short:
- TAAU settings are gone in 5.01, `AAMode` semantics changed, and RT plus wider distances
  are on by default.
- The launcher's 4.04 VR profile must not be applied to 5.01.
- For tests, a Remaster "parity" profile is used: shared keys get the 4.04 VR-profile values,
  and new features (RT, ray reconstruction, editor preset detection) are switched off.
  "Configure Settings for VR" should later apply this automatically per build (see roadmap
  in `docs/guidelines.md`).

### Build guard (first code change)
- `src/game_build.h` identifies the exe from PE `TimeDateStamp` + `SizeOfImage` (4.04, 5.01,
  unknown). `engine_build_supported()` in `dxgi_proxy.cpp` logs the result once and makes
  all 22 `install_engine_*` / `initialize_engine_*` routines return early on unsupported builds.
  The unit test is `tests/game_build_tests.cpp`.
- Verified on 4.04: `engine_hooks=enabled`, hooks installed as before.
- Verified on 5.01: `engine_hooks=disabled_unsupported_build`. The proxy loads and the game
  runs stably (menu, map, Witcher Senses, free roaming). No engine patches are applied.

### Key finding: command-list interception on 5.01
- On 5.01, D3D12 device, queue and DXGI hooks fire, but **no command-list hook fires**
  (zero draws counted).
- A diagnostic (`[DIAG:COMMAND-LIST-VTABLE-MODULE]` in `dxgi_proxy.cpp`) found the cause:
  - 5.01 uses the bundled D3D12 Agility SDK runtime.
  - Its command lists carry a per-object function table whose entries point directly into the
    GPU driver (AMD `amdxc64.dll`). The detours on the runtime's functions are bypassed.
- Disabling the Agility SDK is not possible: 5.01 then refuses to start ("Support for
  DirectX 12 is required").
- Next step: intercept command lists by patching each list's embedded function table at
  creation/reset. Success criterion: draws and PSO binds appear in the log on 5.01. After
  that, check which of the 28 shader fingerprints still match.

### Script side
- All vanilla script methods wrapped by `support/mod*` still exist in 5.01 (signatures not
  yet compared).
- 5.01 save files carry an extra `.json` with build id and mod list.

### Tooling notes
- `ctest` loads the built `dxgi.dll` (`swapchain_identity_tests`), which writes the HUD frame
  scale into the real `Documents\The Witcher 3\dx12user.settings`. Back up the file or restore
  it after running the tests.
- With `[openxr] enabled=0`, set `render_width`/`render_height` to 0. Otherwise the proxy
  forces the swapchain to the VR per-eye size and the flat image is stretched.

## 2026-10-11

### Embedded command-list tables patched (branch `port/embedded-command-list-table`)
- Mechanism: every command-list hook slot is registered. After `Reset` and at
  `ExecuteCommandLists`, the hook is written into each list's embedded function table. The
  original pointers used by the hooks dispatch per list: the remembered embedded entry for
  embedded-table lists, the MinHook trampoline otherwise. 4.04 lists have a shared vtable and
  keep the old path.
- Verified on 5.01, flat (no OpenXR), AMD GPU: stable, no visible issues, no errors in the log.
  - The dummy list used to place the detours has a shared vtable. The game's lists have
    embedded tables at object + 0x5C8.
  - Patched slots: DrawInstanced, DrawIndexedInstanced, Dispatch, SetPipelineState,
    SetDescriptorHeaps, SetCompute/GraphicsRootSignature, SetCompute/GraphicsRootDescriptorTable,
    SetGraphicsRootConstantBufferView. Embedded entries point into the driver (`amdxc64.dll`),
    except SetDescriptorHeaps, which points to a separate `D3D12Core.dll` function.
  - The embedded `Reset` entry is the detoured runtime function, so the Reset hook fires without
    patching.
  - No list had an entry differing from the first remembered one.
  - Draws are now counted (about 1,900 indexed draws per frame in free roaming, 0 before).
- Verified on 4.04 with the same build (commit `ac8cd7f`), Pico 4 over Virtual Desktop, Stereo +
  TAAU: no table is patched (shared vtables), OpenXR setup, draw counts, log message kinds and
  frame pacing match the earlier reference run. No visible difference.
- Open: compare the 28 shader fingerprints on 5.01. The first recording of a newly created list (before its first Reset/submission) is not yet
  intercepted. Hook `CreateCommandList`/`CreateCommandList1` if that turns out to matter.

### Shader fingerprint census (4.04 vs 5.01)
- New diagnostic: `[DIAG:SHADER-FINGERPRINT-CENSUS]` with a table of the 29 fingerprints in
  `src/shader_fingerprint_census.h`, a per-run shader inventory file and an optional bytecode dump
  (`[debug] shader_inventory_dump=1`). Same route on both builds (main menu, load, outdoor view
  with trees, Witcher Senses, map).
- 4.04: 21/29 known fingerprints seen. Missing: the six RT shaders (RT off) and the TAAU motion
  writer pair (not created on this route).
- 5.01: **0/29.** No shader hash of 4.04 occurs in 5.01; every shader was recompiled (newer DXIL
  with native 16-bit types). Shader and pipeline counts are of the same order, and no bound
  pipeline escaped the creation hooks on either build, so no extra creation path needs hooking.
- Offline matching of the bytecode dumps (signatures, resource bindings, thread groups, DXIL op
  histograms) finds plausible 5.01 counterparts for most of the 21 fingerprints. Several are
  still ambiguous and need a closer check before new hashes go into the code.
- Shared constant buffers grew in 5.01:
  - `CameraShaderConsts` (b6, mirrored by the mod's replacement HLSL): append-only, all 4.04
    offsets unchanged, two matrices and one vector added at the end.
  - `SharedPixelConsts`: unchanged up to 0x7F0. One array there grew by 0x100, so everything
    after it moved by 0x100, and new members were appended. The tiled-culling path checks the
    4.04 size (4608 bytes, 4368 aligned to 256) and therefore falls back safely on 5.01.
  - `CSConstants` (tiled light/dimmer compute): members inserted near the start and in the
    middle, so most offsets moved.
- Next: resolve the ambiguous matches, then list for each feature which constant-buffer offsets
  it reads and whether they moved.
- Central fingerprint table `src/shader_fingerprints.h` (one entry per recognized shader, hash per
  build). All call sites go through `matches()`. 5.01 hashes are mapped where the feature's other
  assumptions were checked: terrain motion replay, smoke, foliage snapshot, aim probe. Not yet
  mapped: HUD composite (two render targets), tiled culling (moved offsets), nudge probes
  (ambiguous), RT history (not compared), TAAU (removed in 5.01). Unverified at runtime; the
  census logs `build=5.01` sightings.

### Test mode (unattended runs)
- `[test]` input scripts, gamepad recording and PNG captures work on 4.04: the smoke script
  (`scripts/test-input/smoke-continue.txt`) skips the intros, continues the latest save, walks,
  uses Witcher Senses, opens the game menu and quits, without anyone at the controller.
- Flat stereo without OpenXR (`mode=3`, `[openxr] enabled=0`) renders both eyes alternately;
  two consecutive captures show the two eyes with the expected horizontal offset.
- **5.01 reads the gamepad through GameInput** (the game loads `GameInputRedist.dll`; the exe also
  references ScePad). It never calls `XInputGetState` (0 calls in a full run), so the XInput hook
  sees nothing: test scripts cannot drive 5.01 yet, and the first-person snap-turn/head-follow
  input remap (XInput) has no effect there.
- Test mode now also hooks GameInput on 5.01 (`GameInputInitialize` → IGameInput v3
  `GetCurrentReading`/`GetNextReading` → `IGameInputReading::GetGamepadState`). Verified: with a
  connected gamepad every reading goes through the hook and the game switches to gamepad prompts.
  Without a connected gamepad GameInput returns no readings, so 5.01 runs need a controller.
- 5.01 smoke script (`scripts/test-input/smoke-continue-remaster.txt`, derived from a manual
  5.01 recording) runs unattended with a switched-on gamepad: intros, Continue, walk, camera
  turn, Witcher Senses, game menu, then EXIT through the system menu (the game closes itself,
  the `quit` fallback is not needed).
- Virtual gamepad for 5.01 test runs: the game registers one GameInput device callback for
  gamepads and then queries `GetCurrentReading(Gamepad, device)` for each reported device. With
  a script loaded and no real gamepad reported 3 s after the first Present, the test mode reports
  a virtual device through that callback and answers its queries with a reading that carries the
  script pad. The game only calls `GetDeviceInfo` on it. Verified: the full smoke script runs
  with the gamepad switched off and disconnected.

## 2026-10-12

### Smoke test
- `scripts/smoke-test.ps1` runs the 5.01 smoke script unattended (no controller needed) and
  checks the log: script loaded, all captures written, no hook failure or signature mismatch,
  no timeout. A full run takes about 90 s.

### First engine hooks on 5.01 (per-build address table)
- `src/engine_addresses.h` holds per-build RVAs. 4.04 keeps its original values. A 5.01 entry
  also carries the expected function bytes, which are checked at runtime before hooking
  (`engine_function_target()`); a mismatch disables that hook. Hooks resolved this way run on
  5.01 although `engine_build_supported()` is still false for it.
- Method: the 5.01 address comes from the earlier exe matching; the 4.04 function bytes must
  occur exactly once in the 5.01 exe, at that address. Struct offsets the hook (or code
  mirroring it) relies on must be visible and unchanged in those bytes.
- `IsAnyMenu` (5.01 RVA 0x01DF41A0, whole 35-byte function identical): verified on 5.01, the menu
  state toggles while loading and with the game menu (START/B).
- `IsLoadingScreenVideoPlaying` (5.01 RVA 0x0227A0F0, identical incl. the +0x198/+0x38/vtable
  +0x18 layout mirrored by `poll_engine_loading_screen_video_state()`): moved out of the
  camera-authority installer into its own installer.
- Scene queries, unique and byte-identical in 5.01, now resolved through the table:
  `Actor.IsInGameplayScene` 0x0210B410 (+0x2F3), `Actor.IsInNonGameplayCutscene` 0x0210B430
  (+0x2F2), `Game.IsCurrentlyPlayingNonGameplayScene` 0x022753C0 (+0x11F). The flags read by
  `read_native_camera_authority_snapshot()` are the same offsets. Verified on 5.01: all three
  hook; the game calls the cutscene and global scene queries (result 0 in free roaming).
  `IsInGameplayScene` was not called on this route (on 4.04 mainly the mod scripts call it,
  which are not installed in the 5.01 test setup).
- **5.01 is an AVX build** (VEX-encoded SSE instructions), so most functions are not
  byte-identical even where the logic is unchanged; byte comparison only works for small
  integer-only functions. **Camera object layout moved:** `GetCameraDirection` reads the
  direction at +0x790 in 5.01 (4.04: +0x6C0, so +0xD0). The camera director list layout
  (+0x58 entries, +0x60 count, stride 0x28) is unchanged. Every struct offset used on camera
  objects therefore needs a 5.01 check.
- `CameraDirector.GetCameraDirection` located at 5.01 RVA 0x023826F0 (registered script name;
  both builds have an identical second copy 0x110 bytes later, the hooked one is the first in
  both). The hook reads no camera fields, but **MinHook cannot hook it on 5.01**:
  `MH_CreateHook` returns `MH_ERROR_UNSUPPORTED_FUNCTION`, because its disassembler (HDE64)
  rejects VEX-prefixed instructions and the function has one at byte 4, inside the 5 bytes the
  jump overwrites. This affects small leaf functions; the larger 5.01 candidates (frame data
  factory, view rebuild, temporal writer, gameplay entry, head-bone getters, ...) start with
  plain prologues. Options: patch the native-function registration instead of the code for
  script natives, or teach the trampoline builder VEX instruction lengths. Left 4.04-only.
- Still 4.04-only in the camera-authority installer: `GetTopmostCameraObject` (5.01 candidate
  0x023825D0, compiled differently but same director layout: +0x58 entries, +0x60 count,
  stride 0x28) and `EnableManualControl` (5.01 RVA 0x01E0D800 by registered script name, plain
  prologue). Comparing `EnableManualControl` shows the **custom-camera fields moved by +0x10**
  in 5.01: manual-control flag 0x259 → 0x269, float pairs 0x248/0x250 → 0x258/0x260; the +0x1C0
  pointer is unchanged. The hook and `read_native_camera_authority_snapshot()` read 0x259, so
  enabling them needs a per-build field offset. Worth doing together with the view/frame
  hooks that consume the snapshot.
- `GameplayEntry` (4.04 0x01D00260) confirmed at 5.01 RVA 0x01D0C570: instruction-for-instruction
  the same structure. Fields of the global game object it reads moved by +8 (0x78 → 0x80,
  0x80 → 0x88, 0x88 → 0x90) and +0xC (0x91 → 0x9D); fields of the inner object (0x280, 0x2A0,
  0x70) are unchanged. Not hooked yet: it only matters together with the frame builder and its
  return-address identities.
- 4.04 regression (commit `885c229`, flat mono smoke test): passed. All table-resolved hooks
  land on the original 4.04 addresses; the game calls all three scene queries there.

### Third-person stereo core on 5.01 (mapping)
The second eye comes from the frame-factory hook (calls the factory twice, enqueues the second
frame itself); the view-rebuild hook sets the per-eye camera; gameplay entry maps frames to eyes on
the render thread; render-scene setup paces pairs. Located through the call graph of both builds
(Ghidra decompilation, local only) and the `CRenderCommand_RenderScene` / `CCustomCamera` RTTI:

| Role | 4.04 | 5.01 | Evidence / layout notes |
|---|---|---|---|
| Render-scene setup | 0x01553070 | 0x0227D1F0 | calls the factory, builds a RenderScene command |
| Frame-data factory | 0x016214C0 | 0x022C0820 | calls view rebuild, then the same wrapper |
| Scene enqueue | 0x01621FC0 | 0x022BE990 | same logic; render-context field +0x620 → +0x5440, scheduler global 0x057F5930 → 0x05A5B8F0 |
| View rebuild | 0x015FE550 | 0x02293240 | called from the factory |
| Factory → view rebuild return | 0x0162196D | 0x022C101A | |
| Wrapper → view rebuild return | 0x015FF863 | 0x02292EA7 | |
| Gameplay entry → frame builder | 0x01D00260 → 0x01D86400 (return 0x01D002CF) | 0x01D0C570 → 0x01C1A210 (return 0x01D0C5E1) | task +0x8 frame, +0x10 extra unchanged |
| `CCustomCamera` vtable (subobject +0x170) | 0x02D645B0 | 0x03810198 | RTTI complete-object locators |

Layout changes that block a direct port:
- Scene descriptor: the factory reads up to about +0xB765 in 4.04 (the mod copies 0xC000) and up
  to about +0xF714 in 5.01. The second-eye copy size must grow accordingly (to be confirmed).
- View array: indices 0x00–0xAB (position, angles, FOV, aspect, near/far, projection at 0x80,
  basis at 0xA0/0xA4/0xA8) are used identically. **Centre offset and viewport moved from
  view[0x100..0x103] to view[0x130..0x133]**; 5.01 uses 0x100–0x12F for matrix copies, so the
  hook's writes to view[0x100]/[0x102] must not run unchanged on 5.01. view[0x11A] has a new
  meaning as well.
- Ported through the address table (5.01 values in `src/engine_addresses.h`): frame factory,
  scene enqueue, view rebuild, gameplay entry, render-scene setup, `GetTopmostCameraObject`,
  the CCustomCamera vtable, both view-rebuild return addresses, the scene-descriptor copy size
  (5.01: 0x10000, minimum 0xF800) and the centre-offset/viewport index (`engine_view_centre()`,
  0x100 on 4.04, 0x130 on 5.01; all 30 accesses go through it). Frame builder, temporal writer,
  shadow cascade and the TAAU/DLSS hooks stay 4.04-only.
- Verified on 5.01 (flat, `mode=3`, OpenXR off): all of these hook; at runtime the director's top
  camera carries exactly the predicted 5.01 CCustomCamera vtable (0x03810198) and the scene flags
  read 0 in free roaming; the view rebuild accepts the gameplay camera and renders the eyes
  alternately per frame (two consecutive captures show the expected horizontal offset), as the
  4.04 flat stereo run does. The real dual render (two frames per Present) only starts with a
  valid HMD pose, so it needs an OpenXR run with the headset.
- 4.04 regression of this build (commit `328679e`, flat stereo `mode=3`): passed; the alternating
  eyes show the same offset as before and stay sharp (the mod's per-eye TAAU history).
- On 5.01 the alternating eyes blur: the game's own temporal anti-aliasing blends left and right
  frames, and the per-eye history only exists for 4.04 TAAU. The 5.01 VR baseline should use FXAA
  (no temporal history); the strict pair mode renders both eyes per Present, but consecutive
  frames still alternate eyes, so temporal AA would mix them there as well.
- Not ported yet: frame builder and temporal writer, which 4.04 needs for the asymmetric
  projection (`native_stereo=1`). On 5.01 the engine itself applies the centre offset at
  view[0x130] when building the projection, so a first headset test should start with symmetric
  projection (`native_stereo=0`) and then compare.
- Temporal writer located at 5.01 RVA 0x022BD750: same logic as 4.04 0x015E5A90 (writes the
  centre/jitter block of two views, calling the view rebuild after each). It shows the 5.01 frame
  layout: first view at frame+0x10 in both builds, second view at +0x5F0 instead of +0x520 (view
  stride 0x510 → 0x5E0), centre blocks at +0x4D0/+0xAB0 instead of +0x410/+0x920. 5.01 calls it
  from the frame builder once and from two other functions; render-scene setup no longer calls it,
  so the 4.04 return-address list does not carry over. Frame-builder offsets (+0x420/+0x930,
  +0x428/+0x938, +0x530, +0xA4C, +0x560) need to be re-derived with this stride.

## 2026-10-13

### Headset-free OpenXR runs (SteamVR null driver)
- The SteamVR runtime with its built-in null driver provides a virtual HMD (fixed pose, 72 Hz,
  configurable render size) with the full OpenXR frame loop: session, swapchains, xrWaitFrame,
  xrEndFrame. `scripts/smoke-test.ps1 -OpenXrRuntimeJson <SteamVR>\steamxr_win64.json` selects it
  for the game process only (`XR_RUNTIME_JSON`); the system runtime stays unchanged. `-IniSet`
  changes INI values for one run (`openxr.enabled=1`, `openxr.native_stereo=0`, ...).
- SteamVR needs `steamvr.forcedDriver=null`, `steamvr.requireHmd=true`,
  `steamvr.activateMultipleDrivers=false`, `driver_null.enable=true` and
  `steamvr.startMonitorFromAppLaunch=false`. Without the last one vrmonitor launches Room Setup
  for the uncalibrated null HMD, which sends the game a quit event.
- The null driver reports a constant orientation with 180° roll; use
  `openxr.hmd_roll_scale=0` in these runs, otherwise the scene renders upside down.
- Nothing about the headset path is covered (lenses, real tracking, Pico/VDXR specifics), but the
  complete mod pipeline runs: dual render, pair barrier, OpenXR submission, HUD/smoke routes.
  Successful `xrEndFrame` calls alone do not prove that images were submitted: check the
  `OpenXR layer transition` lines (and the null driver's "Headset Window"). The null HMD also
  enters standby after a few seconds without motion (session drops to SYNCHRONIZED); set
  `power.pauseCompositorOnStandby=false` and a long `power.turnOffScreensTimeout`. SteamVR reports
  every null-HMD frame as dropped on both builds; that counter is meaningless there.

### First dual render on 5.01 (Mode 3, symmetric projection)
- Verified unattended (smoke script, null HMD, `native_stereo=0`): the dual render starts, both
  eyes are produced per pair through the 5.01 frame factory / scene enqueue / gameplay entry,
  captures of consecutive presents show the expected horizontal eye offset, and the run ends
  normally. Scene-descriptor copy on 5.01: 0xFD20 bytes readable (the factory reads up to about
  +0xF714), so the 0x10000 copy size with zero-padded tail holds.
- Initially only ~25 presents/s with ~100 timed-out pairs per run. The new render-thread stack
  sampler (`[debug] thread_stack_sampler=1`, report through the test-mode `flight_dump` action)
  showed ~60% of the render thread inside DXC: the real-smoke world-up GS was recompiled on
  every call because its PSO creation kept failing. With a retry limit: ~90 presents/s.
- Cause of the PSO failure: the 5.01 root signature binds VS b0-b4 as VERTEX-only root CBVs and
  PS b0-b4 as PIXEL-only root CBVs, has no GS parameters and sets
  `DENY_GEOMETRY_SHADER_ROOT_ACCESS` (flags 0x1D). 4.04 has per-stage CBV tables including one
  for the GS (the smoke route copies VS table 3 into GS table 6). On 5.01 the mod now appends a
  GEOMETRY root CBV b1 to root signatures with that layout, drops the GS deny flag and binds the
  appended parameter to the VS b1 address at the smoke draw. The root signatures are created
  before the swapchain on 5.01, so the CreateRootSignature hook is installed from a
  D3D12CreateDevice hook on that build. Result: all three smoke projection PSOs are created,
  ~6 timed-out pairs per run (startup), Present pacing ~11 ms.
- Same as 4.04 (no port issue): the cinema detector's alternating "gameplay authority
  lost/confirmed" lines and the dual-render auto-start reason `automatic_full_vr`.
- Open on 5.01: the foliage native snapshot (V1521) falls back on every draw
  (`reason=graphics_state`): it expects the 4.04 VS CBV table with b0 and b12; on 5.01 b0 is a
  root CBV (parameter 5) and b12 sits in the b5-b15 table (parameter 17). Needs its own 5.01
  binding path.
- Not yet run with these changes: 4.04 regression (the 4.04 paths are unchanged by design:
  early hook and root-signature extension are 5.01-only, the smoke retry limit only matters when
  creation fails).

### Further 5.01 ports (verified with the null HMD unless noted)
- Frame builder (5.01 0x01C1A210, called from the gameplay entry, return 0x01D0C5E1) and the frame
  offsets its helpers read, all in the address table: secondary view core +0x600, centre/extent
  blocks +0x4E0/+0xAC0 (+8), input extent +0xBEC, camera transform +0x630. The builder reads the
  input extent at +0xBEC (4.04 +0xA4C) and passes frame+0x10 to the temporal writer, which writes
  +0x4D0/+0xAB0 relative to it. Enables the cinema/Full VR final-frame camera; not exercised by the
  smoke route (no cutscene). The native asymmetric route stays 4.04-only (temporal writer hook).
- `CCustomCamera.EnableManualControl` (5.01 0x01E0D800): the setter stores the flag at +0x269
  (4.04 +0x259) and copies the float pairs at +0x258/+0x260 (4.04 +0x248/+0x250). The camera
  authority snapshot reads the director's top camera with the same table offset, but that pointer
  carries the +0x170 subobject vtable, so its `top_manual` value is diagnostic only.
- `GetCameraDirection` (5.01 0x023826F0) through `src/prefix_hook.h`: relocates the verified 20-byte
  prefix (inc [rdx+0x30]; vmovss x2) that MinHook cannot decode. Called in gameplay, returns a unit
  direction.
- Viewport resolution (5.01 0x0228F3B0, same signature and logic; the DXGI desktop-size part moved
  into 0x0228E230): `render_width/height` now set the game's render size on 5.01.
- HUD composite: runtime pair VS 2668F02C9B6C7FDD / PS 0816B1C02F3BAF8B (the offline matcher had
  picked another VS). The 5.01 PS adds a colour-blindness filter on the scene colour
  (CustomPixelConsts row 17: mode .x, strength .w, active when .w > 0), otherwise it is the 4.04
  composite the replacement HLSL mirrors. Verified: per-eye HUD variants are created and the HUD
  shifts by ±36 px between the eyes.
- 5.01 anti-aliasing options (`user_config_matrix/pc/graphics.xml`): `AAMode` 0 off, 1 FXAA,
  5 FSR, 6 DLSS, 7 XeSS. VR baseline on 5.01: FXAA.
- The HUD frame scale is written to `dx12user.settings` only inside `witcher3.exe` (ctest no
  longer touches the user's settings).
- VDXR with the user's Virtual Desktop settings reports symmetric eye FOVs (centres 0.5/0.5), so the
  first headset run can use `native_stereo=0` without losing the optical centre.
- 4.04 regression of these changes (commit `33ae4d2`): flat stereo smoke run passed with the same
  hook set as the previous regression run; a null-HMD OpenXR run with the normal 4.04 VR
  configuration (TAAU, `native_stereo=1`, 1748x1468) passed as well: dual render, smoke projection
  PSOs and per-eye HUD PSOs created, 3 timed-out pairs, Present pacing ~11 ms. The 5.01-only
  paths (early CreateRootSignature hook, root-signature extension) stay inactive on 4.04.

### First headset run on 5.01 (Pico 4, Virtual Desktop) and fixes
- Run 1 (`33ae4d2`): session FOCUSED, but the headset stayed black. At startup 5.01 briefly
  presents an R10G10B10A2 backbuffer; the menu/panel projection path copied it into the RGBA8
  swapchain, the D3D12 debug layer rejects that copy, the XR command list failed to close and was
  never usable again, so no later frame carried a layer. The earlier null-HMD runs had the same
  failure (layer `none` after present 3). Fixed in `dd02d89` (format-family guard on the direct
  copy, recreate the list after a failed close; `[debug] d3d12_debug_layer=1` logs the debug
  layer's errors).
- Then both headset eyes flickered in gameplay (steady in menus and on the map), confirmed in the
  null-HMD headset view on 5.01 and absent on 4.04. Cause: `temporal_backend=taau` made the packed
  pair promotion wait for TAAU submissions that 5.01 never produces, so both eyes were fed from the
  alternating backbuffer. Fixed in `4ad8fbd` (5.01 forces the No-AA route). Null-HMD view: steady,
  one image per eye.
- The menu panel appears upside down in the null-HMD view only (180° roll of the null driver).
- Run 2 (`4ad8fbd`, Pico 4 / Virtual Desktop): image in both eyes, intro, menus and loading
  screens as on 4.04, gameplay smooth, but no usable depth; the user suspected swapped eyes.
  Confirmed without the headset: the test mode now also saves the two images submitted to OpenXR
  (`xr_eye0/1` captures). On 4.04 near objects shift left in the right-eye image; on 5.01 they
  shifted right. The built frames had the correct cameras (right eye +IPD along the right
  vector), so the swap happened when the presented backbuffer was tagged with its eye: 5.01
  records the backbuffer PRESENT barrier on the render worker inside the gameplay frame task,
  before that task's completion tag is queued, so the capture took the previous task's tag
  (the other eye). 4.04 records it afterwards on another thread. Fixed in `c892871`: a PRESENT
  inside the running frame task uses that task's own tag. Null-HMD captures now show the 4.04
  parallax direction. Headset check pending.

### First playable VR on 5.01 (headset run 3, `c892871`)
- Pico 4 over Virtual Desktop (VDXR), symmetric projection (`native_stereo=0`), 1748x1468 per eye,
  FXAA, No-AA route. Third-person gameplay in Kaer Morhen, indoors and on the balcony.
- Result (user): real 3D depth, correct in both eyes (checked with one eye closed at a time),
  intro, menus, loading screens and menu panel as on 4.04, performance subjectively close to 4.04,
  fire/smoke, HUD and trees look right. Log: every gameplay projection submits the packed pair
  (one image per eye), no timed-out pairs, Present pacing ~7.4 ms.
- Open:
  - Distant water (river seen from the balcony) slowly alternates between dark and light (about
    once per second, also with camera movement), in both eyes at the same time as far as the user
    can tell. So probably not an eye-alternation artefact; candidates are a reflection/water shader
    or exposure effect, or a mod hook acting on it on 5.01. Not yet checked flat (without the mod)
    or on 4.04 at the same spot.
  - Colour saturation in VR felt lower than on 4.04 (subjective). The mod passes colours through
    unchanged on both builds (monitor and submitted eye image of the same frame have equal mean
    saturation). 2026-10-14, balcony save made on 4.04 (5.01 loads it via Continue), null-HMD and
    flat captures of the same spot, mean saturation / value: 5.01 flat mono 0.244 / 0.395, 5.01 flat
    stereo (alternating eyes) 0.243 / 0.395, **5.01 OpenXR 0.150 / 0.525 (hazy, washed out)**, 4.04
    OpenXR 0.213 / 0.448. So the haze is specific to the 5.01 OpenXR path, not the game itself and
    not the eye alternation. Narrowed down further (same spot, mean saturation / value): flat with
    the forced 1748x1468 render size 0.247 / 0.407 (vivid), OpenXR without HMD camera
    (`hmd_freelook=0`) 0.148 / 0.482, OpenXR mono without dual render 0.154 / 0.494, OpenXR with the
    game's own projection (`hmd_compositor_only=1`, view parameters identical to flat) 0.157 / 0.462.
    So render size, HMD camera, dual render, eye alternation and projection are ruled out, and so is
    the mod's replacement HUD composite (VR with `[debug] disable_hud_composite_replacement=1`:
    0.152 / 0.491). Indoors there is no difference (flat 0.742 vs VR 0.732). **Cause: the 5.01 light
    shafts ("God rays", `[PostProcess] AllowShafts`, on by default in both builds).** With the sun in
    view 5.01 lays a bright veil over the whole image, also flat without OpenXR (camera tilted
    towards the sun: 0.186 / 0.509 vs 0.273 / 0.351 looking down). The wider VR field of view keeps
    the sun or a large part of the sky in view, so the veil is almost always there. With
    `AllowShafts=false`: flat towards the sun 0.266 / 0.443, OpenXR balcony 0.269 / 0.465 (vivid,
    more saturated than 4.04 OpenXR at 0.213). Not a mod defect; the 5.01 VR settings profile
    switches light shafts off (to be applied by the build-aware launcher later). The 5.01
    settings add `[PostProcess] WaterFogPerPixel=true` (also of interest for the water flicker). The water flicker does not occur on 4.04 at the same spot.
  - Aiming (crossbow) untested; cinema camera and cutscenes not yet checked in the headset.
  - Not ported: foliage native snapshot (V1521), native asymmetric projection, TAAU/DLSS/RT
    backends.

### Rough VR performance comparison (2026-10-14)
- SteamVR null driver at 240 Hz (so `xrWaitFrame` does not cap), same balcony save on both builds,
  15 s standing plus 8 s slow camera turn, no captures, identical diagnostic settings, AMD RX 7900
  XTX class GPU. One run each, so only the direction is meaningful.
- Mean time per Present in gameplay: 5.01 (FXAA, No-AA route) ~8.4 ms at 1748x1468 and ~8.8 ms
  at 2448x2448 per eye; 4.04 ~15 ms in all three variants (TAAU at both sizes, and FXAA with
  `temporal_backend=none` at 1748x1468). SteamVR app CPU/GPU time: 5.01 ~10.5/4 ms, 4.04
  ~11.7/3.3 ms.
- 5.01 is not slower in VR; on this machine it delivers about 1.8x the frame rate. Both builds
  are CPU-bound here: 2.3x the pixels per eye barely changes the frame time, so a GPU-limited
  comparison (weaker GPU) is still open.
- The mod's diagnostics are expensive: the same 5.01 run (1748x1468) with all `[debug]` switches
  off (`logging_enabled`, `runtime_diagnostics`, both flight recorders, the TAAU-drop, cinema,
  first-person and world-marker diagnostics) cuts SteamVR's app CPU time from 10.4 ms to 6.0 ms
  per frame and GPU time from 3.9 ms to 2.5 ms (about 40 % less CPU). The numbers above were all
  measured with diagnostics on, so they overstate the cost for normal play. Turn diagnostics off
  for playing and for performance judgements; without them the mod writes no log, so smoke
  tests need them on (`-IniSet 'debug.logging_enabled=1,debug.runtime_diagnostics=1'`).

## 2026-10-15

### Headset play session at 90 Hz (diagnostics off)
- Pico 4 over Virtual Desktop at 90 Hz, same FOV and render size as before, all `[debug]`
  diagnostics off (no log).
- Cutscenes work in the headset, including the cinema camera position.
- Combat (combat tutorial, first ghoul fight) works well. Horse and crossbow not yet tested.
- New findings (details in `remaster-open-issues.md`): background trees in the first Ciri cutscene
  follow the head; the distant river switches between blue and white with the head pose (also per
  eye, light shafts off); one kind of grass is always almost black (courtyard and elsewhere); the
  background-tree problem also appeared in a second cutscene. Overall: nice and playable at 90 Hz.

## 2026-10-16/17

### Black grass: AMD driver, not the port
- One kind of grass rendered solid black everywhere outdoors. Reproduced without a headset with an
  outdoor save, both with OpenXR (null driver) and flat without OpenXR. Not changed by any of: ray
  tracing on, the texture keys copied from the 4.04 profile removed, grass/SpeedTree/foliage values
  at their 5.x defaults, FSR instead of FXAA, the game's native 2560x1440 without the forced render
  size (where the mod no longer changes rendering).
- Community reports describe the same black grass on Radeon GPUs since the 5.0 update. The game's
  launcher also asks for a newer driver. The automatic AMD update offered only Adrenalin 26.8.1;
  26.9.2 WHQL (full download from the AMD website) fixes it: grass correct flat and in the OpenXR
  eye images with the same save and script.
- AMD users need Adrenalin 26.9.2 or newer for 5.x.

### Test action for head motion
- New script action `hmd <yaw> <pitch>` turns the located OpenXR head pose, so head-motion bugs can
  be reproduced with the static null HMD (verified: 30 degrees yaw and -25 degrees pitch turn the
  submitted eye images as expected).

### Cutscene background following the head: fixed (`8d94088`)
- In some cutscenes trees and sky stayed fixed in the image while the head turned (reported from
  the headset on 2026-10-15). Reproduced without a headset with a 5.01 checkpoint before an evening
  cutscene and image pairs at 0 and 20 degrees synthetic yaw: characters and ground shifted, trees
  and sky did not move by a pixel.
- Cause: these cutscenes reuse the native camera stored in the frame data, so the mod's Full VR
  fallback (`prepare_full_vr_frame_camera`) writes the HMD-corrected camera into the frame
  itself. It corrected only the primary view at frame+0x20; the frame carries a second camera
  copy right after it (`FrameSecondaryViewOffset`, 0x600 on 5.01, 0x530 on 4.04), and sky and
  trees read that one.
- Fix: mirror the corrected camera into the secondary view for the frame build and restore it
  afterwards, like the primary. Verified with the null HMD (whole image turns together) and in
  the headset (Pico 4, Virtual Desktop): background no longer attached to the head.
- Found in the same scene: a cutscene camera light that follows the head; fixed in `64b7aad`
  (camera lights placed with the game camera's yaw, see open issues).

### Both cutscene fixes also apply to 4.04 (2026-10-17)
- A 4.04 checkpoint before the same evening cutscene (made flat on 4.04) loads on 4.04 and on 5.01.
  On 4.04 the cutscene also takes the Full VR reused-camera fallback.
- 4.04 without the fixes (build `bbb0231`, No-AA route, null HMD, 20 degrees synthetic yaw):
  characters and ground turn, trees and sky stay fixed in the image, the same as 5.01 before the
  fix. So upstream 4.04 has the background problem too.
- 4.04 with the fixes (`7cbef19`): the whole image turns together; the secondary-view mirror runs
  (offset 0x530, 1296 bytes); the camera light hook at 0x718310 is active and replaces the
  composed yaw (129 degrees) with the game camera's (109 degrees). The TAAU route runs through the
  scene without errors (its desktop captures do not show the HMD view, so only stability was
  checked there).
- Open: headset check on 4.04.

## 2026-10-18/19: Linux/Proton and further 5.01 work (this repository)

Added by [witcher3-remastered-vr](https://github.com/kaarele/witcher3-remastered-vr). Test setup:
Linux with Steam, Proton and vkd3d-proton, NVIDIA RTX 3080, Meta Quest 3 through WiVRn at 90 Hz,
2476x2594 per eye. "Null HMD" below means the headless runtime in `tools/w3vr-nullxr` (next
section). Status lines say whether a change was tested in the headset or only verified with the
null OpenXR runtime.

### Headset-free OpenXR runs under Proton (w3vr null HMD)
- The SteamVR null driver route does not fit Proton: the Windows OpenXR loader ignores
  `XR_RUNTIME_JSON` under Wine (elevated token) and always takes Proton's wineopenxr. Instead:
  `tools/w3vr-nullxr`, a minimal D3D12 OpenXR runtime that exports every `xr*` entry point, plus
  `[test] openxr_loader=<dll>` so the mod loads it in place of `openxr_loader.dll`. Quest 3 FOV
  and recommended size, paced `xrWaitFrame`, BMP captures of the submitted projection/quad layers
  (post-HUD composite). Setup in `linux/BUILD-LINUX.md`.

### DLSS SR on 5.01 under Proton
- Proton blocks it before the mod does anything: 5.01 skips its Streamline setup when
  `ntdll!wine_get_version` exists. The DLL now NOPs that jump in memory at load (exact 5.01 build,
  18 context bytes checked), on by default with `temporal_backend=dlss`.
- Flat (no OpenXR): Streamline 2, `_nvngx` and `nvngx_dlss` load, no Streamline errors, DLSS image.
- Strict Stereo: the game evaluates DLSS once per eye frame through one state and viewport, so one
  history mixed both eyes. Fix: hooks on the constants builder, input prepare and SR apply give
  each eye its own Streamline viewport (engine id, +0x100 for the right eye) and their own copy of
  the state's frame guards (both eyes of a pair share `frame_data+0xC44`, so the second eye's
  constants were otherwise skipped). The per-eye motion-vector camera record (V14003) is ported
  (builder 0x02291600, view+0x530). Null HMD: every pair gets constants/prepare/apply for both
  viewports, 0 Streamline errors, both eyes clean and consistent (no blending), same frame rate
  as FXAA (CPU bound).
- Status: tested in the headset (DLSS Quality to DLAA).

### HUD and menu placement / sharpness
- The projection HUD shader sampled the 2476x2592 HUD with one nearest `Load`; at `hud_size` 0.5
  it dropped 3 of 4 texels (broken glyphs that crawl with the head). Now a premultiplied box of
  bilinear taps over the pixel footprint (`hud_filtered_sampling`, default on).
- The controller-locked HUD plane used the full head orientation captured at recenter, which can
  include several degrees of pitch. Now heading only (`hud_anchor_level`).
- `hud_size` < 1 shrank the HUD toward the straight-ahead axis, which on Quest 3 is 40% from the top
  of the visible image (FOV +44/-55). Now toward the visible-image centre (`hud_scale_origin`), the
  same at `hud_size` 1. Null HMD at 0.5: minimap about +10 degrees, hints about -21 degrees.
- Menus and loading screens are centred on the visible-image centre (`menu_vertical_center`; -13
  degrees on Quest 3).
- Status: tested in the headset.

### World markers on 5.01
- Interaction prompts and NPC icons sat too high and moved with the camera pitch: the
  `CameraDirector.WorldVectorToViewRatio` hook that corrects them was 4.04-only, so 5.01 used the
  game's pitched 60-degree camera, drawn into a HUD that the mod shows on a level plane.
- The native is ported (address table entry, 32-byte 5.01 signature; at installation its decoder
  table, out-parameter global and helper constants must match the table values, else the hook
  stays off). The local projection used for manual cinema and the first-person reticle reads the
  same values and skips empty decoder entries like 5.01's native.
- `[openxr] world_marker_exact_projection` (default 1, 5.01 only): with the controller-locked
  level HUD in Stereo, each pair publishes a marker matrix that maps a world point to the HUD
  texel in its direction from the cyclopean head: the render camera without HMD rotation or eye
  offset (level, game yaw) and an off-axis frustum equal to the plane's stage window. Two clip
  columns change: x by 1/(s kx), y by 1/(s ky) plus an origin term (s = `hud_size`, origin =
  the composite's visible-centre origin, kx/ky = the Scaleform stage band of the HUD texture from
  hud.ws `UpdateScaleformStageSize` and letterboxing). Quest 3, 2476x2594, s 0.5: x 2.000,
  y 3.725, +0.290 clip w. No tuned values; the five `world_marker_*` gains stay for the other
  routes (head-locked HUD, `hud_anchor_level=0`) and for 4.04. 0 leaves the native untouched.
- `[openxr] hud_centre_split_aim_only` (default 1, 5.01 only): the centre square of a reduced
  HUD is redrawn about the render centre only within 4 presents of an AimProbe crosshair draw;
  outside aiming it lifted markers crossing it by 6.6 degrees. On 5.01 the AimProbe signal also
  fires for about 160 presents at the start of gameplay without aiming (`HUD centre split`
  lines).
- `[debug] world_marker_diagnostics=1` (with logging): one `World marker exact` line every 30
  presents (400 at most) with the world point, the returned ratio, the vanilla ratio, the
  expected plane tangent (from the marker camera) and the plane tangent of the ratio through the
  composite geometry (`error_deg` should stay below 0.01); `World marker exact mapping` when the
  mapping changes; `HUD centre split active=` on changes.
- Unit test: `renderer_eye_geometry` runs world points through the mapped matrix, the script
  stage and `build_cyclopean_hud_plane_clip_positions` (fixed and head-locked planes, 16:9 band,
  4:3 and 21:9 stages).
- Status: verified with the null OpenXR runtime; headset confirmation pending (the exact mapping is
  active with the values above). The aim-only centre split is not tested yet.

### Near view "zooming in and out" on 5.01
- Camera trace (`[debug] camera_offset_trace=1`, null HMD, scripted panning): the near offset was
  applied on every factory camera (no flips), and the horse camera's orbit radius stayed constant
  during 360-degree pans. On foot the 5.01 Modern exploration camera snaps up to 1.3 m backwards
  and 1.5 m up within 2-3 frames while walking and glides back; with the game pitch locked for VR
  only that translation is visible. The Close preset: no such snaps (0 vs 16 in the same script).
- Changes: near view moves along the level heading when the game pitch is locked
  (`close_camera_level`); the launcher and the Linux tool set the Close exploration/combat cameras.

### Geralt centred in the near view; gameplay recenter kept across cutscenes
- Geralt sat left of centre because the Close camera is an over-the-shoulder camera (scripts:
  0.74 m right, 1.88 m behind, about 21.5 degrees off-axis). The near view (`camera_mode=1`) only
  moved the camera along the heading, so it never removed the lateral offset.
- `[engine] camera_center=1.0` (strength 0..1, 0 = the old near view): once per stereo pair (per
  Present under AER) the player position (player+0xA0; the 5.01 CNode.GetWorldPosition native at
  0x142267E50 reads it there, and its bytes are checked once, otherwise centring stays off) is
  measured against the game camera along the level right and forward vectors, and the camera
  moves along the level right vector by strength x lateral offset, the same amount for both eyes.
  It is filtered with `camera_center_smoothing_seconds=0.30` and resets to the measured value on
  the first gameplay camera after a cutscene, dialogue, loading screen or F10 panel, after a gap
  over 0.25 s, and on jumps over 1 m. A measurement with the player not 1.2-8 m in front of the
  camera, more than 5 m above or below it, or more than `camera_center_max_m=1.5` to the side
  targets 0 (the game's shoulder camera). While aiming (crossbow, bombs: the aim camera sits
  0.1-0.9 m behind Geralt, or the AimProbe crosshair is live) the camera glides back onto the game
  camera, because the game aims from its own unshifted camera. The near-view distance
  (`close_camera_offset`) is unchanged. No game collision for the shifted camera.
- F8 still toggles 0/1; the log names are `vanilla_shoulder` and `vr_centred` (`near` when
  `camera_center=0`).
- Cutscenes: on cinema entry the mod arms an automatic HMD recenter, which the first accepted
  stereo pair turns into an F9-style recenter (head yaw, the full orientation, head position and
  the centre views). Nothing restored the gameplay centre when the cutscene ended, so gameplay
  continued with the head direction of the cutscene's first frames as its forward axis. With
  `[openxr] cinema_restore_gameplay_recenter=1` (default) that recenter first saves the centre
  gameplay used, and the cinema exit restores it. Only a centre a gameplay camera has rendered
  with is saved, and never the session's first centre (taken when OpenXR started, often in the
  menu), so the recenter that starts gameplay after a load behaves as before, whether it comes from
  a post-load cinema or from an arm left over from a cinema without stereo pairs. F9, or any other
  recenter during the cutscene, drops the saved centre.
- Logs (`[debug] logging_enabled=1 runtime_diagnostics=1`): `HMD auto-recenter armed by first
  packed pair=... save_gameplay=1`, `HMD cinema gameplay centre saved/restored/discarded/not saved`,
  `Camera centre player position verified`, `Camera centre reset reason=first|epoch|gap|jump`.
  With `camera_offset_trace=1` each CAMTRACE line adds `player=`, `meas=`, `lat=` (before), `fwd=`,
  `shift=`, `lat_after=`, `centre=`, `epoch=`, `auth=`, `top=`, `ncut=`, `nscene=`, `cine=`
  (`linux/tools/camtrace.py` summarises them).
- Status: centring verified with the null OpenXR runtime (without it Geralt sits about 22 degrees
  left; with `camera_center=1` the median is 0 degrees, with the same shift in both eyes); headset
  confirmation pending. The cutscene recenter restore and the release while aiming are not tested
  yet.

### Per-eye asymmetric projection on 5.01 (`remaster_asymmetric_ndc`)
- Without this option 5.01 renders each eye with a symmetric envelope (+-54 x +-56 degrees on
  Quest 3) and submits a 1993x2089 crop of the 2476x2594 image, so about 35% of the shaded pixels
  are never shown and the visible area has 0.805x the pixel density of 4.04, which renders each eye
  with its own asymmetric frustum (`native_stereo=1`). That 4.04 path writes the optical centre into
  the view's centre block, which on 5.01 is owned by the temporal writer as the DLSS jitter.
- 5.01's ViewRebuild has a separate NDC projection offset (view+0x520/+0x524; translation P*T applied
  to the projection and the previous projection after the centre/jitter translation; nothing in the
  game writes it). `[engine] remaster_asymmetric_ndc=1` sets each factory eye view to its own FOV
  span and writes that eye's optical centre there; the full image is submitted with the eye's real
  FOV through the existing native-asymmetric pair ledger. The centre block (jitter) is untouched.
- The engine's current-to-previous clip reprojection (view+0x400, read by the DLSS input prepare and
  other temporal passes) is built from the projections before any translation, so it is conjugated
  with the offset after every rebuild (`remaster_asymmetric_ndc_reproject`, default on). The shadow
  cascade frustum helper (0x02291CA0) ignores the offset, so it is folded into its private centre
  block during the call (`remaster_asymmetric_ndc_cascade_fix`, default on).
- Null HMD: every pair verified for both eyes (P[2][0], P[2][1] equal offset + jitter), 0 rejected
  pairs; the headset view lines up with the old crop within 0.2 px in every image region; HUD and
  shadows at the outer and bottom edges unchanged; 6-11% sharper at the new density; scripted head
  turns (40 deg/s) smear less with the reprojection fix than without.
- Per-eye frusta need each eye's temporal history to be its own (the previous projection carries
  the current eye's offset): they are used while the mod's per-eye DLSS runs, or when no temporal AA
  runs at all (no jitter for 240 presents, e.g. FXAA). The game's own TAA/FSR/XeSS alternate one
  history between the eyes and keep the envelope. The decision is latched per stereo pair, and a
  change restarts both per-eye DLSS histories.
- The baked bootstrap HUD (shown until the retained HUD pair exists) uses the optical-centre
  variants for eyes rendered with a per-eye frustum; without a runtime visibility mask (WiVRn
  returns an empty one) those variants are derived from the eye FOVs.
- After 90 distinct consecutive rejected pairs the option switches itself off for the session (log
  `RANDC disabled`). Menus, cinema panels, loading screens and fallback cameras keep the envelope.
- Status: tested in the headset (with per-eye DLSS). The headset setup uses
  `remaster_asymmetric_ndc_cascade_fix=0`, see the next section.

### Sun shadow cascades once per stereo pair (`[engine] remaster_shadow_pair_reuse`)
- In strict Stereo both eyes run the whole sun-shadow job inside the frame builder, one after the
  other on the render thread: cascade fit and caster cull (0x01BF39B0), SpeedTree cascade views
  (0x01CEBC30) and the cascade depth passes (0x01D1EBF0, called from RenderShadows 0x01C34E90).
  They overwrite the same persistent state: the cascade set (collector+0x100), the SpeedTree
  cascade frusta ([collector+0x4EE8]+0x1C0) and one D16 shadow map array. The lighting rebuilds its
  cascade constants from the set on every render, so the same set gives the same constants.
- `remaster_shadow_pair_reuse=1`: the first exact render of a pair (eye/pair/generation tag from
  the frame-data registry, either eye) runs everything and is the seed. The other eye skips the
  fit/cull and the depth passes, so its lighting samples the seed's map with the seed's constants.
- The frame builder's end-of-render cleanup (0x01BF7F10, after the recording step) zeroes the
  set header (set+0 = collector, set+0x8 = cascade count) and the sixteen draw-list counts after
  every render. The second render's SpeedTree cascade views and its lighting constants
  (0x01C704B0) read the count, so the seed keeps the header and a skipped fit writes it back. The
  draw lists stay empty; only the depth passes read them.
- The skip needs: a hash of the set's persistent fit bytes for the seed's cascade count (caster
  frusta, each cascade's views and texel constants, parity; about 19 KB) equal to the one taken
  after the seed's fit; a header that is cleared or still the seed's; the same frame shadow inputs
  (far plane, sun direction, splits, bias and per-cascade parameters, shadow flags) and SpeedTree
  object; and no depth passes since the seed's (a counter of depth-pass calls).
- Fit skip and depth-pass skip are one decision per render: after a skipped fit the depth passes
  always return the way the engine's own early exit does (nothing recorded: no clear, no barrier,
  no draw), with no further check and no fallback; after a fit that ran they always draw. The
  render's SpeedTree gather job is joined by the cleanup, as on that early exit.
- Gate: Mode 3 dual render, no menu, cinema or loading video. After 8 mismatching pairs in a row,
  or more than 32 in 256 attempts, the option switches itself off for the session (log `Shadow
  pair reuse disabled`).
- `=9` is measurement only: it skips the depth passes but keeps each eye's own fit, so the second
  eye's shadows are misregistered by up to the IPD. It separates the depth-pass saving from the
  fit/cull saving.
- Logs: the install line `Shadow pair reuse requested=... mode=... code fit=... render=...
  cleanup=... hooks fit=... render=...`; per window `Shadow pair reuse mode=...
  seeded=+w/total fit_skipped=... render_skipped=... frusta_restored=... fallbacks:
  <reason>=+w/total` (census log in a measurement run, otherwise every 1200 Presents with
  diagnostics on). Fallback reasons: gate, no_tag, same_eye, seed_not_rendered, busy,
  foreign_render (another render drew the map or replaced the slot), and the mismatches that feed
  the fail-safe: thread, collector, cascades (set header neither cleared nor the seed's), frame,
  set_hash, scene_object, read_fault.
- `[perf] shadow_profile=1` (needs the VRS census log) times the four stages per eye with QPC
  and logs per census `PERF shadow eye=left|right|untagged` (ms per call of each stage, render
  calls per Present, skipped fits/renders, mean cascade count and parity, share of renders with
  sun shadows on, draws per depth-pass call, the last set hash, shadow map size and handle),
  `PERF shadow set` (fits that followed the other eye of the same pair, how many found the set's
  fit bytes as that eye left them (`untouched`; the cleared header aside) or produced the same set
  (`same_fit`), shadow map handle changes,
  all draws per Present and the depth passes' share) and once `PERF shadow frame` (split count,
  split distances, per-cascade parameters, far plane).
- With `=1` both eyes use the seed eye's fit (the engine's sphere from three corners of that eye's
  frustum). With `remaster_asymmetric_ndc_cascade_fix=0` both eyes' fits have the same shape, so the
  other eye only adds the eye separation to a single eye's coverage; with `cascade_fix=1` the seed's
  fit follows the seed eye's asymmetric frustum and may cover the other eye's outer side less. Not
  done yet: Stage 2 (a pair-locked fit, the minimal sphere around both eyes' frustum corners) and
  Stage 3 (skipping the reuser's SpeedTree cascade gathers).
- Status: verified with the null OpenXR runtime; headset confirmation pending. In a village scene
  (DLSS Quality, run-ahead on) every pair was seeded and skipped on the other eye, with no mismatch
  fallbacks; both eyes' sun shadows matched in captures, with no coverage loss visible at the
  reusing eye's outer edge; about 1.4% more Presents/s.

### Local lights per eye on 5.01 (`remaster_local_*`)
- Symptom: inside and near buildings a light can look blocked for one eye but not the other.
  Every engine render (one per eye in strict Stereo) picks its own shadowed local lights and their
  atlas tiles from its own camera: one 2048x2048 dynamic slice, tile size halved per distance step
  (512 up to 1.2 light radii), all lights halved and packed once more when the packing fails, and
  faces that still get no tile cast no shadow (light through walls in that eye). The legacy order
  packs the smallest lights first. Flickering lights never use the static cube cache, and their
  flicker advances once per render, i.e. at twice the speed and one step apart between the eyes.
- `[engine] remaster_local_light_diag=1` (no behaviour change): per render `LL render` (pair, eye,
  render order, light and shadowed counts, packing attempts (2 = overflow), tile failures per
  attempt, static cube misses, tile-size histogram, flicker calls), a few `LL light` tables,
  `LLDIFF` when the two renders of a pair differ (lights shadowed in one eye only, different tile
  size or allocated faces), and an `LL summary` every 1800 tagged renders. Lines also reach the
  census log of a `[vrs] log=1` run.
- `remaster_local_shadow_slices=2..4` and `remaster_local_shadow_sort_corrected=1` write
  `Rendering/LocalShadows NumDynamicSlices` / `UseCorrectedLightSort` (names checked first) when the
  shadow manager is created; 4 is the most the tile packing can address. Log `LL capacity`.
- `remaster_light_flicker_per_pair=1`: a light already advanced in this pair keeps the first
  render's flicker state (lock-free table keyed by light; renders are matched through the frame
  index the engine stamps on each visited light).
- `remaster_local_light_pair_lock=1`: both renders of a pair size and fade local shadows from the
  first render's view position (the selection reads nothing else of its render). One of the
  selection's two call sites keeps volatile registers live across the call, so the detour runs
  behind a stub that preserves them.
- All off by default; both pair options stay off under AER (one eye per tick).
- Status: `remaster_light_flicker_per_pair` and `remaster_local_light_pair_lock` are verified with
  the null OpenXR runtime; headset confirmation pending (a candle-lit interior at night).

### Fixed foveated rendering, Phase 0: VRS measurement build (`[vrs] mode=3`)
- Purpose: measure the most variable rate shading can save before building per-eye foveated
  rate maps. Mode 3 shades the scene passes at one uniform coarse rate everywhere (centre
  included), so it is for measurement only. Default `mode=0` installs no detour and changes
  nothing; modes 1/2 are reserved and currently mean off.
- Classes come from each graphics PSO's render-target signature at creation (scene depth
  D32_FLOAT_S8X24, one sample): `gbuffer` = 4 RTs RGBA8, RGBA8, RGBA8, RGBA16F velocity;
  `decal` = 1 or 3 RGBA8 RTs; `scene_hdr` = RT0 RGBA16F (forward/transparent, sky, ocean, light
  volumes). Everything else (shadows, UI on D24S8, post passes without depth, HUD composite, the
  mod's own PSOs) stays at 1x1. `RSSetShadingRate` is called at SetPipelineState only when a
  list's rate changes; Reset/ClearState/Close return the record to 1x1 and the list is classified
  again on its next bind. Bundles, compute lists, the XR list and ReShade's list are never
  touched. The mod's own terrain motion replay draw runs at
  1x1; its smoke/focus-fire PSO substitutes replace one engine draw and keep that draw's rate.
- Startup logs `VRS caps` (OPTIONS6: tier, tile size, additional rates); tier below 1 turns it
  off, and 2x4/4x2/4x4 fall back to 2x2 without additional-rate support. Every 600 Presents a
  `VRS census` line gives Presents/s and per-class bind counts for the window, the list states,
  and `unknown_pso_binds` (binds of PSOs that neither creation detour saw; it should stay 0, or
  the measured ceiling is too low). The first bind of each classified PSO logs one `VRS pso` line
  (64 at most), and the first 8 unknown binds log their PSO.
- Measurement INI: `[debug] runtime_diagnostics=0` (`logging_enabled` does not matter, so the
  launcher's or a test run's diagnostics-off state works); `[vrs] mode=3`, `uniform_rate=2x2`,
  `classes=gbuffer,decal,scene_hdr`, `log=1`. The VRS lines are the only ones written to
  `witcher3vr.log` in that state.
- Baseline: the same INI with `uniform_rate=1x1`. It runs the same detours and census but never
  calls `RSSetShadingRate`, because every list record starts at 1x1. A `mode=0` run writes no
  census, so it can only be timed from outside the mod.

### Descriptor metadata store (`[engine] descriptor_metadata_store`)
- Problem (null HMD, 5.01, Proton): the `CopyDescriptors`/`CopyDescriptorsSimple` detours took
  about 15% of the engine render thread and 16% of both RenderDispatcher threads. Every copied
  descriptor did several hashed lookups (Witcher Senses cache, 1M-slot CBV table, resource
  mirror), and each CBV miss, i.e. every non-CBV source, also locked a shared fallback-map mutex.
  `[perf] skip_descriptor_tracking=1` raised CPU-bound throughput from 69 to about 100 eye
  renders/s; the render thread's job wait at 0x141D335A9 fell from about 3.2 to 1.0 ms.
- `shadow` (default): one 48-byte entry per descriptor holds every metadata kind (CBV address and
  size, retained-HUD resource and format, Witcher Senses target). Entries are ordered by CPU
  handle in lazily allocated pages of 2048 granules. The granule is the largest power of two
  dividing the CBV_SRV_UAV increment: 32 on NVIDIA and on vkd3d-proton's default path, the
  native descriptor size on its embedded/descriptor-heap paths (64 on RDNA2/Steam Deck, 128 on
  Intel). Handle bits below the granule (vkd3d-proton's per-heap tag) are part of the page key,
  so heaps whose handle windows overlap never share entries. A copy walks source and destination
  entries run by run under a per-entry seqlock, with no hashing or lock; a per-thread page
  cache resolves the pages. Waits on a writer pause, then yield the core. The startup log has
  `Descriptor metadata store=...` (in the census log during a measurement run); an increment
  that is not a multiple of 8 keeps `legacy`.
- Deliberate difference: a copy now gives the destination exactly the source's metadata, empty
  included. `legacy` kept the destination's old CBV/resource entry when the source had none.
  The shadow store also never evicts (the legacy Witcher Senses cache and the full legacy
  resource table could lose entries). The smoke lookup finds handles the legacy one could not:
  those in the fallback map, and descriptors briefly mid-write (up to 8 attempts instead of 1).
  Witcher Senses lookups wait for a writer instead of giving up after 3 attempts. All of these
  only add hits. TAAU/RT keep the `g_resource_descriptors` map path unchanged under
  `g_reverse_mutex`.
- `legacy` runs the previous code; its CBV lookup no longer takes the fallback mutex on an empty
  probe slot (slots are never cleared, so such a handle cannot be in the map).
- Memory: 96 KiB per touched page, never freed; a fully used 1M-descriptor heap is 48 MiB. Each
  census has `PERF descriptor store=... pages=... shadow_mib=...` and guard counters
  (`directory_overflows`, `alloc_failures`, `owner_collisions` (should stay 0),
  `long_waits`); a legacy run logs `PERF descriptor store=legacy`. One line warns past 4096
  pages.
- `[perf] descriptor_shadow_verify=1` (needs the VRS census log) keeps both stores and compares
  every lookup. Each census adds a `PERF descriptor verify kind=...` line per kind with
  `+window/total` counts: match, empty, legacy_only_stale (expected: legacy kept stale data
  after a copy), legacy_only (shadow lost data), shadow_only (legacy evicted or missed),
  differ, transient (a difference that did not repeat on a second look) and copy_source_lost
  (copies whose legacy source had data the shadow source lost). The first 20 examples of
  differ, shadow_only and legacy_only are logged once.
- Status: on by default; verified with the null OpenXR runtime; headset confirmation pending.

### Render-thread CPU trims (descriptor copy, aim detector, pair barrier)
- Descriptor copy, shadow store (`copy_descriptor_shadow_ranges`): both copy detours read one
  atomic route mask instead of about eight predicate chains per call. The mask holds only terms
  that depend on the INI and build constants; it is computed in `load_config` and again when the
  store falls back to legacy. The runtime-variable terms of `descriptor_metadata_hooks_needed()`
  (native-asymmetric state, cinema panel, AFW) are evaluated in full only when the configured
  terms are all off. The startup log has `Descriptor copy routes=0x...` (in the census log
  during a measurement run).
- Per copy: switches are read once per call instead of once per run. The last source and
  destination pages are kept across runs, so a gather looks its destination page up once. With
  one entry per descriptor (stride 1, NVIDIA/vkd3d-proton) the run limit needs no division.
  The run minimum no longer uses the initializer-list `std::min`.
- Per descriptor: source and destination are read header first. A source without metadata
  kinds (or held by another owner) loads no value fields. An empty copy onto an entry that
  already reads as empty is skipped: no owner stamp, lock or store. Owner bits alone are not
  observable: every consumer checks the kind bits, and under verification also the
  copied-empty markers, which count as content here. No new key: `[engine]
  descriptor_metadata_store=legacy` bypasses the shadow copy entirely, and
  `[perf] descriptor_shadow_verify=1` checks it (expected: differ 0, legacy_only 0).
- Aim detector (`sample_aim_pso_probe`, feeds `g_aim_crosshair_last_present`): the 6x1 draw
  check no longer takes `g_reverse_mutex` or runs `strstr` on the draw source. Every record into
  `g_pipeline_infos` refreshes a 4-slot lock-free set of PSOs recorded with the AimProbe VS/PS
  pair. A PSO recorded again with another pair leaves the set, so the set stays equal to the map.
  A fifth AimProbe PSO logs `Aim probe PSO set full` and the detector goes back to the locked
  map lookup. With `[debug] first_person_aim_diagnostics=1` every candidate draw is also checked
  against that lookup: `FPAIM probe set hit ... lookup=1` (first 4), `FPAIM probe set mismatch`
  (must never appear).
- Strict-Stereo pair barrier (`wait_for_engine_pair_completion`): the spin before parking is a
  pause budget, `[engine] pair_barrier_spin_pauses` (default 4096, roughly 60-130 us), instead
  of a ~200 us window that called `QueryPerformanceCounter` (a syscall under Proton) on every
  pause. `0` restores the timed spin. Measurement runs log `PERF pair_barrier_spin_pauses=...`.

### Strict-Stereo pair barrier run-ahead (`[perf] pair_barrier_lag`)
- Problem (null HMD, 5.01, Proton): after RenderSceneSetup queues both eyes of pair P, the game
  (main) thread waits in `hook_engine_render_scene_setup` until P has rendered
  (`wait_for_engine_pair_completion`). The next game tick therefore never overlaps rendering, and
  the render thread idles in the engine's command-ring wait (0x141C69AE0, ~26% of its samples)
  for a whole tick per pair.
- `pair_barrier_lag=1` (5.01 only; other builds log it and keep 0) waits for the pair queued by
  the previous tick instead, so tick P+1 runs while P renders: at most one pair of run-ahead.
  The masks and floors of a pair are still settled by the same wait, one tick later; a pair is
  never released on its first eye (that would drop its second completion, its captures and its
  0x3 mask). A tick that queues no pair (menu, loading video, retry window) waits for the
  pending pair; a pending pair from before a route reset (new capture generation) is dropped
  without waiting, because it can no longer complete. Default 0: unchanged behaviour.
- Audit of state that assumed one pair in flight:
  - The right-eye scene descriptor copy was indexed by `present % 16`; with lag 1 every pair gets
    its own slot of the 16 (a counter), since the next factory can run before the Present count
    moves.
  - Capture ring: a full ring drops the capture, it never overwrites a slot in use (fence and
    `recording_pending` checks). With lag 1 the ring has 4 slots per eye instead of 3, because
    the render thread no longer idles through the tick while the GPU still holds the oldest slot.
    The census counts dropped captures (`ring_full`).
  - Pair-frozen views, HMD pose snapshots, FOVs and frame tags travel with the frame data
    (`g_engine_dual_frame_eyes`, pruned only 256 pairs back); native-asymmetric, shadow-cascade,
    temporal-matrix and world-marker camera rings are keyed by pair (16-128 slots). The live HMD
    globals (`g_xr_views`, `g_hmd_*`) are written and read on the game thread (pair factory,
    view rebuild); render-thread hooks read the frame tag instead. Strict Stereo HUD placement
    uses the submitted (pair-frozen) views and the F9 centre. The 5.01 world-marker hook
    (`world_marker_exact_projection`) reads its camera on the game thread: at lag 1 it uses the pair
    the previous tick queued (`g_engine_pair_barrier_pending`), the same pair lag 0 reads from the
    completed signal, so the marker camera age does not depend on render-thread progress.
  - The per-eye DLSS guards (`call_remaster_dlss_per_eye`) run only on the render thread, in
    render order, which run-ahead does not change.
  - Pair pose lead: the pose was predicted 2 display periods after the last completed frame, which
    assumes no run-ahead. Every pair now remembers the completed time its pose was located from,
    and its first real publication measures how many periods passed (average over pairs). With
    `pair_barrier_lag=1` and `pair_pose_lead_adaptive=1` the rounded average (1-6) replaces the 2
    after 8 samples.
- Census (VRS census log), one line per window: `PERF pair barrier lag= waits= waits_per_present=
  ms_per_wait= ms_per_present= max_ms= long_waits= timeouts= drains= stale= masks= ring_slots=
  ring_full= pose_lead= observed_lead= lead_samples=`. `ms_per_wait` is the game thread's wait per
  pair (lag 0 expected about one pair's render time, lag 1 near 0 when the render thread keeps
  up); `masks` (completion-mask backlog) must stay small; `timeouts` and `ring_full` should stay 0.
  The pipeline flight recorder still times the wait as `EnginePairWait`.
- Status: off by default; verified with the null OpenXR runtime; headset confirmation pending. With
  lag 1 the game thread's wait per pair fell to near 0 with no timeouts; CPU-limited headless runs
  gave about 15% more frames per second.
- With lag 1 the eye rendered second showed moving characters one game tick ahead (different
  animation poses per eye). Fixed by `[perf] pair_enqueue_after_fence`, see "Same animation pose
  in both eyes with run-ahead" below. With the fix, lag 1 is used in the tested headset setup.

### Black HUD rectangle during fades (`hud_fade_mode`, 5.01)
- Symptom: a black rectangle over about 2/3 of the view for 1-2 s when a cutscene loads,
  with a small see-through area near the middle; after a loading screen a rectangle fades away.
- Cause: 5.01 draws its screen fade (`FadeOut`/`FadeIn`, blackscreens) in the GUI pass 0x01C26A60,
  after the HUD movies and into the same target, the UI texture t1: fill-rect helper 0x01C99780
  called at 0x01C26F1E (and from the standalone fade draw at 0x01C0B948), only while the fade
  amount (fade block +0x48) is above 0. Mode 3 removes t1 from the eye images and shows it as the
  shrunk HUD plane, so the full-screen fade became a HUD-sized rectangle; the see-through area is
  the part of the centre-split hole that the centre square does not cover.
- Fix: a detour on the fill-rect helper (prologue, the amount test and both call sites checked;
  5.01 only) drops the fade fill while the VR HUD plane owns t1: Mode 3 with the retained HUD
  route, not in menus, the loading video or a normal/manual (F10) Cinema panel. The HUD movies
  are drawn as before; other callers and other return addresses pass through.
  `[openxr] hud_fade_mode`:
  - `remove` (default): no fade in that state; the world stays visible during blackscreens.
  - `uniform`: same, plus each whole eye image is blended with the latched fade colour and amount
    in the XR submit, before the HUD plane (so subtitles stay readable). The latch goes stale 3
    Presents after the last fill. Without its XR pipeline it keeps the native fade.
  - `native`: the game's draw (the rectangle). Nothing is hooked unless diagnostics are on, where
    the detour only logs.
- Logs (diagnostics on): `HUD fade fill hooked`, then per fade `fade fill start|full|partial|gate`
  lines (`site=gui|object rgba=RRGGBBAA suppressed=0|1 mode=...`), one `fade fill end ... calls=
  suppressed= max_a=` summary, and `fade fill uniform drawn` (first 16) in uniform mode; also
  `HUD_FADE` in the minimal XR log.
- Status: verified with the null OpenXR runtime; headset confirmation pending. In captures `remove`
  shows the world and the HUD where `native` shows the rectangle, and every fade fill at a load-in
  was suppressed.

### Even pair cadence on a fixed-refresh headset (`[openxr] pair_rate_divisor`)

A headset panel has no VRR: every refresh shows the newest finished stereo pair (re-aimed to the
current head rotation), so a game running at 37-44 pairs/s on a 90 Hz panel shows some pairs for 2
refreshes and others for 3. Head rotation stays smooth; game motion (camera orbit, animation) steps
unevenly. Headless runs with a WiVRn-like pacer in the null runtime (`W3VR_NULLXR_PACER=wivrn`)
show why WiVRn does not lock to 45/30 here: its measured GPU time per frame (~13 ms) exceeds one
90 Hz period, so its app pacer stops sleeping the application and the cadence becomes uneven
(a village scene, DLSS Quality: ~37 unique pairs/s, holds 2:60% 3:40%). `frame_wait_early=1` did not
change that (36.7 vs 37.1).

`pair_rate_divisor=N` holds each pair to exactly N display periods on the game thread (after the
strict-Stereo barrier): 2 = 45/s at 90 Hz, 40/s at 80 Hz, 36/s at 72 Hz; 3 = 40/s at 120 Hz. It only
caps: a pair that is already late restarts the schedule instead of catching up. Pick the refresh
rate and divisor the game can hold in heavy scenes. With `[vrs] log=1` the census writes
`PERF pair rate limit divisor= period_ms= ticks= waits= ms_per_wait= resyncs= phase_shifts=`.

Measured with the WiVRn-like pacer (a village scene, run-ahead, shadow reuse), the cap is not worth
using yet, so it stays 0:

| Setup | Unique pairs/s | Pairs held exactly 2 refreshes | Judder |
|---|---|---|---|
| 90 Hz, DLSS Quality, free | 44.0 | 92-94% | 1.4 ms |
| 90 Hz, DLSS Balanced, cap 2 | 45.0 | 67-73% (78-91% with phase hunting) | 3.1 ms |
| 80 Hz, DLSS Quality, cap 2 | 40.0 | 56-85% (56-63% with phase hunting) | 3.5-3.8 ms |

The cap runs on the game thread, before the render, so the render-time jitter decides on which side
of a refresh a pair lands. Phase hunting (move the schedule half a period whenever the runtime's
predicted display time jumps by a period) keeps shifting instead of settling. Free-running just
below the refresh rate is smoother. A useful cap would have to hold the finished pair on the render
thread, just before xrWaitFrame/xrEndFrame.

### OpenXR frame loop order for strict Stereo (`[openxr] frame_wait_early`)
- Strict Stereo binds one OpenXR frame to each published pair and calls `xrWaitFrame` and
  `xrBeginFrame` inline, right before that pair's `xrEndFrame`. WiVRn's application pacer
  (`server/driver/app_pacer.cpp`) measures the app time from the `xrWaitFrame` wake-up to
  `xrEndFrame`, so it sees only those few milliseconds, takes the game for fast and sleeps each
  `xrWaitFrame` to a display slot. The pair rate then locks to whole display periods: 30 pairs/s
  at 90 Hz for a 25-28 ms pair, 40 at 120 Hz for a 24 ms pair.
- `frame_wait_early=1` (default 0) uses the standard loop order instead: right after a successful
  `xrEndFrame`, the same render thread calls `xrWaitFrame` and `xrBeginFrame` for the next pair and
  keeps the frame state. The next submit (new pair, two-Present rescue, cinema or menu layer)
  consumes it: it locates its views at that frame's predicted display time and ends it with that
  display time. A submit that finds no begun frame (first pair, after a failure or a session
  restart) waits and begins inline as before. WiVRn then measures about one pair of app time, which
  exceeds one period, and stops sleeping ("limited by app time, don't wait"): the game runs at its
  own rate and each pair is shown at the next display slot, with uneven holds (for example 2,2,3
  periods at 90 Hz for 40 pairs/s).
- Only the pair-bound strict-Stereo lifecycle arms an early frame (the predicate of the Present
  hook's `pair_bound_openxr_frame`); AER, Mono and the non-pair-bound prepare paths are unchanged.
  While an early frame is pending, `prepare_openxr_render_frame` (factory and Present-boundary
  prepares) does not wait or begin another frame, so there is no double `xrBeginFrame`. Session
  stop, loss and restart drop the pending frame (`xrEndSession` discards it).
- Log (diagnostics on): `OpenXR early frame wait configured=1 pair_bound=...`, `... armed first
  frame ...`, up to 8 `... no pending frame, inline wait ...` and `... prepare origin=... deferred
  ...` lines, and every 1024 early frames a summary with the mean and maximum `xrWaitFrame` time
  and the mean time a begun frame waited for its pair.
- Null HMD: `W3VR_NULLXR_PACER=wivrn` (`tools/w3vr-nullxr`) reproduces WiVRn's pacer, and the
  `pacing` summary lines of `w3vr_nullxr.log` show unique frames per second and the hold histogram.
  With an offline driver of the runtime (fixed 25 ms per pair, 90 Hz): inline order 30.0 pairs/s,
  all holds 3; early order 39.9 pairs/s, holds 2 and 3. Note that WiVRn also skips the sleep when the
  ideal slot is out of reach but the next one is not, so an inline pair of about 13-23 ms at 90 Hz
  already runs free.
- Status: experimental, off by default. Measured with the null OpenXR runtime only: with the
  WiVRn-like pacer it made no difference in the game (previous section). Not tried in the headset.

### Same animation pose in both eyes with run-ahead (`[perf] pair_enqueue_after_fence`)
- Symptom (run-ahead `pair_barrier_lag=1` with shadow reuse): moving characters (Geralt
  running, NPCs walking) are shaded darker in one eye and lighter in the other, mostly on the back;
  each eye alone looks fine; Geralt stutters when running.
- Null HMD with zero IPD and a symmetric FOV, both eyes compared while Geralt walks and runs past
  NPCs: with lag 0 the eyes are identical (with or without shadow reuse). With lag 1 Geralt and the
  NPCs stand in different animation poses and positions per eye (with or without shadow reuse, FXAA
  and DLAA), while the camera and the static world stay identical. Shadow reuse only turns the pose
  difference into the dark/light patches (the second eye's bodies sample the first eye's shadow
  map). 1-2 of 8 moving captures were clean, so the affected eye flips between the same tick and
  the next one: the stutter.
- Cause: 5.01 sends skinning and relink updates (pose slot, localToWorld, bbox) through the batched
  ParallelRenderCommands queue, not through the command ring: producers append (renderer vtable
  +0x300), constructing any `CRenderCommand_RenderScene` or `CRenderCommand_Fence` publishes what
  was appended (renderer vtable +0x2F8), and every frame build applies what is published at that
  moment (`ParallelRenderCommands::FlushCommands` 0x01C1CA10, called by the frame builder, so once
  per eye). RenderSceneSetup (0x0227D1F0) calls the frame factory (0x0227D97D), waits on the fence
  that the previous tick's Fence command signals after that tick's primary eye (0x0227D9F4), then
  commits the primary RenderScene and the next Fence command. The factory hook queued the duplicate
  eye (rendered first) inside the factory call, before that wait. With lag 1 tick N+1 therefore
  published its batch while pair N's second eye (the primary, left by default) was still queued, and
  that eye applied it: tick N+1 poses and positions with tick N's camera.
- Fix (`pair_enqueue_after_fence=1`, default): inside RenderSceneSetup the factory hook keeps the
  duplicate and its reference in a per-thread slot instead of queuing it. A detour on the
  CRenderFence wait (vtable +0x18, 0x01CABDE0) queues it as soon as the wait called from
  RenderSceneSetup (return 0x0227D9F7) has returned, before the primary is committed. The duplicate
  still renders first and the primary second, and both publishes of tick N+1 follow pair N's Fence
  command, as with one eye. Barrier masks, pair/eye/generation tags, shadow reuse roles, DLSS
  slots, capture rings and markers are unchanged; with lag 0 the order changes the same way (the
  wait returns almost at once there).
- Checks: the wait's code (18 bytes, unique), the factory call and the fence wait call in
  RenderSceneSetup (bytes ending at both return addresses), and per setup call that the renderer
  already has a fence (+0x1D0) whose vtable wait is the hooked one. Otherwise the duplicate is queued
  in the factory as before: other factory callers, the first setup call (it creates the fence and
  does not wait), a mismatch. A duplicate still held when RenderSceneSetup returns is queued then,
  after the primary, before any pair barrier. Each duplicate is queued and released exactly once.
- `0` restores the old order. 5.01 only; other builds keep 0.
- Census (VRS census log), one pair of lines per window:
  `PERF pair enqueue after_fence= hooked= deferred= late= replaced= factory outside= other_caller=
  no_fence= fence_class= unhooked=` (`deferred` should count one per pair, the others stay near 0)
  and, from a probe on FlushCommands that sums the staged entries (count at +0x15C, 17 buckets of
  12 bytes) before each flush, `PERF parallel flush first= first_entries= second= second_entries=
  second_nonzero= second_max= second_eye= untagged= untagged_entries=`. The role comes from the frame
  tag: the first flush of a pair id is the eye rendered first. Expected: `second_entries` about 0
  with the fix; with `pair_enqueue_after_fence=0` and lag 1, tens to hundreds per pair at the second
  eye (`second_eye=0`) while characters move. The probe reads the counts without the bucket locks.
- Status: verified. With the null OpenXR runtime (zero IPD, symmetric FOV, Geralt walking and running
  past NPCs) the largest left/right difference on moving characters fell from about 110 (fix off) to
  about 2-3 (the static floor), with FXAA and DLAA, at unchanged throughput; `second_entries` was 0.
  Confirmed in the headset: run-ahead with the fix shows no per-eye pose or shading difference.

### People outside the game camera's view keep animating (`animated_component_visible`, 5.01)
- Symptom (5.01, in a town): many people stood in a T-pose or frozen (one man's axe moved,
  his arms and body did not); they animated once Geralt got close. Raising the in-game background
  characters setting made it less noticeable.
- Cause: a 4.04 hook that was never ported. Every frame the animated-component visibility update
  (CAnimatedComponent vtable slot 67; 4.04 0x015E1750, 5.01 0x02437970, shared by the mimic,
  moving-agent and background-root components) tests the component's world bounds against the
  game camera's frustum and stores the result at +0xF74. While that byte is set, the update skips
  the skinned attachments: the body keeps its last pose (the bind pose if it was never updated),
  and only plain child transforms such as a held weapon follow. In VR the head looks around
  freely, so the headset shows people that are outside the game camera's frustum. On 4.04
  `animated_component_visible=1` (default) hooks the frustum test (0x0031E050) and, only for the
  call from that update (return 0x015E186C) and only while the HMD view is active, reports the box
  as visible. On 5.01 the installer returned early (4.04-only addresses).
- 5.01: the frustum test is 0x0099B2B0, an AVX rewrite over a structure-of-arrays frustum with
  the same arguments and result (al = 1 when the box is fully outside a plane). Its only caller
  is the same update (call 0x02437A8B, return 0x02437A90, followed by the store to +0xF74). MinHook
  cannot patch the test (VEX instruction at byte 4) and its first 13 bytes are too short for
  prefix_hook (RIP-relative instruction at byte 13), so the mod redirects that one call instead:
  its rel32 now points at a 14-byte absolute jump to the existing detour, allocated within reach
  of the call (`src/call_site_hook.h`). The test itself stays unpatched and is called as the
  original. Checks before the patch: the test's 16 leading bytes, the call bytes (their rel32
  encodes the test), the store after the call, and that the decoded destination is the verified
  test. Any mismatch leaves the game unchanged and logs why.
- Same gates as 4.04: `hmd_freelook`, a valid HMD pose, no menu (needs `menu_state_probe=1`), not
  in cinema mode, and the HMD camera route active within the last 8 Presents.
- Log: `Animated component visibility hooked build=remaster-5.01 call_site=1 ...` at startup and
  `Animated component visibility forced full count=1` the first time a component is forced.
  Census (VRS census log): `PERF anim visibility call_site= tests= culled= forced=` per window
  (`culled` = tests that found the component outside the game camera's frustum, `forced` = those
  reported visible).
- Not covered (both builds): CAsyncAnimTickManager (5.01 tick 0x0265D900, run from
  RenderSceneSetup) culls its registered entities (animated entities, background NPCs) against
  the game camera's frustum on its own and skips their tick outside it. Work freezing in
  `gameplay.ini [Work]` (UseWorkFreezing, 7 m radius for "invisible" actors) may also stop work
  animations. If people outside the game camera's view still stand still with this fix, those
  are the next candidates.
- Status: verified with the null OpenXR runtime (the redirect is installed, `forced` equals `culled`,
  no regressions) and in the headset in a town, together with `UseWorkFreezing=false` in
  `bin/config/r4game/legacy/base/gameplay.ini`: people kept animating; a frozen character was seen
  once, in a cutscene.

### Per-eye FSR history in Stereo (`[engine] remaster_fsr_per_eye`, 5.01)

- Problem: with Anti-aliasing = FSR (AAMode 5), 5.01 dispatches one upscale ffxContext for every
  render. Strict Stereo renders both eyes through it, so one FSR history alternates between the
  eyes and blends them (ghosting); the per-eye frustum gate also stays on the envelope route
  because temporal jitter runs without a per-eye history.
- Game path (Steam 5.01): the exe imports `ffxCreateContext`, `ffxConfigure`, `ffxQuery`,
  `ffxDestroyContext` and `ffxDispatch` from `amd_fidelityfx_loader_dx12.dll` (FidelityFX SDK 2.x
  API; the game ships loader 2.3.0, upscaler 4.1.1, frame generation 4.0.1). 0x01C64D20 creates
  the upscale context into FSR state +0x40 (description: `FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE`,
  flags 0x28 depth inverted + auto exposure, +1 HDR, +0x40 dynamic resolution, max render size =
  max upscale size = the output size, chained to the DX12 backend description; no allocation
  callbacks). The upscaler selection 0x01C09E73 calls the FSR dispatch 0x01C665B0 (state =
  renderer +0xD0, the same frame_data as the DLSS functions); it fills
  `FFX_API_DISPATCH_DESC_TYPE_UPSCALE` (jitter from frame_data +0x4D0, render/output sizes from
  +0xBDC..+0xBE8, near/far +0x40/+0x44, vertical FOV from +0x2C) and dispatches on `&state->ctx`
  (call 0x01C66C3D). The history reset is the byte at state +0x26, cleared after the dispatch
  unless frame generation owns it (state +0x24). Jitter comes from the context's jitter queries
  (0x10004/0x10005) in 0x01C0A9B0, indexed by the render counter.
- Fix (`remaster_fsr_per_eye=1`, off by default): the mod redirects the exe's import slots for
  `ffxCreateContext`, `ffxDestroyContext` and `ffxConfigure` (the AMD DLLs are not patched).
  When the game creates an upscale context, the mod creates a twin with the game's own
  description for the right eye; destroy and configure calls on the game's context are repeated
  on the twin. The FSR dispatch 0x01C665B0 is hooked (signature checked): for a right-eye frame
  (eye from the dual-render frame tag) the state's context field holds the twin for the duration
  of the call, so the game's own code dispatches it. Resets follow the per-eye DLSS rules: a new
  or long-interrupted eye restarts (frame index gap > 4), a reset the game asks for on the first
  eye of a pair is repeated on the second (same frame index or the next), and a per-eye frustum
  change restarts both. An untagged dual-render frame runs on the game's context with a reset and
  restarts both eyes afterwards. With the key on, per-eye FSR also enables the per-eye
  motion-vector camera record (`remaster_dlss_per_eye_motion`) and counts as a per-eye history for
  the per-eye frustum gate.
- Nothing depends on the FSR version: the twin is created through the same loader, so it gets the
  provider the game's context gets (FSR 2/3.1 on most GPUs, FSR 4.x where the driver offers it).
  The log names both providers and the twin's VRAM at creation:
  `FSR per-eye: game upscale context ... right-eye twin ... vram=... MB`, then
  `FSR per-eye: first left-eye dispatch` / `first right-eye dispatch`. With
  `runtime_diagnostics=1`, `FSR per-eye dispatch eye= slot= frame_index= reset= calls=` lines.
- Not covered: FSR frame generation (off in VR; disabled by the game under Wine); the jitter
  sequence is still indexed by the render counter, so each eye sees every other sample (as with
  DLSS). XeSS keeps one history for both eyes.
- Status: built, not yet run in the game.
