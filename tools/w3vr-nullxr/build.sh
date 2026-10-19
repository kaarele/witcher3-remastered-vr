#!/usr/bin/env bash
# Build w3vr_nullxr.dll (headless OpenXR runtime for tests, see linux/BUILD-LINUX.md) with clang-cl + lld-link
# against the xwin sysroot. Run inside your clang-cl/xwin build container (see linux/BUILD-LINUX.md).
#
# Environment:
#   XWIN_SYSROOT    required: the xwin splat output directory
#   OPENXR_SDK      OpenXR include dir (default: external/openxr-sdk/include, from linux/build/fetch-deps.sh)
#   OUT             output dir (default: build/w3vr-nullxr in this repository)
#   XWIN_LLVM_ROOT  directory with clang-cl/lld-link (default: /usr/lib/llvm-22/bin)
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SYSROOT=${XWIN_SYSROOT:?set XWIN_SYSROOT to your xwin splat directory (see linux/BUILD-LINUX.md)}
XR=${OPENXR_SDK:-$HERE/../../external/openxr-sdk/include}
OUT=${OUT:-$HERE/../../build/w3vr-nullxr}
LLVM=${XWIN_LLVM_ROOT:-/usr/lib/llvm-22/bin}

[[ -f $SYSROOT/crt/include/vcruntime.h && -f $SYSROOT/sdk/lib/um/x86_64/kernel32.lib ]] \
  || { echo "error: xwin sysroot missing or incomplete: $SYSROOT" >&2; exit 3; }
[[ -f $XR/openxr/openxr.h ]] \
  || { echo "error: OpenXR headers not found in $XR (run linux/build/fetch-deps.sh or set OPENXR_SDK)" >&2; exit 2; }
[[ -x $LLVM/clang-cl && -x $LLVM/lld-link ]] \
  || { echo "error: clang-cl/lld-link not found in $LLVM (set XWIN_LLVM_ROOT)" >&2; exit 2; }

mkdir -p "$OUT"
"$LLVM/clang-cl" --target=x86_64-pc-windows-msvc -fms-compatibility-version=19.44 \
  /imsvc "$SYSROOT/crt/include" /imsvc "$SYSROOT/sdk/include/ucrt" /imsvc "$SYSROOT/sdk/include/um" \
  /imsvc "$SYSROOT/sdk/include/shared" /I "$XR" \
  /std:c++20 /O2 /MT /EHsc /W3 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS \
  /c "$HERE/w3vr_nullxr.cpp" /Fo"$OUT/w3vr_nullxr.obj"
"$LLVM/lld-link" /DLL /OUT:"$OUT/w3vr_nullxr.dll" "$OUT/w3vr_nullxr.obj" \
  /LIBPATH:"$SYSROOT/crt/lib/x86_64" /LIBPATH:"$SYSROOT/sdk/lib/um/x86_64" /LIBPATH:"$SYSROOT/sdk/lib/ucrt/x86_64" \
  kernel32.lib /MACHINE:X64
# Runtime manifest, only for OpenXR loaders that honour XR_RUNTIME_JSON (native Windows).
# Under Proton the loader ignores XR_RUNTIME_JSON; use [test] openxr_loader instead
# (see linux/BUILD-LINUX.md).
cat > "$OUT/w3vr_nullxr.json" <<JSON
{
    "file_format_version": "1.0.0",
    "runtime": {
        "name": "w3vr null HMD",
        "library_path": "w3vr_nullxr.dll"
    }
}
JSON
ls -la "$OUT"
