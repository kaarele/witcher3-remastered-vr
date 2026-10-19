# The Witcher 3 VR v0.9.6 (V1526)

This release adds the first version of frame generation for VR, brings back Mono mode, adds native OptiScaler support and expands the launcher with new presentation and world-detail controls. It also includes a large group of fixes for asymmetric projection, AER, first person, cutscenes, the HUD and general render stability.

## Important

- OFXR frame generation is brand new and has been tested with VDXR and partially tested with SteamVR OpenXR. Please report results with other OpenXR runtimes.
- Mono mode has returned after only quick validation. Some cutscenes may not retain DLSS/DLAA or TAAU correctly.
- Ray tracing support has been removed for now because it does not work correctly with asymmetric projection.

## Downloads

- **Witcher3VR** is the main package and includes OFXR frame generation.
- **OptiScaler Addon** is required only if you want to use OptiScaler. Install it after the main package; it uses the FidelityFX files already supplied with the game.
- **RenderDoc Addon** contains the custom diagnostic DLL. It is optional and is not required to play.

## Highlights

### Frame Generation for VR

OFXR is a new VR frame-generation option based on optical flow. It has been tested with VDXR and partially tested with SteamVR OpenXR.

- **FidelityFX** is the faster option and can provide around a 50% frame-rate increase, but produces more artifacts.
- **NVIDIA Optical Flow** generally provides around a 25% increase with fewer artifacts.
- Actual gains depend on the GPU, scene, resolution and headset refresh rate.
- Some performance counters, including xrFPS, may display half the perceived frame rate while OFXR is enabled.
- You must launch the game through the VR Launcher to inject OFXR.

### Mono Mode Returns

Mono mode is available again as the lightest rendering option. It supports No AA, FXAA, TAAU, DLSS and DLAA. Testing is still limited, and some cutscenes may temporarily lose DLSS/DLAA or TAAU.

### Native OptiScaler Support

The mod now supports OptiScaler directly. OptiScaler relies on NVIDIA NGX, so disable DLSS Override before enabling it.

### Asymmetric Projection by Default

Asymmetric projection is now enabled by default. Press `F2` to switch temporarily back to symmetric projection. This is a temporary diagnostic toggle and is expected to be removed in a future release.

### More Cinema Formats

Cinema mode now includes `16:9` and `16:10` in addition to `5:4` and `4:3`.

### World Detail Range

The launcher now includes a World Detail Range slider.

- `100%` is the default and preserves the normal extended world-detail range.
- Lower values can recover performance in cities and other CPU-heavy areas.
- At `40%`, the extended LOD/culling correction is effectively disabled.

### Alternative Presentation Resize

The new **Alt. Resize** checkbox is required for Presentation Size to work correctly with SteamVR in the configurations tested so far. It can slightly reduce image quality, so leave it disabled if Presentation Size already works correctly with your OpenXR bridge.

## Other Features

- Removed the foliage correction because it was incomplete and too expensive. It will be revisited in a future release.
- Added general performance optimizations.

## Fixes

- Fixed the DLSS black screen after loading in AER mode.
- Fixed asymmetric resolution in AER mode.
- Fixed vertical mouse and gamepad pitch.
- Fixed first-person view while galloping.
- Removed the automatic switch from first to third person while sailing.
- Fixed doubled Witcher Senses in asymmetric projection.
- Fixed doubled lights on headsets other than Quest 3 in asymmetric projection.
- Added multiple HUD and render-stability fixes.
- Fixed most cutscenes that were still rendered symmetrically.

## Limitations

- Screen-space reflections on High can still produce stereo artifacts.
- Very distant cameras and unusual cutscenes can still expose visual inconsistencies.
- First-person mode can expose animation and camera issues in scripted situations.

## Known Issues

- Some Full VR cutscenes may show floating or unstable landscapes and terrain.
- First-person movement may behave unexpectedly when walking backward with Strafe Movement enabled.

## Pimax Users Recommendation

Use SteamVR OpenXR and enable **Alt. Resize** only if Presentation Size does not change the field of view correctly without it. Start from Presentation Size `1.00`, then reduce it gradually. Keep Alternative Resize disabled when the normal resize path already works, as it gives the best image quality.
