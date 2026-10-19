#!/usr/bin/env bash
# build-dxgi.sh - configure + build ONLY witcher3vr_dxgi (dxgi.dll) with clang-cl + lld-link
# against an xwin MSVC CRT / Windows SDK sysroot, then check the result with verify-dxgi.sh.
#
# Builds dxgi.dll from this repository. The toolchain file, fetch-deps.sh and
# verify-dxgi.sh are taken from this script's own directory (linux/build).
# See linux/BUILD-LINUX.md for the container setup and the xwin sysroot.
#
# Run from the repository root inside your build container, for example:
#   distrobox enter <box> -- env XWIN_SYSROOT="$XWIN_SYSROOT" linux/build/build-dxgi.sh
#
# Environment:
#   XWIN_SYSROOT    required: the xwin splat output directory
#   SRC             source tree (default: this repository)
#   OUT             CMake build dir (default: $SRC/build/linux-clang-cl)
#   LOGS            log dir (default: $OUT/logs)
#   XWIN_LLVM_ROOT  directory with clang-cl/lld-link/llvm-* (default: /usr/lib/llvm-22/bin)
#   CLEAN=1         wipe OUT before configuring
#   VERIFY_REF      optional reference dxgi.dll that verify-dxgi.sh compares against
# Logs: $LOGS/{fetch-deps,configure,build-dxgi,verify-dxgi}.log
# Exit: 0 = built and verified, 1 = verify-dxgi.sh failed, 2 = bad source tree,
#       3 = XWIN_SYSROOT unset or incomplete; other codes come from cmake.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SRC=${SRC:-$(cd -- "$HERE/../.." && pwd)}
OUT=${OUT:-$SRC/build/linux-clang-cl}
LLVM=${XWIN_LLVM_ROOT:-/usr/lib/llvm-22/bin}
LOGS=${LOGS:-$OUT/logs}

# ---- 0. preconditions -------------------------------------------------------
splat_help() {
    cat >&2 <<EOF
Create the sysroot yourself after reading and accepting Microsoft's licence terms
(https://go.microsoft.com/fwlink/?LinkId=2086102), see linux/BUILD-LINUX.md:
  xwin --accept-license --arch x86_64 splat --output <sysroot>
then run this script again with XWIN_SYSROOT=<sysroot>.
EOF
}
if [[ -z ${XWIN_SYSROOT:-} ]]; then
    echo "error: set XWIN_SYSROOT to your xwin splat directory" >&2
    splat_help
    exit 3
fi
SYSROOT=$XWIN_SYSROOT
if [[ ! -f $SYSROOT/crt/include/vcruntime.h || ! -f $SYSROOT/crt/lib/x86_64/libcmt.lib \
      || ! -f $SYSROOT/sdk/include/um/windows.h || ! -f $SYSROOT/sdk/lib/um/x86_64/kernel32.lib ]]; then
    echo "error: xwin sysroot missing or incomplete: $SYSROOT" >&2
    splat_help
    exit 3
fi
if [[ ! -x $LLVM/clang-cl || ! -x $LLVM/lld-link ]]; then
    echo "error: clang-cl/lld-link not found in $LLVM (set XWIN_LLVM_ROOT, e.g. /usr/lib/llvm-19/bin)" >&2
    exit 2
fi
if [[ ! -f $SRC/CMakeLists.txt || ! -f $SRC/src/dxgi_proxy.cpp ]]; then
    echo "error: $SRC does not look like a witcher3-vr source tree (set SRC)" >&2
    exit 2
fi
if [[ ${CLEAN:-0} == 1 ]]; then
    case $OUT in
        ""|/|"$SRC"|"${HOME:-/}") echo "error: refusing to wipe OUT=$OUT" >&2; exit 2 ;;
    esac
    rm -rf -- "$OUT"
fi
mkdir -p -- "$LOGS"
if [[ ! -f $SRC/external/minhook/include/MinHook.h || ! -f $SRC/external/directx-headers/include/directx/d3d12.h ]]; then
    "$HERE/fetch-deps.sh" "$SRC" 2>&1 | tee "$LOGS/fetch-deps.log"
fi

# ---- 1. configure (Release, same target config as the Windows release preset) -
cmake -S "$SRC" -B "$OUT" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/clang-cl-xwin.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF -DW3VR_BUILD_LAUNCHER=OFF \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DXWIN_SYSROOT="$SYSROOT" -DXWIN_LLVM_ROOT="$LLVM" \
    2>&1 | tee "$LOGS/configure.log"

# ---- 2. build only dxgi.dll (+ its minhook static lib) ----------------------
t0=$(date +%s)
cmake --build "$OUT" --target witcher3vr_dxgi --parallel "$(nproc)" --verbose 2>&1 | tee "$LOGS/build-dxgi.log"
t1=$(date +%s)
echo "build time: $((t1 - t0)) s (witcher3vr_dxgi, $(nproc) jobs)" | tee -a "$LOGS/build-dxgi.log"
echo "warnings: $(grep -c 'warning:' "$LOGS/build-dxgi.log" || true)" | tee -a "$LOGS/build-dxgi.log"

# ---- 3. verify: required exports + static CRT (optional reference diff) -----
verify_rc=0
XWIN_LLVM_ROOT="$LLVM" "$HERE/verify-dxgi.sh" "$OUT/dxgi.dll" ${VERIFY_REF:+"$VERIFY_REF"} 2>&1 \
    | tee "$LOGS/verify-dxgi.log" || verify_rc=$?
echo "built: $OUT/dxgi.dll"
if [[ $verify_rc -ne 0 ]]; then
    echo "error: verify-dxgi.sh failed, see $LOGS/verify-dxgi.log" >&2
    exit 1
fi
