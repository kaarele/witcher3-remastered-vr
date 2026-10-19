# Porting analysis: 4.04 → Remaster

Status: initial static analysis of upstream v0.9.8v2 (commit `79293dd`). Everything here is
derived from this repository's public source. Runtime findings belong in `local/`.
Line numbers refer to `src/dxgi_proxy.cpp` at that commit and will drift. Search by name.

## Key finding

- No game-build check exists. `current_process_is_witcher3()` only compares the exe file name.
- Game-internal hooks are `GetModuleHandleW(nullptr) + RVA` patched by MinHook without
  verifying the target bytes. The only exception is the native head-pose hook pair
  (`GetHeadBoneIndex` / `GetBoneWorldMatrix`), which compares a 26/27-byte prologue and
  logs "Witcher 3 4.04 script callback signature mismatch".
- `install_engine_native_camera_authority_hooks` installs six hooks unconditionally from
  `ensure_initialized`.
- There is no pattern-scan infrastructure. Comments use Ghidra names (`FUN_1415E5A90`, ...).

## Build-dependent code by category

| # | Category | Size | Risk on new exe |
|---|---|---|---|
| 1 | Hard-coded RVAs into `witcher3.exe` | ~58 values | certain |
| 2 | MinHook targets | 78 call sites: 27 game-internal, ~51 D3D12/DXGI/system/NGX/SL | game-internal only |
| 3 | Engine struct offsets | ~100 expressions over ~15 object types | if layouts changed |
| 4 | Renderer fingerprints | 28 shader hashes, ~50 root/CB/format heuristics | if shaders rebuilt |
| 5 | Witcher scripts / bridge | ~35 `@wrapMethod`, ~100 `@addField`/`@addMethod`, DLC bundle | low (bundle: high) |
| 6 | Launcher | ~15 path/settings assumptions | low–medium |
| 7 | Explicit version strings | 2 | informational |

### 1. RVAs

- **Hooked game functions (27):** constants named `kEngine*Rva` (view constants, temporal
  writer, frame builder, DLSS command emit/command, upscaler pipeline, gameplay entry, scene
  enqueue, render-scene setup, shadow cascade build, frustum AABB test). There is also the
  native-hook table around the head-pose / camera-authority installers: `IsInGameplayScene`,
  `IsInNonGameplayCutscene`, `IsPlayingNonGameplayScene`, `EnableManualControl`,
  `GetTopmostCameraObject`, `IsLoadingScreenVideoPlaying`, `GetCameraDirection`,
  `WorldVectorToViewRatio`, temporal camera build, view rebuild, view copy rebuild,
  `FrameDataFactory`, `IsAnyMenu`, viewport resolution.
- **Called directly:** `kTransformToEulerRva`, scene enqueue (second eye), frame-data release
  via vtable slot 2.
