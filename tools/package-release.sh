#!/usr/bin/env bash
# Build the release zip in the "extract anywhere, open Install" layout.
#
#   tools/package-release.sh STAGE VERSION [OUT_DIR]
#
# STAGE mirrors the game folder (bin/, mods/, Witcher3VR/) with the tested dxgi.dll and witcher3vr.ini.
# The installer scripts, the Linux tool and the 5.01 state-bridge scripts are taken from this repository, so the
# zip always ships the versions that match the code. Output: OUT_DIR/Witcher3RemasteredVR-vVERSION.zip and
# SHA256SUMS.txt.
#
# Zip layout:
#   Witcher3RemasteredVR-vX.Y.Z/
#     Install (Linux).desktop   Install (Windows).bat   Uninstall (Linux).desktop   Uninstall (Windows).bat
#     README-FIRST.txt
#     installer/               (the scripts the four launchers run)
#     payload/                 (copied into the game folder; payload/Witcher3VR also carries the uninstallers,
#                               so the mod can be removed from the game folder after the zip is gone)
set -euo pipefail
stage=${1:?usage: package-release.sh STAGE VERSION [OUT_DIR]}
version=${2:?usage: package-release.sh STAGE VERSION [OUT_DIR]}
out=${3:-.}
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -f "$stage/bin/x64_dx12/dxgi.dll" ] || { echo "error: $stage has no bin/x64_dx12/dxgi.dll" >&2; exit 1; }
[ -f "$stage/bin/x64_dx12/witcher3vr.ini" ] || { echo "error: $stage has no bin/x64_dx12/witcher3vr.ini" >&2; exit 1; }
mkdir -p "$out"
out="$(cd "$out" && pwd)"
name="Witcher3RemasteredVR-v$version"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
top="$work/$name"
mkdir -p "$top/installer" "$top/payload"
cp -a "$stage/." "$top/payload/"
# Tool and scripts from the repository.
mkdir -p "$top/payload/Witcher3VR/linux"
cp "$repo/linux/w3vr-linux.py" "$top/payload/Witcher3VR/linux/w3vr-linux.py"
cp -a "$repo/support/remaster/." "$top/payload/mods/"
for f in install.sh uninstall.sh basic-install.sh common.ps1 install.ps1 uninstall.ps1; do
    cp "$repo/installer/$f" "$top/installer/$f"
done
for f in 'Install (Linux).desktop' 'Uninstall (Linux).desktop' 'Install (Windows).bat' 'Uninstall (Windows).bat' \
         README-FIRST.txt; do
    cp "$repo/installer/$f" "$top/$f"
done
mkdir -p "$top/payload/Witcher3VR/installer"
for f in uninstall.sh basic-install.sh common.ps1 uninstall.ps1; do
    cp "$repo/installer/$f" "$top/payload/Witcher3VR/installer/$f"
done
cp "$repo/installer/Uninstall (Linux).desktop" "$repo/installer/Uninstall (Windows).bat" "$top/payload/Witcher3VR/"
find "$top" -name '__pycache__' -prune -exec rm -rf {} +

python3 -I - "$work" "$name" "$out/$name.zip" <<'PY'
import os, sys, zipfile
work, name, dest = sys.argv[1:]
EXEC = ('.sh', '.desktop')
CRLF = ('.bat', '.ps1')
root = os.path.join(work, name)
entries = []
for base, dirs, files in os.walk(root):
    dirs.sort()
    rel_base = os.path.relpath(base, work)
    entries.append((rel_base + '/', None))
    for f in sorted(files):
        entries.append((os.path.join(rel_base, f), os.path.join(base, f)))
with zipfile.ZipFile(dest, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for arc, path in entries:
        info = zipfile.ZipInfo(arc.replace(os.sep, '/'), date_time=(2026, 1, 1, 0, 0, 0))
        info.create_system = 3  # Unix, so unzip/Ark/file-roller restore the permissions below
        if path is None:
            info.external_attr = (0o40755 << 16) | 0x10
            z.writestr(info, b'')
            continue
        with open(path, 'rb') as f:
            data = f.read()
        if path.endswith(CRLF):
            data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
        info.external_attr = (0o100755 if path.endswith(EXEC) else 0o100644) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
print('wrote %s (%d entries)' % (dest, len(entries)))
PY
(cd "$out" && sha256sum "$name.zip" > SHA256SUMS.txt && cat SHA256SUMS.txt)
