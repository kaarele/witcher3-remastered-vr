# w3vr-nullxr

A minimal headless OpenXR runtime (Direct3D 12 only) for testing the mod without a headset, on
Windows or under Proton. It reports a Quest 3-like headset (field of view and recommended size as
reported by WiVRn), paces `xrWaitFrame` (fixed rate, or a model of WiVRn's application pacer with
`W3VR_NULLXR_PACER=wivrn`), accepts every submitted layer and can save the submitted projection and
quad layer images as BMP files.

**Test tool only.** It is built separately from the mod and is never shipped in the release
packages. Do not leave it configured when you play with a headset.

## Build and use

```bash
XWIN_SYSROOT="$XWIN_SYSROOT" tools/w3vr-nullxr/build.sh   # -> build/w3vr-nullxr/w3vr_nullxr.dll
```

Run it inside the clang-cl/xwin build container described in
[linux/BUILD-LINUX.md](../../linux/BUILD-LINUX.md). Copy the DLL to
`<game>/bin/x64_dx12/w3vr_nullxr/` and point the mod at it in `witcher3vr.ini`:

```ini
[test]
openxr_loader=w3vr_nullxr\w3vr_nullxr.dll
```

The mod then loads this DLL in place of `openxr_loader.dll`. Clear `openxr_loader` again before
playing with a headset. The test workflow and the main environment variables are described in
[Headless testing](../../README.md#headless-testing-for-developers) in the top-level README; the
full list of variables is in the header of [`w3vr_nullxr.cpp`](w3vr_nullxr.cpp).

## License

GPL-3.0-or-later; see [LICENSE](LICENSE). The frame-pacing model is adapted from WiVRn's
application pacer (`server/driver/app_pacer.cpp`, Copyright (C) 2025 Patrick Nicolas,
[WiVRn](https://github.com/WiVRn/WiVRn), GPL-3.0-or-later).

This licence applies only to this directory. The mod itself is under the MIT License (see the
top-level [LICENSE](../../LICENSE)).