- **Data/.rdata:** `kStreamlineInterfaceRva` (game's Streamline wrapper, vtable +0x98), render
  scheduler global (diagnostic), script argument decoder table, script out-param, projection
  constants used by the local `WorldVectorToViewRatio` reimplementation,
  `kNativeCustomCameraVtableRva`.
- **Return-address identities:** `kAsymmetricTemporalWriterReturnRvas` (8),
  `kAsymmetricCullingFrustumReturnRvas` (9, apparently unused), `kCenteredTemporalWriterReturnRva`,
  `kGameplayFrameBuilderCallerRva`, `kNormalHaltonReturnRva`,
  `kAnimatedComponentVisibilityReturnRva`, `kViewFactoryReturnRva`, `kViewCopyRebuildReturnRva`,
  plus two shadow-cascade callers in `src/shadow_cascade_authority_policy.h`. These are also
  asserted in `tests/shadow_cascade_authority_policy_tests.cpp`.

### 2. Hooks that do not depend on the build

D3D12 device and command-list vtables, DXGI Present/Present1/ResizeBuffers/CreateSwapChain*,
`D3D12CreateDevice`, `XInputGetState`, `LoadLibrary*`, NGX/OptiScaler exports, Streamline
`sl*` exports. Streamline hooks assume the SL 1.5 `sl::Constants` layout.

### 3. Struct layouts

Main groups: `frame_data` (camera +0x20, projection centres +0x420/+0x930, matrices
+0x520..0x590, dimensions +0xA3C/+0xA4C, frame id +0xAA4), the 512-float view array (basis,
projection +0x80, viewport, jitter +0x400, temporal matrix +0x3C0), scene descriptor copy
sizes (0xBD00/0xC000), player rotation/position/scene flags, game loading flag +0x198,
CameraDirector camera list, `dlss_state` +0x6C..0x74, script VM stack decoding.

### 4. Renderer fingerprints

`hash_shader_bytecode` = FNV-1a 64 over full bytecode, so any recompiled shader changes its
hash. There are 28 hash constants: terrain VS/HS/DS, smoke, TAAU resolve, tiled light, RT
AO/shadow/specular, TAAU motion writer, nudge/orbit probes, foliage
(`src/foliage_native_snapshot_policy.h`), HUD composite and crosshair. Further assumptions:
- root signature indices and CB sizes (TAAU cb10 ≥512, tiled culling cb12 = 4608)
- embedded replacement HLSL that mirrors game register layouts (`CameraShaderConsts b6`,
  HUD `CustomPixelConsts b3`, `GeometryCamera b1`)
- 512×512 Witcher Senses history
- ~47 DXGI format checks
- `launcher/startup_checks.cpp` whitelist of the game's DX12 DLLs

### 5. Scripts and bridge

`support/mod*/content/scripts/local/*.ws` use annotations only and override no vanilla
file. All wrapped vanilla methods checked so far still exist in the Remaster script sources;
signatures have not been compared yet. The DLL↔script channel consists of:
- magic return values of the hooked `GetHeadBoneIndex` (heartbeats)
- marker reads of `IsInNonGameplayCutscene`
- a FOV tag on `CCustomCamera.fov` (`kMarkerBase = 4096`)
- in-game settings group `W3VRSettings`

The channel therefore breaks with category 1.
`support/dlcmovementinputfix` (removed from this repository; it is in the original release) is a binary content bundle installed to `dlc/`. The Remaster
layout has no `dlc` folder (content merged into `content/content0`).

### 6. Launcher

Expects to sit next to `witcher3.exe`. It reads and writes
`Documents\The Witcher 3\dx12user.settings` (AAMode, DLSSQuality, AllowDLSS,
`DlcEnabled_movementinputfix`, `W3VRSettings`) and applies the template
`launcher/vr_dx12user.settings`. A comparison of freshly written default files (4.04 vs 5.01)
gives the following:
- **Removed or renamed in 5.x:**
  - `[Rendering/TAA]` CAS section (consistent with TAAU removal)
  - `EnableFSR1`
  - `FSR2Sharpness*`, now `FSRSharpness*`
  - `DynamicResolutionPercentage`
  - `VSyncThreshold`, `HdrGamma1/2`, `RTXGI SkyLight*`
- **New in 5.x:**
  - path tracer, ray reconstruction and FSR/XeSS frame generation keys
  - `AMDAntiLag`
  - mod manager keys (`ModioEnabled`, `[ContentManager/Mods]`)
  - accessibility options
  - `StartupFlow LastGameVersion=500`
- `AAMode` value semantics differ (5.01 default 5, while the mod knows 0/3/6).
- Remaster defaults enable RT (GI/AO/shadows) and much larger foliage, LOD and shadow distances.
- The `Allow*` post-process keys and the texture-downscale keys are absent from both fresh
  defaults. They exist only in the mod's profile.

So the launcher's VR profile and AAMode table need a Remaster variant. Applying the 4.04
profile to 5.x must be avoided. It also expects
the user-config-matrix XML and the HUD editor script path. There is no version detection.

## Runtime findings on the Remaster (with build guard, engine hooks disabled)

- The proxy loads and runs stably on 5.01. Device-level hooks (resource/descriptor creation),
  queue-level hooks (`ExecuteCommandLists`) and DXGI hooks (`Present`, `ResizeBuffers`) fire.
- **Command-list hooks never fire.** 5.01 ships the D3D12 Agility SDK
  (`bin\x64_dx12\D3D12_0\D3D12Core.dll`, 1.619), while 4.04 uses the OS runtime. The detours
  are placed on the function table of a dummy command list, which resolves into the Agility
  `D3D12Core.dll`. The game's command lists instead carry a **per-object function table embedded
  in the object** (observed at object + 0x5C8), whose entries point **directly into the GPU
  driver** (`amdxc64.dll` on AMD). Draws, PSO/root-signature binds, render-target binds, etc.
  are therefore invisible to the current hooking scheme.
  Unverified: behaviour on NVIDIA/Intel drivers and the exact runtime feature behind this.
- Falling back to the OS runtime is not an option. With `D3D12_0` renamed, 5.01 refuses to
  start ("GPU does not meet minimal requirements. Support for DirectX 12 is required.").
- Consequence: command-list interception needs a new mechanism, e.g. patching each command
  list's embedded table on creation (`CreateCommandList*` and `Reset`), instead of inline
  detours on runtime functions.
- With `[openxr] enabled=0`, the proxy still forces the swapchain to `render_width/height`
  when those INI values are non-zero. Set them to 0 for flat tests.

## Renderer fingerprints on the Remaster (2026-10-11)

Method: shader census plus bytecode dumps on the same route in both builds, matched offline with
`dxc -dumpbin` (signatures, resource bindings, thread groups, DXIL op histograms) and through whole
pipeline combinations (stage hashes, render-target count/format, DSV format, topology).

All shaders were recompiled (newer DXIL, native 16-bit types), so every fingerprint needs a 5.01
hash. Per feature (the mod's assumptions beyond the hash, and their status on 5.01):

| Feature | 5.01 counterpart | Mod assumptions | 5.01 status |
|---|---|---|---|
| Terrain motion replay (VS/HS×2/DS) | found, high confidence (two HS like 4.04) | b1 camera rows 0-7, DS output signature, 4 RTs incl. RGBA16F velocity | DS output signature identical, CameraShaderConsts append-only: hashes only |
| Smoke world-up GS (VS/PS) | found, high confidence | camera CBV in graphics root table 3, offset 1, size >= 0x24C, camera position +0x240, VS output TEXCOORD0-7 | VS output signature identical, used rows unchanged: hashes only |
| Foliage snapshot (2 VS/PS pairs) | found, high confidence | root table 3: VS b0 size 772-1024, b12 size 160-256, copied verbatim | sizes 776/484 still inside the windows: hashes only |
| HUD composite (VS/PS) | PS found (bindings identical), VS likely | replacement PS: t0, t1, s1, b3 offsets 0x10/0x20, SV_Target0 | **pipeline now has 2 render targets** (4.04: 1); replacement PSO needs review |
| Aim probe (VS/PS) | found, high confidence | non-indexed draw, 6 vertices, 1 instance | hashes only |
| Tiled light / dimmer culling (2 CS) | likely | SharedPixelConsts in CBV slot 12, exact size 4608, patches 0xCF0/0xD00/0xD20 | **offsets moved by +0x100, size 5632 expected**; currently falls back safely on the size check |
| TAAU resolve / motion writer | n/a | | TAAU removed in 5.01 |
| RT/NRD history (6 CS) | not checked (RT off) | exact cb0 sizes, fixed patch offsets | open |
| Nudge/orbit probes (2 pairs) | B likely, A ambiguous (PS) | roots 10/13, offsets < 0x7F0 | reverse-engineering diagnostics only |

All features fail safe when an assumption does not hold (log and native draw). Most features only
need their 5.01 hashes; HUD composite and tiled culling need code changes as well.

## Approach and effort (rough, high uncertainty)

1. **Build guard.** Identify the exe build at startup and disable game hooks on unknown builds.
2. **Address layer.** Per-build tables or byte-pattern scans. Rediscover the ~58 RVAs in the
   Remaster exe by diffing against 4.04 (Ghidra Version Tracking / BinDiff). Having both builds
   installed is the main enabler.
3. **Check without a headset.** Log whether hooks hit, struct fields look sane, and which
   shader hashes appear (diagnostic logging), on 4.04 first as the baseline.
4. **Renderer.** Re-fingerprint passes whose hashes changed and revalidate root/CB assumptions.
   This is the unknown: weeks if mostly unchanged, much more if the renderer was reworked.
5. **Content.** Replace or drop the movement-input-fix DLC bundle for the merged content layout.
