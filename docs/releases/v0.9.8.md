# Witcher 3 VR v0.9.8

> [!NOTE]
> **0.9.8v2 hotfix (mod V1581):** Fixed missing subtitles and dialogue text during Full VR cutscenes.

> [!WARNING]
> Intermittent crashes may occur when using DLSS 5 Neural Rendering.
> This development release includes fixes, but does not resolve every case.

> [!IMPORTANT]
> The only supported DLSS 5 Neural Rendering integration is the included custom OptiScaler build.
> The other launcher-managed DLSS 5 integration options have been removed.
> ReShade is still supported, but DLSS 5 ReShade add-ons are no longer bundled or managed by the mod.

> [!NOTE]
> OFXR is also embedded in OptiScaler. When using that integration, `XR_APILAYER_XRFrameBridge_diagnostic.dll` provides the OpenXR bootstrap for the embedded implementation.
> You can enable OptiScaler with DLSS 5 Neural Rendering switched off and change OFXR settings live through the OptiScaler GUI.
> OptiScaler and a DLSS/DLAA rendering mode are required to use FSR 3.1 Frame Generation.

## New features

- **Experimental OFXR 0.3.0 (NVIDIA only)** — select **Nvidia (new)** in the launcher or OptiScaler GUI. **Nvidia** keeps the legacy route. The experimental route requires OptiScaler; the standalone OFXR DLL remains v0.2.1.

- **FSR 3.1 Frame Generation.**
- **Optional HUD detached from headset rotation (experimental).**
- **OFXR updated to v0.2.1.**

## Fixes and improvements

- Improved HUD stability in Stereo and AER+AFW modes.
- Fixed missing HUD elements, subtitles and dialogue choices during Cinema sequences and menu transitions.
- Fixed gameplay subtitles drifting away from their configured position.
- Improved reliability of VR shortcuts, including F9 calibration and F10 Cinema.
- Improved SteamVR FSR presentation and experimental NVIDIA pacing.
- Disabled diagnostic recording and file writes during normal gameplay; logging remains available through the launcher.
- Improved preservation of existing OptiScaler settings when saving or launching from the launcher.
- Added separate logging controls for OFXR and OptiScaler.
- Improved DLSS Neural Rendering resource lifetime and GPU synchronization safeguards. Intermittent crashes remain under investigation.
- Improved RenderDoc compatibility with the stereo rendering pipeline.

## How to use it

**Everything needed for the integrations is already in the package EXCEPT the NVIDIA DLSS 5 Neural Rendering DLL.** ReShade, OptiScaler, OFXR and the launcher are already included. No separate ReShade installation is needed.

You can experiment with DLSS 5 ReShade add-ons yourself, but they are not supported.

1. Extract the archive into **The Witcher 3** game folder, merge the folders and overwrite the package files when prompted.
2. **Only if you want to use DLSS 5 Neural Rendering:** obtain the NVIDIA runtime separately, choosing the appropriate version for your GPU. Copy only `nvngx_dlssnr.dll` into `The Witcher 3\bin\x64_dx12\witcher3vr-dlss5-reference`. This reference folder is empty in the release. Do not copy the rest of the downloaded package into it.
3. Open `Witcher3VRLauncher.exe` in `bin\x64_dx12`, choose your desired integration from the dropdown and click **Save & Launch**. For DLSS 5 Neural Rendering, also select a DLSS/DLAA rendering mode. The launcher handles the required file copies and configures the selected integration automatically.

## Credits

Internal builds: mod **V1580**, launcher **V1579**, OptiScaler **V23244**.

Thanks to [crosire / ReShade](https://github.com/crosire/reshade),
the [OptiScaler team](https://github.com/optiscaler/OptiScaler), and
[Dagherbou / OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR).

Special thanks to [sadbee166](https://github.com/sadbee166), author of
[OptiScaler_DLSSNR PR #6](https://github.com/Dagherbou/OptiScaler_DLSSNR/pull/6),
for the change that runs DLSS Neural Rendering before DLSS upscaling and forms
the basis of the VR v2 pre-DLSS path.

Our compatibility changes are available in the
[ReShade VR fork](https://github.com/tig3rmast3r/ReShade_VR) and
[OptiScaler VR fork](https://github.com/tig3rmast3r/OptiScaler_DLSSNR_VR).
Full component credits are in the README.
