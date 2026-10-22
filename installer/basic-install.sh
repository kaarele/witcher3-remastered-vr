# Witcher 3 Remastered VR - basic install/uninstall in plain bash, used only when python3 is missing.
# It copies the files and prints the two steps left to do by hand (launch options, graphics settings).
# shellcheck shell=bash

W3VR_APP=292030
W3VR_STATE="${XDG_DATA_HOME:-$HOME/.local/share}/w3vr-linux"

w3vr_find_game() {
    local root lib vdf acf dir
    for root in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.steam/root" \
                "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam" \
                "$HOME/.var/app/com.valvesoftware.Steam/data/Steam" \
                "$HOME/snap/steam/common/.local/share/Steam" "$HOME/snap/steam/common/.local/share/steam"; do
        [ -d "$root/steamapps" ] || continue
        vdf="$root/steamapps/libraryfolders.vdf"
        for lib in "$root" $( [ -f "$vdf" ] && sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' "$vdf" ); do
            acf="$lib/steamapps/appmanifest_$W3VR_APP.acf"
            [ -f "$acf" ] || continue
            dir=$(sed -n 's/^[[:space:]]*"installdir"[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p' "$acf" | head -n1)
            W3VR_GAME="$lib/steamapps/common/${dir:-The Witcher 3}"
            W3VR_DOCS="$lib/steamapps/compatdata/$W3VR_APP/pfx/drive_c/users/steamuser/Documents/The Witcher 3"
            [ -d "$W3VR_GAME/bin/x64_dx12" ] && return 0
        done
    done
    return 1
}

w3vr_game_running() {
    local f
    for f in /proc/[0-9]*/cmdline; do
        tr '\0' '\n' < "$f" 2>/dev/null | head -n1 | grep -qi 'bin\\x64_dx12\\witcher3\.exe$' && return 0
    done
    return 1
}

basic_install() {
    local payload="$1" rel stamp line
    echo "python3 was not found, so this is the basic install (files only)."
    w3vr_find_game || { echo "error: The Witcher 3 was not found in your Steam libraries."; return 1; }
    echo "Found the game: $W3VR_GAME"
    if w3vr_game_running; then echo "error: quit The Witcher 3 first, then run the installer again."; return 1; fi
    if [ -d "$W3VR_DOCS/gamesaves" ]; then
        stamp=$(date +%Y%m%d-%H%M%S)
        mkdir -p "$HOME/w3vr-backups/$stamp" && cp -a "$W3VR_DOCS/gamesaves" "$HOME/w3vr-backups/$stamp/" &&
            echo "Saves backed up to $HOME/w3vr-backups/$stamp"
    fi
    mkdir -p "$W3VR_STATE"
    : > "$W3VR_STATE/basic-install-files.txt"
    (cd "$payload" && find bin mods Witcher3VR -type f 2>/dev/null) | while IFS= read -r rel; do
        mkdir -p "$(dirname "$W3VR_GAME/$rel")"
        cp -f "$payload/$rel" "$W3VR_GAME/$rel"
        printf '%s\n' "$rel" >> "$W3VR_STATE/basic-install-files.txt"
    done
    printf '%s\n' "$W3VR_GAME" > "$W3VR_STATE/basic-install-game.txt"
    echo "Mod files copied."
    line='PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1'
    [ -d /var/lib/flatpak/app/io.github.wivrn.wivrn ] &&
        line="$line PRESSURE_VESSEL_FILESYSTEMS_RW=/var/lib/flatpak/app/io.github.wivrn.wivrn"
    line="$line WINEDLLOVERRIDES=\"dxgi=n,b\""
    [ -e /proc/driver/nvidia/version ] && line="$line PROTON_ENABLE_NVAPI=1"
    line="$line %command% --launcher-skip"
    echo
    echo "Two steps are left:"
    echo "1. Steam > right-click The Witcher 3 > Properties > General > Launch Options, paste:"
    echo "     $line"
    echo "2. In the game: Options > Graphics > Anti-aliasing DLSS (Quality), sharpening, Reflex and frame"
    echo "   generation off."
    return 0
}

basic_uninstall() {
    local game rel
    [ -f "$W3VR_STATE/basic-install-files.txt" ] || { echo "Nothing to remove (no basic install found)."; return 0; }
    game=$(cat "$W3VR_STATE/basic-install-game.txt")
    while IFS= read -r rel; do
        [ -n "$rel" ] || continue
        rm -f -- "${game:?}/$rel"
        # Remove folders the mod created once they are empty (never above the game folder).
        (cd "$game" && rmdir -p --ignore-fail-on-non-empty -- "$(dirname -- "$rel")" 2>/dev/null)
    done < "$W3VR_STATE/basic-install-files.txt"
    rm -f -- "$W3VR_STATE/basic-install-files.txt" "$W3VR_STATE/basic-install-game.txt"
    echo "Mod files removed. Clear the Steam launch options by hand (Steam > The Witcher 3 > Properties)."
    echo "Your saves were not touched."
    return 0
}
