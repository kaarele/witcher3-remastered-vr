#!/usr/bin/env bash
# Witcher 3 Remastered VR - Linux uninstaller. Undoes everything "Install (Linux)" did; saves are never touched.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
# Works from the extracted release folder and from the copy in the game folder (The Witcher 3/Witcher3VR).
tool="$root/payload/Witcher3VR/linux/w3vr-linux.py"
[ -f "$tool" ] || tool="$root/linux/w3vr-linux.py"

finish() {
    if [ -t 0 ]; then
        echo
        read -r -p "Press Enter to close this window." _
    fi
    exit "$1"
}

echo "Witcher 3 Remastered VR - uninstaller"
echo "-------------------------------------"
if command -v python3 >/dev/null 2>&1 && [ -f "$tool" ]; then
    # The tool also deletes the copy of this folder in the game folder; bash keeps reading this script from its
    # open file, so that is safe.
    python3 -I "$tool" remove "$@"
    finish $?
fi
. "$here/basic-install.sh"
basic_uninstall
finish $?
