#!/usr/bin/env bash
# verify-dxgi.sh - structural checks of a built dxgi.dll.
#
# Usage (inside the build container):
#   verify-dxgi.sh [<built dxgi.dll>] [<reference dxgi.dll>]
#
# <built dxgi.dll> defaults to build/linux-clang-cl/dxgi.dll in this repository.
# Hard checks (exit 1 on failure):
#   - every export in REQUIRED_EXPORTS below is present;
#   - no dynamic CRT import (VCRUNTIME*, MSVCP*, ucrtbase, api-ms-win-crt-*): the
#     DLL must link the C runtime statically (/MT), like the Windows build.
# With a <reference dxgi.dll> (for example a dxgi.dll built with MSVC) the PE
# header, exports, imported DLLs and imported functions are also diffed against
# it. Those differences are printed for review only; they are expected to be
# small (different compiler), so they do not fail the check.
# Environment: XWIN_LLVM_ROOT (default /usr/lib/llvm-22/bin) for llvm-readobj/llvm-objdump.
set -uo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$HERE/../.." && pwd)
DLL=${1:-$REPO/build/linux-clang-cl/dxgi.dll}
REF=${2:-}
LLVM=${XWIN_LLVM_ROOT:-/usr/lib/llvm-22/bin}

# Keep in sync with src/dxgi_proxy.def and the __declspec(dllexport) functions
# in src/dxgi_proxy.cpp.
REQUIRED_EXPORTS=(
    CreateDXGIFactory
    CreateDXGIFactory1
    CreateDXGIFactory2
    DXGIDeclareAdapterRemovalSupport
    DXGIDisableVBlankVirtualization
    DXGIGetDebugInterface1
    W3VR_GetHudlessXrColorV1
)

if [[ $# -gt 2 ]]; then
    echo "usage: $0 [<built dxgi.dll>] [<reference dxgi.dll>]" >&2
    exit 2
fi
for f in "$DLL" ${REF:+"$REF"}; do
    [[ -f $f ]] || { echo "error: no such file: $f" >&2; exit 2; }
done
for t in llvm-readobj llvm-objdump; do
    [[ -x $LLVM/$t ]] || { echo "error: $LLVM/$t not found (set XWIN_LLVM_ROOT)" >&2; exit 2; }
done

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
rc=0

exports() { "$LLVM/llvm-readobj" --coff-exports "$1" | awk '/^ *Name: /{print $2}' | sort; }
imports() { "$LLVM/llvm-readobj" --coff-imports "$1" | awk '
    /^Import \{/            {dll=""}
    /^  Name: /              {dll=toupper($2)}
    /^  Symbol: /            {print dll "!" $2}' | sort -u; }
import_dlls() { "$LLVM/llvm-readobj" --coff-imports "$1" | awk '/^  Name: /{print toupper($2)}' | sort -u; }
hdr() { "$LLVM/llvm-objdump" -p "$1" | grep -E '^(Magic|Subsystem|DllCharacteristics|MajorSubsystemVersion|MinorSubsystemVersion|MajorOSystemVersion)\b'; }

echo "built: $DLL ($(stat -c '%s' "$DLL") bytes)"
[[ -n $REF ]] && echo "reference: $REF ($(stat -c '%s' "$REF") bytes)"
file "$DLL" 2>/dev/null || true

echo; echo "== PE header =="
if [[ -n $REF ]]; then
    echo "(built | reference)"
    diff -y -W 110 <(hdr "$DLL") <(hdr "$REF") || true
else
    hdr "$DLL" || true
fi

echo; echo "== exports =="
exports "$DLL" >"$TMP/e1"
cat "$TMP/e1"
missing=0
for e in "${REQUIRED_EXPORTS[@]}"; do
    if ! grep -qx -- "$e" "$TMP/e1"; then echo "missing export: $e"; missing=1; fi
done
if [[ $missing -eq 0 ]]; then
    echo "EXPORTS: all ${#REQUIRED_EXPORTS[@]} required exports present ($(wc -l <"$TMP/e1") total)"
else
    echo "EXPORTS: required export missing (FAIL)"; rc=1
fi
if [[ -n $REF ]]; then
    exports "$REF" >"$TMP/e2"
    diff -u "$TMP/e2" "$TMP/e1" && echo "(exports identical to reference)" || true
fi

echo; echo "== imported DLLs =="
import_dlls "$DLL" >"$TMP/d1"
if [[ -n $REF ]]; then
    import_dlls "$REF" >"$TMP/d2"
    echo "(built | reference)"
    diff -y -W 110 "$TMP/d1" "$TMP/d2" || true
else
    cat "$TMP/d1"
fi
if grep -E -i '^(VCRUNTIME|MSVCP|UCRTBASE|API-MS-WIN-CRT-)' "$TMP/d1"; then
    echo "CRT: dynamic CRT import found (FAIL: expected static /MT)"; rc=1
else
    echo "CRT: no VCRUNTIME/MSVCP/ucrtbase/api-ms-win-crt imports (static /MT confirmed)"
fi

imports "$DLL" >"$TMP/i1"
if [[ -n $REF ]]; then
    echo; echo "== imported functions: only in built (+) / only in reference (-) =="
    imports "$REF" >"$TMP/i2"
    comm -13 "$TMP/i2" "$TMP/i1" | sed 's/^/+ /'
    comm -23 "$TMP/i2" "$TMP/i1" | sed 's/^/- /'
    echo "(built: $(wc -l <"$TMP/i1") imports, reference: $(wc -l <"$TMP/i2"))"
else
    echo "(imported functions: $(wc -l <"$TMP/i1"))"
fi

echo; echo "== resources (expect RT_MANIFEST id 2) =="
"$LLVM/llvm-readobj" --coff-resources "$DLL" | grep -E 'Type:|Name:|ID:' | head -12 || true

echo; echo "== sections (built) =="
"$LLVM/llvm-objdump" -h "$DLL" | sed -n '1,20p'

echo; [[ $rc -eq 0 ]] && echo "VERIFY: PASS" || echo "VERIFY: FAIL"
exit $rc
