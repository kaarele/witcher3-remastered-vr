# Changelog

Versions below 1.0 are alpha releases. Downloads are on the
[Releases page](https://github.com/kaarele/witcher3-remastered-vr/releases). Release notes of the
original mod and of the Remastered port this project builds on are in [docs/releases](docs/releases/).

## 0.1.4 (2026-10-23)

- **SteamVR on Linux tested and working** (Quest 3 through Steam Link, SteamVR 2.18): use
  `launch-options --runtime steamvr`, run the one-time `setcap` for SteamVR, and set texture quality to High
  or Medium on a 10 GB card.
- **See-through pause menu:** the whole in-game pause menu (main list and all settings pages except
  HDR calibration) floats over the live stereo world on a soft dark panel with feathered edges, so
  graphics changes are visible as you make them (`[openxr] options_menu_see_through=1`, veil 0.75,
  feather 0.15). Inventory, map and the other panels are unchanged.
- **Main menu and loading screens on a world-locked screen** (3.6 m wide at 2.5 m, raised 5°),
  fixed where you look when it appears; `F9` re-centres (`menu_world_locked=1`).
- **360° backgrounds** around that screen instead of a black void, also on the main menu after
  quitting a game (`loading_background=1`). Mono 2:1 or top-bottom 3D pictures in
  `bin/x64_dx12/w3vr_backgrounds/`, rotating every 30 s. Ships with one 3D picture captured in the game.
- **In-game 360° capture** (opt-in, `capture360_enabled=1`): pause menu + `F4` captures a 3D
  (omnidirectional stereo) 10240x10240 picture of the frozen world, assembled in the mod and added to
  the rotation; saved with the forward direction 60° left of the screen (`capture360_subject_yaw=60` in the
  release ini).
- **One-click installer:** extract the zip anywhere and open `Install (Linux)` or
  `Install (Windows)`; `Uninstall` reverts it. Nothing is downloaded. Linux sets the Steam launch
  options when Steam is closed. The Windows installer has not been run yet.
- SteamVR preparation (untested): `render_size_from_runtime`, `launch-options --runtime steamvr`.
- Linux tool: `setup`/`remove` for the installer, Snap Steam, the new keys added by `configure`.

## 0.1.3 (2026-10-22)

- **DLSS fixed in both eyes.** The eye rendered second (the left one) was jagged and flickered on
  moving characters with DLSS Quality, Balanced and Performance. Two causes, both fixed and on in the
  release ini and the Linux tool:
  - Moving objects lost their motion vectors in the second eye
    (`[engine] remaster_pair_previous_transforms=1`).
  - Each eye got only half of the DLSS jitter pattern and its own shadow noise
    (`remaster_pair_jitter=1`, `remaster_pair_noise=1`).
  Balanced is now playable without artifacts; Quality or DLAA look best.
- **Interaction prompts and NPC labels at the target's real depth** (`[openxr] world_marker_depth=1`).
- **FSR in Stereo** with a separate history for each eye (`remaster_fsr_per_eye=1`). It covers both
  of the game's FSR paths: the FidelityFX loader (FSR 4 / 3.1 on AMD) and the built-in FSR 2
  (NVIDIA, Intel, older AMD, Steam Deck). Works on NVIDIA but costs a lot of frames there; untested on
  AMD.
- Background characters keep updating while the headset sees them (`async_anim_tick_visible=1`).
- DLSS image controls: texture LOD offset (`dlss_mip_bias_offset`, 0.5 in the release ini) and an
  optional DLSS model override (`dlss_preset`, untested).
- First person: `F11` recenters on entry; optional eye at Geralt's head
  (`remaster_native_head_provider`, untested); optional follow of runtime recenters.
- Linux tool: `configure` defaults to DLSS Quality, adds the new keys when missing and supports
  `--aa fsr`.

## 0.1.2 (2026-10-20)

- Linux: `w3vr-linux.py configure` now works on a fresh install from the release zip. In 0.1.1 it
  stopped with "mod not installed (run install first)" at step 4 of the install guide.
- README: the long black screen when loading a save is the story recap video; skip it with Space / X.

## 0.1.1 (2026-10-20)

- About 15% more frames per second with DLSS Quality: one-pair run-ahead is on, and the second eye
  is queued after the game's per-frame fence (`[perf] pair_enqueue_after_fence`), so both eyes show
  the same animation poses. No gain with DLAA, where the GPU is the limit.
- People keep animating when the headset sees them but the game camera does not
  (`[engine] animated_component_visible`, now also on 5.01).
- README: quick install guide, status, slow-loading note, Steam Controller.

## 0.1.0 (2026-10-19)

First public alpha, built on joschi127's Remastered port (`remaster` branch) of tig3rmast3r's
Witcher 3 VR:

- Linux / Proton support with a setup tool, and a Linux build of the DLL.
- DLSS/DLAA on 5.01 with a separate history for each eye, also under Proton.
- Per-eye asymmetric projection.
- HUD: filtered sampling, level controller-locked HUD, centred menus and loading screens, no
  black fade rectangle, exact interaction markers; menu stereo fix.
- Geralt centred in the near view, forward direction kept after cutscenes.
- Faster descriptor tracking; shared sun shadows, per-pair light flicker and local shadows.
- Null OpenXR runtime for headless testing.
