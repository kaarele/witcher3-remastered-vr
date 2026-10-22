#!/usr/bin/env bash
# Witcher 3 Remastered VR - Linux installer. Open "Install (Linux)" in the release folder, or run this script.
# Everything it installs comes from this folder; nothing is downloaded.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
payload="$root/payload"
tool="$payload/Witcher3VR/linux/w3vr-linux.py"

finish() {
    if [ -t 0 ]; then
        echo
        read -r -p "Press Enter to close this window." _
    fi
    exit "$1"
}

echo "Witcher 3 Remastered VR - installer"
echo "-----------------------------------"
if [ ! -f "$payload/bin/x64_dx12/dxgi.dll" ]; then
    echo "error: the release files are missing next to this installer ($payload)."
    echo "Extract the whole zip first, then open \"Install (Linux)\" from the extracted folder."
    finish 1
fi
if command -v python3 >/dev/null 2>&1; then
    python3 -I "$tool" setup --payload "$payload" "$@"
    finish $?
fi
# No python3 (unusual): the basic install below copies the files and prints what is left to do by hand.
. "$here/basic-install.sh"
basic_install "$payload"
finish $?
