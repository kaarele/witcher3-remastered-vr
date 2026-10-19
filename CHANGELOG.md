# Changelog

Versions below 1.0 are alpha releases. Downloads are on the
[Releases page](https://github.com/kaarele/witcher3-remastered-vr/releases). Release notes of the
original mod and of the Remastered port this project builds on are in [docs/releases](docs/releases/).

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
