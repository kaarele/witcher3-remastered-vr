# Building dxgi.dll on Linux (clang-cl + lld-link + xwin)

The mod's `dxgi.dll` can be cross-compiled on Linux with clang-cl and lld-link against Microsoft's
C runtime and Windows SDK, which [xwin](https://github.com/Jake-Shadle/xwin) downloads. All tools
live in a distrobox container, so nothing is installed on the host (this also works on immutable
distributions).

The Linux build uses the same target configuration as the Windows release build: Release, static
CRT (`/MT`), the same `witcher3vr_dxgi` defines and include directories, `-std:c++20`,
`src/dxgi_proxy.def` and `/MAP`. It builds only `dxgi.dll`; the launcher and the tests are not part
of the Linux build.

All commands below run inside the container, from the root of your clone of this repository
(distrobox shares your home directory with the host).

## Requirements

- On the host: distrobox with Podman or Docker.
- In the container: Ubuntu 24.04, LLVM/Clang 19 or newer from [apt.llvm.org](https://apt.llvm.org/)
  (22 is tested; the scripts default to `/usr/lib/llvm-22/bin`), CMake 3.24 or newer, Ninja, Git and
  Python 3.
- xwin, and Microsoft's CRT and Windows SDK, which xwin downloads after you accept Microsoft's
  licence terms (step 3).

Ubuntu's own LLVM (18) is too old: the MSVC STL that xwin downloads (14.44) refuses Clang older
than 19 (`STL1000`), and Ubuntu's `clang` package does not ship `clang-cl`.

## 1. Create the container

```bash
distrobox create --name w3vr-build --image docker.io/library/ubuntu:24.04
distrobox enter w3vr-build
```

## 2. Install the packages

```bash
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
  cmake ninja-build git python3 curl ca-certificates gnupg file
```

LLVM from the official apt.llvm.org repository (signing key fingerprint
`6084 F3CF 814B 57C1 CF12 EFD5 15CF 4D18 AF4F 7421`):

```bash
LLVM_VER=22
sudo install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | sudo tee /etc/apt/keyrings/apt.llvm.org.asc >/dev/null
gpg --show-keys /etc/apt/keyrings/apt.llvm.org.asc        # check the fingerprint
echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] http://apt.llvm.org/noble/ llvm-toolchain-noble-$LLVM_VER main" \
  | sudo tee /etc/apt/sources.list.d/llvm-$LLVM_VER.list
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
  clang-$LLVM_VER clang-tools-$LLVM_VER lld-$LLVM_VER llvm-$LLVM_VER
```

This provides `clang-cl`, `lld-link`, `llvm-lib`, `llvm-rc`, `llvm-mt`, `llvm-readobj` and
`llvm-objdump` in `/usr/lib/llvm-$LLVM_VER/bin`. With a version other than 22, set
`XWIN_LLVM_ROOT=/usr/lib/llvm-$LLVM_VER/bin` for the build scripts.

## 3. Create the xwin sysroot

Download xwin (tested with 0.10.0) in a directory outside the repository and check the tarball
against the published `.sha256` file:

```bash
XWIN_VER=0.10.0
XWIN_TGZ=xwin-$XWIN_VER-x86_64-unknown-linux-musl.tar.gz
curl -fsSLO https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VER/$XWIN_TGZ
curl -fsSLO https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VER/$XWIN_TGZ.sha256
sha256sum $XWIN_TGZ; cat $XWIN_TGZ.sha256                 # the two hashes must match
tar xzf $XWIN_TGZ
install -D -m 0755 xwin-$XWIN_VER-x86_64-unknown-linux-musl/xwin ~/.local/bin/xwin
```

xwin downloads Microsoft's CRT and Windows SDK only after you accept
[Microsoft's licence terms](https://go.microsoft.com/fwlink/?LinkId=2086102); pass
`--accept-license` once you have read them (without it, xwin asks and exits). The sysroot is for
your own builds: never commit or redistribute it.

```bash
export XWIN_SYSROOT="$HOME/xwin-sysroot"
xwin --accept-license --cache-dir "$HOME/.cache/xwin" --arch x86_64 splat --output "$XWIN_SYSROOT"
```

- xwin picks the newest CRT and SDK in the Visual Studio 2022 manifest (tested: MSVC CRT 14.44,
  Windows SDK 10.0.26100). Pin them with `--crt-version` / `--sdk-version` if needed.
- Use the default splat layout. Do not pass `--disable-symlinks`: the lowercase header and library
  aliases that xwin creates are needed on a case-sensitive file system.
- The debug CRT is only splatted with `--include-debug-libs`, which only a Debug (`/MTd`) build needs.
- The cache directory can be deleted after the splat.

Check the sysroot:

```bash
ls "$XWIN_SYSROOT"/crt/include/vcruntime.h "$XWIN_SYSROOT"/sdk/include/um/windows.h \
   "$XWIN_SYSROOT"/sdk/include/um/d3d12.h "$XWIN_SYSROOT"/crt/lib/x86_64/libcmt.lib \
   "$XWIN_SYSROOT"/sdk/lib/um/x86_64/{kernel32,d3d12,dxgi,dxguid}.lib
```

## 4. Fetch the dependencies

```bash
linux/build/fetch-deps.sh          # optional argument: the repository root (default: this clone)
```

`fetch-deps.sh` is a bash port of `scripts/setup-dependencies.ps1`: the same repositories, pinned
commits and sparse paths, plus a check that each checkout is exactly at its pin and the same
presence checks as `CMakeLists.txt`. It is idempotent. Everything goes to `external/`, which is
ignored by git (the NGX headers must never be committed).

| `external/` | Repository | Sparse paths |
|---|---|---|
| `minhook` | TsudaKageyu/minhook | `include src` |
| `openxr-sdk` | KhronosGroup/OpenXR-SDK (1.1.61) | `include` |
| `openxr-loader` | KhronosGroup/OpenXR-SDK (1.0.22) | `include src` |
| `directx-headers` | microsoft/DirectX-Headers | `include` |
| `streamline` | NVIDIA-RTX/Streamline (2.12.0) | `external/ngx-sdk/include` |

`openxr-loader` is only used by `scripts/package-release.ps1`; skip it with
`W3VR_SKIP_OPENXR_LOADER=1`. `external/renderdoc/renderdoc_app.h` is tracked in git.

## 5. Build dxgi.dll

```bash
linux/build/build-dxgi.sh
```

Or from the host:

```bash
distrobox enter w3vr-build -- env XWIN_SYSROOT="$XWIN_SYSROOT" linux/build/build-dxgi.sh
```

Output: `build/linux-clang-cl/dxgi.dll`, with the `dxgi.lib` import library, `dxgi.map` and
`compile_commands.json` (for clangd) next to it. `build/` is ignored by git. To install the DLL, see
[LINUX.md](../LINUX.md) (`linux/w3vr-linux.py install ... --dll build/linux-clang-cl/dxgi.dll`).

The script checks the sysroot (exit code 3, with the splat command, if `XWIN_SYSROOT` is unset or
incomplete), runs `fetch-deps.sh` if `external/` is missing, configures with CMake and the toolchain
file, builds only the `witcher3vr_dxgi` target and runs `verify-dxgi.sh` (exit code 1 if that fails).

| Variable | Default | Meaning |
|---|---|---|
| `XWIN_SYSROOT` | required | xwin splat output directory |
| `SRC` | this repository | Source tree |
| `OUT` | `$SRC/build/linux-clang-cl` | CMake build directory |
| `LOGS` | `$OUT/logs` | `fetch-deps`, `configure`, `build-dxgi` and `verify-dxgi` logs |
| `XWIN_LLVM_ROOT` | `/usr/lib/llvm-22/bin` | Directory with `clang-cl`, `lld-link` and `llvm-*` |
| `CLEAN` | `0` | `1` wipes `OUT` before configuring |
| `VERIFY_REF` | unset | Reference `dxgi.dll` for `verify-dxgi.sh` |

The script runs these commands, which you can also run by hand:

```bash
cmake -S . -B build/linux-clang-cl -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/linux/build/clang-cl-xwin.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DW3VR_BUILD_LAUNCHER=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DXWIN_SYSROOT="$XWIN_SYSROOT" -DXWIN_LLVM_ROOT=/usr/lib/llvm-22/bin
cmake --build build/linux-clang-cl --target witcher3vr_dxgi --parallel "$(nproc)"
linux/build/verify-dxgi.sh build/linux-clang-cl/dxgi.dll
```

`BUILD_TESTING` and `W3VR_BUILD_LAUNCHER` only add the test executables and `launcher/`; neither
changes `dxgi.dll`. The test executables are Windows programs and cannot run on Linux without Wine.

## 6. Verify the DLL

`build-dxgi.sh` runs `verify-dxgi.sh` at the end. To run it by hand:

```bash
linux/build/verify-dxgi.sh [<built dxgi.dll>] [<reference dxgi.dll>]
```

The built DLL defaults to `build/linux-clang-cl/dxgi.dll`. The check fails (exit code 1) if one of
the required exports is missing (the `src/dxgi_proxy.def` exports plus `W3VR_GetHudlessXrColorV1`)
or if the DLL imports a dynamic CRT (`VCRUNTIME*`, `MSVCP*`, `ucrtbase`, `api-ms-win-crt-*`): it
must link the C runtime statically. It also prints the PE header, imports, resources (the embedded
manifest, `RT_MANIFEST` id 2) and sections. With a reference DLL, for example an MSVC build of the
same source, it also prints the header, export and import differences for review; the bytes never
match because the compilers differ.

## The toolchain file

`linux/build/clang-cl-xwin.cmake` sets up the cross build:

- `CMAKE_SYSTEM_NAME Windows`, `--target=x86_64-pc-windows-msvc`, `clang-cl` for C and C++,
  `lld-link`, `llvm-lib`, `llvm-rc` and `llvm-mt` (absolute paths, searched in `XWIN_LLVM_ROOT`
  first). `XWIN_LLVM_SUFFIX` handles suffixed tool names such as `clang-cl-19`.
- `XWIN_SYSROOT` (cache variable or environment) is required. Headers come through `/imsvc`
  (`crt/include`, `sdk/include/{ucrt,um,shared,winrt}`), libraries through `/libpath:`.
- `-fms-compatibility-version` is read from the sysroot's `yvals_core.h` (19.44 for CRT 14.44;
  override with `XWIN_MSVC_COMPAT_VERSION`). Configure warns if clang is older than the STL demands.
- Static CRT as in `CMakeLists.txt` (`CMAKE_MSVC_RUNTIME_LIBRARY`, `/MT`). CMake's own compiler
  checks run in Release, so the debug CRT is not needed.
- `XWIN_ASYNC_SEH=ON` (default) adds `-Xclang -fasync-exceptions`. MSVC's `__try/__except` catches
  faults raised by instructions inside the `__try` body; plain clang only catches faults raised
  inside calls. Many of the mod's `__try` blocks guard inline reads of engine memory, so keep it on.
- `XWIN_MSVC_DIAG_PARITY=ON` (default) adds `-Wno-error=c++11-narrowing` (MSVC only warns, C4838).
- `XWIN_VS_SUBSYSTEM_PARITY=ON` (default) links DLLs with `/subsystem:console`, as the Visual
  Studio generator does. This only affects the PE header; the loader ignores it for DLLs.
- Libraries whose exact spelling is not in the sysroot (for example `Synchronization.lib`) get a
  symlink in `<build>/xwin-casefix`, which is added to the library path. `XWIN_CASEFIX_LIBS` adds
  more names.
- Release links with `/INCREMENTAL:NO`, so CMake passes `/MANIFEST:EMBED,ID=2` and lld-link embeds
  the default asInvoker manifest itself.

## Source changes for clang-cl

clang-cl-only source changes must be guarded with `#if defined(__clang__)` and be no-ops for MSVC.
Prefer a flag in the toolchain file when one does the job.

## Headset-free OpenXR tests (optional)

`tools/w3vr-nullxr/` is a minimal OpenXR runtime (D3D12 only) for testing the full VR path without
a headset, also under Proton. It reports a static Quest 3-like headset, paces `xrWaitFrame`, sends
the session state events, accepts every submitted layer and can save the submitted projection and
quad layers as BMP files, including the HUD composite. The full guide, with the environment
variables, is in [Headless testing](../README.md#headless-testing-for-developers) in the README.

Build it inside the container and copy it next to the game:

```bash
tools/w3vr-nullxr/build.sh          # needs XWIN_SYSROOT and external/openxr-sdk -> build/w3vr-nullxr/w3vr_nullxr.dll
mkdir -p "<game>/bin/x64_dx12/w3vr_nullxr"
cp build/w3vr-nullxr/w3vr_nullxr.dll "<game>/bin/x64_dx12/w3vr_nullxr/"
```

Point the mod at it in `witcher3vr.ini`, for test runs only:

```ini
[test]
openxr_loader=w3vr_nullxr\w3vr_nullxr.dll
input_script=linux-capture-remaster.txt
```

- A test run loads your latest save and the game may autosave: back up your saves first, and do not
  touch the mouse, keyboard or gamepad during a scripted run.
- The DLL exports every `xr*` entry point, so the mod loads it in place of `openxr_loader.dll`.
  `XR_RUNTIME_JSON` cannot be used under Proton: the Windows OpenXR loader ignores it under Wine,
  which runs the game with an elevated token. (`build.sh` still writes a `w3vr_nullxr.json` runtime
  manifest for native Windows loaders.)
- Frame pacing: by default `xrWaitFrame` paces at a fixed rate and never blocks a late caller, so
  null runs show free-running throughput. `W3VR_NULLXR_PACER=wivrn` models WiVRn's application
  pacer instead (the frame-rate steps seen in the headset); `W3VR_NULLXR_PACER_GPU_MS`,
  `_COMPOSITOR_MS`, `_MARGIN_MS` and `_BACKPRESSURE` tune it, and `W3VR_NULLXR_FRAME_LOG=1`
  (default with `wivrn`) logs one `frame` line per `xrEndFrame`. See the header of
  `tools/w3vr-nullxr/w3vr_nullxr.cpp`.
- Input scripts are in `scripts/test-input/` (`linux-capture-remaster.txt` for 5.01); copy one next
  to the game exe and adjust its timings to your load times.
- Camera debugging: `[debug] camera_offset_trace=1` logs every factory camera (`CAMTRACE` lines),
  and `linux/tools/camtrace.py` summarises them.
- **Clear `openxr_loader` again before playing with a headset.** `linux/w3vr-linux.py configure`
  clears it.
