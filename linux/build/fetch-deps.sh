#!/usr/bin/env bash
# fetch-deps.sh - bash port of scripts/setup-dependencies.ps1
#
# Usage: fetch-deps.sh [<repo root>]   (default: this repository)
#
# Populates <repo root>/external/{minhook,openxr-sdk,openxr-loader,
# directx-headers,streamline} with the SAME repositories, the SAME pinned
# commits and the SAME sparse (cone) paths as scripts/setup-dependencies.ps1.
# Keep the PINS table below identical to scripts/setup-dependencies.ps1 in this
# repository.
#
# external/renderdoc/renderdoc_app.h is vendored (tracked in git) and is not
# fetched here.
#
# Notes:
#  - Idempotent: an existing checkout is re-sparsed, re-fetched and re-detached
#    at the pinned commit, exactly like the .ps1.
#  - openxr-loader is only used by scripts/package-release.ps1 to build
#    openxr_loader.dll; the dxgi.dll build does not need it. Set
#    W3VR_SKIP_OPENXR_LOADER=1 to skip it.
#  - Needs only git (>= 2.27 for partial clone + cone sparse-checkout) and
#    network access to github.com. Runs fine on the host or in a container.

set -euo pipefail

if [[ $# -gt 1 ]]; then
    echo "usage: $0 [<repo root>]" >&2
    exit 2
fi

HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workdir=$(cd -- "${1:-$HERE/../..}" && pwd)
if [[ ! -f "$workdir/CMakeLists.txt" || ! -f "$workdir/scripts/setup-dependencies.ps1" ]]; then
    echo "error: $workdir does not look like a witcher3-vr checkout" >&2
    exit 2
fi

dependency_root="$workdir/external"
mkdir -p -- "$dependency_root"

# sync_dependency <name> <url> <commit> <sparse-path>...
# Mirrors Sync-Dependency in setup-dependencies.ps1 step for step.
sync_dependency() {
    local name=$1 url=$2 commit=$3
    shift 3
    local -a sparse_paths=("$@")
    local target="$dependency_root/$name"

    echo "==> $name @ $commit (sparse: ${sparse_paths[*]})"

    if [[ ! -e "$target/.git" ]]; then
        if [[ -e "$target" ]]; then
            echo "error: dependency path exists but is not a Git checkout: $target" >&2
            exit 1
        fi
        git clone --filter=blob:none --no-checkout "$url" "$target" \
            || { echo "error: failed to clone $name" >&2; exit 1; }
    fi

    git -C "$target" sparse-checkout init --cone \
        || { echo "error: failed to initialize sparse checkout for $name" >&2; exit 1; }
    git -C "$target" sparse-checkout set "${sparse_paths[@]}" \
        || { echo "error: failed to configure sparse checkout for $name" >&2; exit 1; }
    git -C "$target" fetch origin "$commit" \
        || { echo "error: failed to fetch $commit for $name" >&2; exit 1; }
    git -C "$target" checkout --detach "$commit" \
        || { echo "error: failed to check out $commit for $name" >&2; exit 1; }

    # Extra safety over the .ps1: prove HEAD is exactly the pin.
    local head
    head=$(git -C "$target" rev-parse HEAD)
    if [[ "$head" != "$commit" ]]; then
        echo "error: $name HEAD is $head, expected $commit" >&2
        exit 1
    fi
}

# ---- PINS (keep identical to scripts/setup-dependencies.ps1) ---------------
sync_dependency minhook \
    https://github.com/TsudaKageyu/minhook.git \
    98b74f1fc12d00313d91f10450e5b3e0036175e3 \
    include src
sync_dependency openxr-sdk \
    https://github.com/KhronosGroup/OpenXR-SDK.git \
    5267613edf3d937e3d77556a106a65c2f82b25c6 \
    include
if [[ "${W3VR_SKIP_OPENXR_LOADER:-0}" != "1" ]]; then
    sync_dependency openxr-loader \
        https://github.com/KhronosGroup/OpenXR-SDK.git \
        458984d7f59d1ae6dc1b597d94b02e4f7132eaba \
        include src
fi
sync_dependency directx-headers \
    https://github.com/microsoft/DirectX-Headers.git \
    2c305c16da8a4450db8d7f1e7d8d014c8bc665ee \
    include
sync_dependency streamline \
    https://github.com/NVIDIA-RTX/Streamline.git \
    e8aaa6eaac968711fb62473d4ae8256dde20919b \
    external/ngx-sdk/include
# ---------------------------------------------------------------------------

# Same presence checks as the top-level CMakeLists.txt.
missing=0
for required in \
    "$dependency_root/minhook/include/MinHook.h" \
    "$dependency_root/openxr-sdk/include/openxr/openxr.h" \
    "$dependency_root/directx-headers/include/directx/d3d12.h" \
    "$dependency_root/streamline/external/ngx-sdk/include/nvsdk_ngx.h" \
    "$dependency_root/renderdoc/renderdoc_app.h"; do
    if [[ ! -f "$required" ]]; then
        echo "error: missing dependency file: $required" >&2
        missing=1
    fi
done
[[ $missing -eq 0 ]] || exit 1

echo 'Witcher3VR dependencies are ready.'
