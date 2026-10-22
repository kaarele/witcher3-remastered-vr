#!/usr/bin/env python3
"""w3vr-linux: Linux setup tool for the Witcher 3 VR mod (witcher3-vr) on Proton.

It replaces Witcher3VRLauncher.exe on Linux. That launcher only stages dxgi.dll, writes witcher3vr.ini and
dx12user.settings, registers the HUD editor and starts the game. This tool does the same file work, plus:
  - finds Steam (native, Steam Deck, Flatpak) and every Steam library
  - checks that the game is a DX12 build the mod supports: Patch 4.04 (Steam beta "next-gen") or
    Remastered 5.01 (current Steam build); each gets its own tested preset
  - installs a release zip, after checking it against the release's SHA-256 file
  - applies tested VR presets
  - backs up saves and settings
  - uninstalls cleanly

Standard library only (works on SteamOS). Close the game before running any command that writes files.

Commands:
  status                       show what was found and the current VR config
  backup-saves                 copy the save folder to ~/w3vr-backups/<timestamp>/
  install ZIP [--sha256 FILE]  install a release zip into the game folder
  configure [options]          write witcher3vr.ini + dx12user.settings + HUD editor registration
  launch-options               print the Steam launch options for your OpenXR runtime
  wivrn-shortcut [--autostart 0|1]  show the game in WiVRn's in-headset app launcher
  uninstall [--restore-settings]  remove the mod files this tool installed
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
import zipfile

APP_ID = '292030'
TOOL_DIR = os.path.dirname(os.path.abspath(__file__))
# Inside the repository the launcher's own templates are used; a standalone copy ships them in templates/.
TEMPLATES = (os.path.join(TOOL_DIR, '..', 'launcher')
             if os.path.exists(os.path.join(TOOL_DIR, '..', 'launcher', 'witcher3vr.default.ini'))
             else os.path.join(TOOL_DIR, 'templates'))
EXTRA_MODS = os.path.join(TOOL_DIR, 'extra-mods')
STATE_DIR = os.path.join(os.environ.get('XDG_DATA_HOME', os.path.expanduser('~/.local/share')), 'w3vr-linux')
BACKUP_ROOT = os.path.expanduser('~/w3vr-backups')

# witcher3.exe (bin/x64_dx12) builds the mod supports, identified like the mod does (src/game_build.h): PE header
# TimeDateStamp and SizeOfImage. Patch 4.04 (Steam beta "next-gen") uses fixed engine addresses; Remastered 5.01 uses
# signature-checked per-function hooks.
BUILDS = {
    (0x664FB0DC, 0x0612E000): ('404', '4.04 (Steam next-gen)'),
    (0x6AC555A3, 0x06402000): ('501', 'Remastered 5.01'),
}
# On 5.01 the state classes need their internal names in @wrapMethod; the repository keeps that variant here.
REMASTER_SUPPORT = os.path.join(TOOL_DIR, '..', 'support', 'remaster')

# Launcher mode table (launcher/config.cpp kModes): openxr mode, dual render, AER, temporal backend, AAMode, AllowDLSS
MODES = {
    'mono_noaa': (1, False, False, 'none', 0, False),
    'mono_taau': (1, False, False, 'taau', 3, False),
    'mono_dlss': (1, False, False, 'dlss', 6, True),
    'aer_taau': (3, True, True, 'taau', 3, False),
    'aer_dlss': (3, True, True, 'dlss', 6, True),
    'stereo_noaa': (3, True, False, 'none', 0, False),
    'stereo_taau': (3, True, False, 'taau', 3, False),
    'stereo_dlss': (3, True, False, 'dlss', 6, True),
}
DIAGNOSTIC_KEYS = ['logging_enabled', 'runtime_diagnostics', 'taau_drop_diagnostics', 'cinema_camera_diagnostics',
                   'first_person_state_diagnostics', 'first_person_aim_diagnostics', 'world_marker_diagnostics']
HUD_CONTEXTS = ['Boat', 'BoatPassenger', 'Combat', 'Combat_Replacer_Ciri', 'Diving', 'Exploration',
                'Exploration_Replacer_Ciri', 'Horse', 'Horse_Replacer_Ciri', 'JumpClimb', 'Scene', 'Swimming']
HUD_EDITOR_BINDINGS = [('IK_Insert', 'W3VRHudEditorToggle'), ('IK_Q', 'W3VRHudEditorPrevious'),
                       ('IK_E', 'W3VRHudEditorNext'), ('IK_Left', 'W3VRHudEditorMoveLeft'),
                       ('IK_Right', 'W3VRHudEditorMoveRight'), ('IK_Up', 'W3VRHudEditorMoveUp'),
                       ('IK_Down', 'W3VRHudEditorMoveDown'), ('IK_MouseZ', 'W3VRHudEditorScale'),
                       ('IK_R', 'W3VRHudEditorResetCurrent'), ('IK_X', 'W3VRHudEditorResetProfile'),
                       ('IK_F7', 'W3VRHudEditorProfile')]
HUD_XML_LINE = b'modWitcher3VRHUDEditor.xml;'
# 5.01 keys tested in the headset for 0.1.3 (configure adds them when missing).
REMASTER_DEFAULTS = (
    ('openxr', 'world_marker_depth', '1'),
    ('engine', 'remaster_pair_jitter', '1'),
    ('engine', 'remaster_pair_noise', '1'),
    ('engine', 'remaster_pair_previous_transforms', '1'),
    ('engine', 'dlss_mip_bias_offset', '0.5'),
    ('engine', 'async_anim_tick_visible', '1'),
)
DLSS_QUALITY_NAMES = {0: 'DLAA', 1: 'Quality', 2: 'Balanced', 3: 'Performance', 4: 'Ultra Performance'}
# Remastered 5.01: the game runs DLSS itself (Streamline 2; the mod gives each eye its own DLSS history) and has
# no TAAU. "none" uses FXAA. "fsr" is the game's FSR (FSR 4 on RDNA4, the older FSR elsewhere, any GPU); in Stereo
# the mod gives each eye its own FSR context ([engine] remaster_fsr_per_eye, experimental). AER is not available
# there. Values: openxr mode, dual render, AER, backend, AAMode, AllowDLSS.
MODES_501 = {
    'mono_noaa': (1, False, False, 'none', 1, False),
    'mono_dlss': (1, False, False, 'dlss', 6, True),
    'mono_fsr': (1, False, False, 'none', 5, False),
    'stereo_noaa': (3, True, False, 'none', 1, False),
    'stereo_dlss': (3, True, False, 'dlss', 6, True),
    'stereo_fsr': (3, True, False, 'none', 5, False),
}


def die(msg):
    print('error: ' + msg, file=sys.stderr)
    sys.exit(1)


# ----------------------------------------------------------------------------------------------- ini editing
class Ini:
    """Line-preserving ini editor that keeps the source newline style."""

    def __init__(self, text):
        self.nl = '\r\n' if '\r\n' in text else '\n'
        self.lines = text.replace('\r\n', '\n').split('\n')
        if self.lines and self.lines[-1] == '':
            self.lines.pop()

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as f:
            return cls(f.read().decode('utf-8-sig'))

    def _bounds(self, section):
        for i, line in enumerate(self.lines):
            t = line.strip()
            if t.startswith('[') and t.endswith(']') and t[1:-1].strip().lower() == section.lower():
                end = len(self.lines)
                for j in range(i + 1, len(self.lines)):
                    u = self.lines[j].strip()
                    if u.startswith('[') and u.endswith(']'):
                        end = j
                        break
                return i, end
        return None

    def entries(self, section):
        b = self._bounds(section)
        if not b:
            return []
        return [tuple(l.split('=', 1)) for l in self.lines[b[0] + 1:b[1]]
                if '=' in l and not l.lstrip().startswith((';', '#'))]

    def get(self, section, key, default=None):
        for k, v in self.entries(section):
            if k.strip().lower() == key.lower():
                return v.strip()
        return default

    def set(self, section, key, value):
        b = self._bounds(section)
        if not b:
            if self.lines and self.lines[-1].strip():
                self.lines.append('')
            self.lines += ['[%s]' % section, '%s=%s' % (key, value)]
            return
        for i in range(b[0] + 1, b[1]):
            line = self.lines[i]
            if ('=' in line and not line.lstrip().startswith((';', '#'))
                    and line.split('=', 1)[0].strip().lower() == key.lower()):
                self.lines[i] = '%s=%s' % (key, value)
                return
        self.add_line(section, '%s=%s' % (key, value))

    def has_action(self, section, action):
        b = self._bounds(section)
        return bool(b) and any(re.search(r'Action=%s[,)]' % re.escape(action), l) for l in self.lines[b[0] + 1:b[1]])

    def add_line(self, section, line):
        b = self._bounds(section)
        if not b:
            if self.lines and self.lines[-1].strip():
                self.lines.append('')
            self.lines += ['[%s]' % section, line]
            return
        ins = b[1]
        while ins > b[0] + 1 and not self.lines[ins - 1].strip():
            ins -= 1
        self.lines.insert(ins, line)

    def text(self):
        return self.nl.join(self.lines) + self.nl


def write_with_backup(path, data, dry_run=False):
    """Like the launcher's AtomicWriteWithBackup: <file>.w3vr.bak keeps the previous version."""
    if os.path.exists(path):
        with open(path, 'rb') as f:
            if f.read() == data:
                print('  unchanged  %s' % path)
                return False
    if dry_run:
        print('  would write %s' % path)
        return True
    if os.path.exists(path):
        shutil.copy2(path, path + '.w3vr.bak')
    tmp = path + '.w3vr.tmp'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, path)
    print('  wrote      %s' % path)
    return True


# ---------------------------------------------------------------------------------------------- discovery
def parse_vdf_paths(text):
    return [m.group(1).replace('\\\\', '\\') for m in re.finditer(r'"path"\s+"([^"]*)"', text)]


def steam_roots():
    candidates = [
        os.path.expanduser('~/.local/share/Steam'),
        os.path.expanduser('~/.steam/steam'),
        os.path.expanduser('~/.steam/root'),
        os.path.expanduser('~/.var/app/com.valvesoftware.Steam/.local/share/Steam'),
        os.path.expanduser('~/.var/app/com.valvesoftware.Steam/data/Steam'),
    ]
    seen, roots = set(), []
    for c in candidates:
        if os.path.isdir(os.path.join(c, 'steamapps')):
            real = os.path.realpath(c)
            if real not in seen:
                seen.add(real)
                roots.append(real)
    return roots


def find_game():
    """Return a dict describing the Witcher 3 install, or None."""
    for root in steam_roots():
        libraries = [root]
        vdf = os.path.join(root, 'steamapps', 'libraryfolders.vdf')
        if os.path.exists(vdf):
            with open(vdf, encoding='utf-8', errors='replace') as f:
                libraries += parse_vdf_paths(f.read())
        for lib in dict.fromkeys(os.path.realpath(l) for l in libraries):
            manifest = os.path.join(lib, 'steamapps', 'appmanifest_%s.acf' % APP_ID)
            if not os.path.exists(manifest):
                continue
            with open(manifest, encoding='utf-8', errors='replace') as f:
                acf = f.read()
            installdir = re.search(r'"installdir"\s+"([^"]*)"', acf)
            game = os.path.join(lib, 'steamapps', 'common', installdir.group(1) if installdir else 'The Witcher 3')
            prefix = os.path.join(lib, 'steamapps', 'compatdata', APP_ID, 'pfx')
            def field(name):
                m = re.search(r'"%s"\s+"([^"]*)"' % name, acf)
                return m.group(1) if m else ''
            return {
                'steam_root': root,
                'library': lib,
                'manifest': manifest,
                'game': game,
                'x64': os.path.join(game, 'bin', 'x64_dx12'),
                'prefix': prefix,
                'docs': os.path.join(prefix, 'drive_c', 'users', 'steamuser', 'Documents', 'The Witcher 3'),
                'buildid': field('buildid'),
                'betakey': field('BetaKey'),
                'state_flags': field('StateFlags'),
                'flatpak_steam': '/.var/app/com.valvesoftware.Steam/' in root,
            }
    return None


def require_game():
    g = find_game()
    if not g:
        die('The Witcher 3 (app %s) was not found in any Steam library.' % APP_ID)
    if not os.path.isdir(g['x64']):
        die('Game folder looks incomplete: %s is missing.' % g['x64'])
    return g


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def exe_identity(path):
    """(TimeDateStamp, SizeOfImage) from a PE file's headers, or None."""
    try:
        with open(path, 'rb') as f:
            head = f.read(4096)
        pe = int.from_bytes(head[0x3C:0x40], 'little')
        if head[pe:pe + 4] != b'PE\0\0':
            return None
        stamp = int.from_bytes(head[pe + 8:pe + 12], 'little')
        size_of_image = int.from_bytes(head[pe + 24 + 56:pe + 24 + 60], 'little')
        return stamp, size_of_image
    except (OSError, ValueError):
        return None


def build_info(g):
    """('404' | '501' | None, description) for the installed bin/x64_dx12/witcher3.exe."""
    exe = os.path.join(g['x64'], 'witcher3.exe')
    if not os.path.exists(exe):
        return None, 'missing'
    identity = exe_identity(exe)
    if identity in BUILDS:
        return BUILDS[identity]
    return None, 'UNKNOWN build (TimeDateStamp 0x%08X, SizeOfImage 0x%08X)' % identity if identity else 'unreadable'


def game_build(g):
    """'404', '501' or None for the installed bin/x64_dx12/witcher3.exe."""
    return build_info(g)[0]


STATE_BRIDGE = 'mods/modWitcher3VRStateBridge/content/scripts/local/witcher3vr/first_person_state_bridge.ws'
# Remastered 5.01 only resolves the player states by their internal class names in @wrapMethod.
STATE_BRIDGE_NAMES = [('HorseRiding', 'CR4PlayerStateHorseRiding'), ('Sailing', 'CR4PlayerStateSailing'),
                      ('SailingPassive', 'CR4PlayerStateSailingPassive')]


def fix_state_bridge(g, build, dry_run=False):
    """Make the installed state-bridge script use the state names the detected build compiles."""
    path = os.path.join(g['game'], STATE_BRIDGE)
    if build not in ('404', '501') or not os.path.exists(path):
        return
    with open(path, 'rb') as f:
        data = f.read()
    fixed = data
    for short, internal in STATE_BRIDGE_NAMES:
        old, new = (short, internal) if build == '501' else (internal, short)
        fixed = fixed.replace(b'@wrapMethod(%s)' % old.encode(), b'@wrapMethod(%s)' % new.encode())
    if fixed != data:
        print('State bridge script adapted for %s:' % ('Remastered 5.01' if build == '501' else '4.04'))
        write_with_backup(path, fixed, dry_run)


def game_running():
    for pid in os.listdir('/proc'):
        if pid.isdigit():
            # Match Wine's argv[0] (backslash path). The comm name is useless (Wine renames it to the thread name,
            # e.g. "MainThread"), and shells that merely mention the path use forward slashes.
            try:
                with open('/proc/%s/cmdline' % pid, 'rb') as f:
                    argv0 = f.read().split(b'\0', 1)[0].lower()
            except OSError:
                continue
            if argv0.endswith((b'\\bin\\x64_dx12\\witcher3.exe', b'\\bin\\x64\\witcher3.exe')):
                return True
    return False


def require_game_closed():
    if game_running():
        die('The Witcher 3 is running. Quit the game first (it rewrites its settings on exit).')


def openxr_runtime():
    """Describe the host OpenXR runtime as (kind, detail)."""
    paths = [os.path.join(os.environ.get('XDG_CONFIG_HOME', os.path.expanduser('~/.config')), 'openxr/1/active_runtime.json'),
             '/etc/xdg/openxr/1/active_runtime.json']
    for p in paths:
        if os.path.exists(p):
            target = os.path.realpath(p)
            kind = 'wivrn-flatpak' if '/flatpak/app/io.github.wivrn.wivrn/' in target else \
                'wivrn' if 'wivrn' in target.lower() else 'monado' if 'monado' in target.lower() else \
                'steamvr' if 'steamvr' in target.lower() else 'other'
            return kind, target
    if os.path.isdir('/var/lib/flatpak/app/io.github.wivrn.wivrn') or \
            os.path.isdir(os.path.expanduser('~/.local/share/flatpak/app/io.github.wivrn.wivrn')):
        return 'wivrn-flatpak-idle', 'WiVRn Flatpak installed; no active runtime (connect the headset first)'
    return None, 'no active OpenXR runtime found'


def find_steamvr():
    """Path of SteamVR's Linux OpenXR manifest (steamxr_linux64.json) in any Steam library, or None."""
    for root in steam_roots():
        libraries = [root]
        vdf = os.path.join(root, 'steamapps', 'libraryfolders.vdf')
        if os.path.exists(vdf):
            with open(vdf, encoding='utf-8', errors='replace') as f:
                libraries += parse_vdf_paths(f.read())
        for lib in dict.fromkeys(os.path.realpath(l) for l in libraries):
            manifest = os.path.join(lib, 'steamapps', 'common', 'SteamVR', 'steamxr_linux64.json')
            if os.path.exists(manifest):
                return manifest
    return None


def nvidia_gpu():
    return os.path.exists('/proc/driver/nvidia/version')


# ------------------------------------------------------------------------------------------------ commands
def cmd_status(args):
    g = find_game()
    if not g:
        die('The Witcher 3 was not found in any Steam library.')
    exe = os.path.join(g['x64'], 'witcher3.exe')
    build_description = build_info(g)[1]
    print('Steam root      %s%s' % (g['steam_root'], ' (Flatpak Steam)' if g['flatpak_steam'] else ''))
    print('Game folder     %s' % g['game'])
    print('Branch          %s (buildid %s, StateFlags %s)' % (g['betakey'] or 'public', g['buildid'], g['state_flags']))
    print('DX12 exe        %s' % build_description)
    print('Prefix docs     %s%s' % (g['docs'], '' if os.path.isdir(g['docs']) else '  (missing: run the game once)'))
    dxgi = os.path.join(g['x64'], 'dxgi.dll')
    ref = os.path.join(g['x64'], 'witcher3vr-mod-reference', 'dxgi.dll')
    if os.path.exists(dxgi):
        same = os.path.exists(ref) and sha256_file(dxgi) == sha256_file(ref)
        print('Mod             installed (dxgi.dll %s)' % ('matches release reference' if same else 'differs from reference'))
    else:
        print('Mod             not active (no bin/x64_dx12/dxgi.dll)')
    kind, detail = openxr_runtime()
    print('OpenXR runtime  %s' % (detail if kind else detail))
    steamvr = find_steamvr()
    print('SteamVR         %s' % (steamvr if steamvr else 'not installed'))
    print('GPU             %s' % ('NVIDIA (DLSS available)' if nvidia_gpu() else 'non-NVIDIA (no DLSS: FXAA on 5.01, TAAU on 4.04)'))
    print('Game running    %s' % ('yes' if game_running() else 'no'))
    ini_path = os.path.join(g['x64'], 'witcher3vr.ini')
    if os.path.exists(ini_path):
        ini = Ini.load(ini_path)
        mode = ini.get('openxr', 'mode')
        aer = ini.get('openxr', 'mode3_aer_presentation')
        print('witcher3vr.ini  mode=%s%s backend=%s render=%sx%s world_detail_range=%s hud_scale=%s/%s hud_locked=%s '
              'camera_mode=%s near_offset=%s diagnostics=%s' % (
                  mode, ' (AER)' if mode == '3' and aer == '1' else ' (strict Stereo)' if mode == '3' else ' (Mono)',
                  ini.get('engine', 'temporal_backend'), ini.get('openxr', 'render_width'), ini.get('openxr', 'render_height'),
                  ini.get('openxr', 'world_detail_range'), ini.get('openxr', 'hud_horizontal_scale'),
                  ini.get('openxr', 'hud_vertical_scale'), ini.get('openxr', 'hud_controller_locked'),
                  ini.get('engine', 'camera_mode'), ini.get('engine', 'close_camera_offset'),
                  ini.get('debug', 'logging_enabled')))
    settings = os.path.join(g['docs'], 'dx12user.settings')
    if os.path.exists(settings):
        s = Ini.load(settings)
        q = s.get('PostProcess', 'DLSSQuality')
        print('dx12user        Resolution=%s AAMode=%s DLSSQuality=%s AllowDLSS=%s uiFrameScale=%s/%s' % (
            s.get('Viewport', 'Resolution'), s.get('PostProcess', 'AAMode'), q, s.get('Rendering', 'AllowDLSS'),
            s.get('Hidden', 'uiHorizontalFrameScale'), s.get('Hidden', 'uiVerticalFrameScale')))
    state = load_state(g)
    if state:
        print('Installed by    w3vr-linux %s from %s (%d files)' % (state.get('installed_at', '?'), state.get('source', '?'),
                                                                    len(state.get('files', []))))


def cmd_backup_saves(args):
    g = require_game()
    dest = backup_saves(g)
    print('Saves backed up to %s' % dest)


def backup_saves(g):
    src = g['docs']
    if not os.path.isdir(src):
        die('No Witcher 3 documents folder yet (%s). Start the game once.' % src)
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    dest = os.path.join(BACKUP_ROOT, stamp)
    os.makedirs(BACKUP_ROOT, exist_ok=True)
    shutil.copytree(src, os.path.join(dest, 'The Witcher 3'))
    # Verify the copy byte for byte.
    for base, _, files in os.walk(src):
        for name in files:
            a = os.path.join(base, name)
            b = os.path.join(dest, 'The Witcher 3', os.path.relpath(a, src))
            if sha256_file(a) != sha256_file(b):
                die('backup verification failed for %s' % a)
    return dest


def state_path(g):
    key = hashlib.sha1(os.path.realpath(g['game']).encode()).hexdigest()[:12]
    return os.path.join(STATE_DIR, 'install-%s.json' % key)


def load_state(g):
    p = state_path(g)
    if os.path.exists(p):
        with open(p) as f:
            return json.load(f)
    return None


def save_state(g, state):
    os.makedirs(STATE_DIR, exist_ok=True)
    with open(state_path(g), 'w') as f:
        json.dump(state, f, indent=1)


def parse_release_sha256(path):
    """The release SHA256 file is key=HEX lines (main_archive.sha256=...), sometimes with stray bytes first."""
    with open(path, 'rb') as f:
        text = f.read().decode('utf-8', errors='replace')
    return {m.group(1): m.group(2).lower() for m in re.finditer(r'([A-Za-z0-9_.]+)=([0-9A-Fa-f]{64})', text)}


def safe_member_name(name):
    name = name.replace('\\', '/')
    parts = [p for p in name.split('/') if p not in ('', '.')]
    if not parts or any(p == '..' for p in parts) or name.startswith('/'):
        return None
    return '/'.join(parts)


def cmd_install(args):
    g = require_game()
    require_game_closed()
    zip_path = os.path.abspath(args.zip)
    if not zipfile.is_zipfile(zip_path):
        die('%s is not a zip file' % zip_path)
    if args.sha256:
        sums = parse_release_sha256(args.sha256)
        expected = sums.get('main_archive.sha256')
        if not expected:
            die('no main_archive.sha256 entry in %s' % args.sha256)
        actual = sha256_file(zip_path)
        if actual != expected:
            die('SHA-256 mismatch: %s, expected %s' % (actual, expected))
        print('SHA-256 OK (%s)' % actual)
    else:
        print('warning: no --sha256 file given; the archive is not verified')
    exe = os.path.join(g['x64'], 'witcher3.exe')
    build, build_description = build_info(g)
    if build is None:
        msg = ('bin/x64_dx12/witcher3.exe is not a build this mod supports (%s): Patch 4.04 (Steam beta '
               '"next-gen") or Remastered 5.01.' % build_description)
        if not args.force:
            die(msg + ' Use --force to install anyway.')
        print('warning: ' + msg)
    else:
        print('Game build: %s' % build_description)
    if os.path.isdir(g['docs']):
        print('Backing up saves and settings first ...')
        print('  saves -> %s' % backup_saves(g))

    state = load_state(g) or {'files': [], 'backups': {}}
    installed = set(state['files'])
    with tempfile.TemporaryDirectory(prefix='w3vr-linux-') as tmp, zipfile.ZipFile(zip_path) as z:
        members = []
        for info in z.infolist():
            name = safe_member_name(info.filename)
            if name is None:
                die('unsafe path in archive: %r' % info.filename)
            if info.is_dir() or info.filename.endswith(('/', '\\')):
                continue
            members.append((info, name))
        for info, name in members:
            out = os.path.join(tmp, name)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(info) as src, open(out, 'wb') as dst:
                shutil.copyfileobj(src, dst)
        top = {name.split('/')[0] for _, name in members}
        unexpected = top - {'bin', 'mods', 'dlc', 'Witcher3VR'}
        if unexpected:
            die('archive has unexpected top-level entries %s; is this a witcher3-vr release zip?' % sorted(unexpected))
        for _, name in members:
            dest = os.path.join(g['game'], name)
            if os.path.exists(dest) and name not in installed:
                backup = os.path.join(STATE_DIR, 'overwritten', hashlib.sha1(g['game'].encode()).hexdigest()[:12], name)
                os.makedirs(os.path.dirname(backup), exist_ok=True)
                shutil.copy2(dest, backup)
                state['backups'][name] = backup
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copyfile(os.path.join(tmp, name), dest)
            installed.add(name)
    ref = os.path.join(g['x64'], 'witcher3vr-mod-reference', 'dxgi.dll')
    if not os.path.exists(ref):
        die('release has no bin/x64_dx12/witcher3vr-mod-reference/dxgi.dll')
    if args.sha256:
        sums = parse_release_sha256(args.sha256)
        if sums.get('dll.sha256') and sha256_file(ref) != sums['dll.sha256']:
            die('dxgi.dll does not match dll.sha256 from the release file')
    # Activate the renderer exactly like the launcher: a plain copy of the reference DLL, or a custom build.
    shutil.copyfile(os.path.abspath(args.dll) if args.dll else ref, os.path.join(g['x64'], 'dxgi.dll'))
    installed.add('bin/x64_dx12/dxgi.dll')
    if args.dll:
        print('Activated custom dxgi.dll %s (sha256 %s)' % (args.dll, sha256_file(args.dll)))
    # Linux extras: HUD frame-scale guard script (keeps the VR HUD scale when the vanilla Rescale HUD screen opens).
    if os.path.isdir(EXTRA_MODS):
        for base, _, files in os.walk(EXTRA_MODS):
            for name in files:
                rel = os.path.relpath(os.path.join(base, name), EXTRA_MODS)
                dest_rel = 'mods/' + rel.replace(os.sep, '/')
                dest = os.path.join(g['game'], dest_rel)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                shutil.copyfile(os.path.join(base, name), dest)
                installed.add(dest_rel)
    # Remastered 5.01: the state bridge needs the internal state class names (CR4PlayerStateHorseRiding, ...).
    fix_state_bridge(g, build)
    if build == '501' and os.path.isdir(REMASTER_SUPPORT):
        for base, _, files in os.walk(REMASTER_SUPPORT):
            for name in files:
                rel = os.path.relpath(os.path.join(base, name), REMASTER_SUPPORT)
                dest_rel = 'mods/' + rel.replace(os.sep, '/')
                dest = os.path.join(g['game'], dest_rel)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                shutil.copyfile(os.path.join(base, name), dest)
                installed.add(dest_rel)
        print('Installed the Remastered 5.01 script variants from %s' % os.path.normpath(REMASTER_SUPPORT))
    state.update({'files': sorted(installed), 'source': zip_path,
                  'installed_at': datetime.datetime.now().isoformat(timespec='seconds'), 'game': g['game']})
    save_state(g, state)
    print('Installed %d files and activated bin/x64_dx12/dxgi.dll.' % len(installed))
    print('Next: w3vr-linux.py configure   (then set the launch options: w3vr-linux.py launch-options)')


def cmd_configure(args):
    g = require_game()
    require_game_closed()
    x64, docs = g['x64'], g['docs']
    # A release zip extracted into the game folder puts dxgi.dll in place directly; `install` (from an upstream
    # release zip) also keeps the reference copy in witcher3vr-mod-reference.
    if not os.path.exists(os.path.join(x64, 'dxgi.dll')) and \
            not os.path.exists(os.path.join(x64, 'witcher3vr-mod-reference', 'dxgi.dll')):
        die('mod not installed: bin/x64_dx12/dxgi.dll is missing (extract the release zip into the game folder first)')
    settings_path = os.path.join(docs, 'dx12user.settings')
    if not os.path.exists(settings_path):
        die('%s is missing. Start the game once without VR, reach the main menu, quit, then run configure.' % settings_path)

    build, build_description = build_info(g)
    remaster = build == '501'
    if build is None:
        if not args.force:
            die('witcher3.exe is not a build this tool knows (%s). A game update may need a new mod build; '
                'use --force to apply the 4.04 preset anyway.' % build_description)
        print('warning: unknown witcher3.exe build; using the 4.04 preset')
    fix_state_bridge(g, build, args.dry_run)
    aa = args.aa or ('dlss' if nvidia_gpu() else ('none' if remaster else 'taau'))
    mode_name = '%s_%s' % (args.mode, {'none': 'noaa'}.get(aa, aa))
    modes = MODES_501 if remaster else MODES
    if mode_name not in modes:
        die('unsupported combination on %s: --mode %s --aa %s (%s)' % (
            'Remastered 5.01' if remaster else '4.04', args.mode, aa,
            'stereo/mono with dlss, fsr or none' if remaster else 'AER needs taau or dlss; fsr is 5.01 only'))
    openxr_mode, dual, aer, backend, aa_mode, allow_dlss = modes[mode_name]
    from_runtime = args.res.lower() == 'runtime'
    # With --res runtime the mod asks the OpenXR runtime for its recommended per-eye size at every start
    # ([openxr] render_size_from_runtime=1); 2476x2594 is only the fallback if that query fails.
    w, h = (2476, 2594) if from_runtime else (int(v) for v in args.res.lower().split('x'))
    if from_runtime:
        print("note: the render size follows the OpenXR runtime's recommendation (SteamVR: its resolution setting)")
    elif args.res.lower() == '2476x2594':
        print("note: using the Quest 3 default render size 2476x2594; for other headsets pass --res WIDTHxHEIGHT "
              "with your runtime's recommended per-eye size")
    quality = args.dlss_quality
    if backend == 'dlss' and quality != 0 and abs(w - h) < 48 and not remaster:
        die('scaled DLSS needs |width - height| >= 48 (the launcher refuses near-square sizes)')

    # witcher3vr.ini: start from the launcher's schema-19 template, keep existing user tuning.
    ini_path = os.path.join(x64, 'witcher3vr.ini')
    ini = Ini.load(ini_path if os.path.exists(ini_path) else os.path.join(TEMPLATES, 'witcher3vr.default.ini'))
    ini.set('openxr', 'enabled', '1')
    ini.set('openxr', 'mode', str(openxr_mode))
    ini.set('openxr', 'mode3_aer_presentation', '1' if aer else '0')
    ini.set('openxr', 'resolution_auto', '0')
    ini.set('openxr', 'render_width', str(w))
    ini.set('openxr', 'render_height', str(h))
    ini.set('openxr', 'render_size_from_runtime', '1' if from_runtime else '0')
    # 5.01 never uses the 4.04 native_stereo path (its centre block is the DLSS jitter there); it gets
    # per-eye frusta through the engine's NDC projection offset instead (remaster_asymmetric_ndc), so the
    # whole render target is the eye's field of view rather than a crop of a symmetric envelope.
    ini.set('openxr', 'native_stereo', '0' if remaster else '1')
    if remaster:
        ini.set('engine', 'remaster_asymmetric_ndc', '0' if args.symmetric else '1')
        # Per-eye FSR only acts while the game uses FSR, so it is safe to leave on with DLSS or FXAA.
        ini.set('engine', 'remaster_fsr_per_eye', '1')
        # Tested 0.1.3 defaults; a value the user already set is kept.
        for section, key, value in REMASTER_DEFAULTS:
            if ini.get(section, key) in (None, ''):
                ini.set(section, key, value)
    ini.set('openxr', 'world_detail_range', '%.3f' % args.world_detail)
    ini.set('openxr', 'hud_horizontal_scale', '%.3f' % args.hud_frame)
    ini.set('openxr', 'hud_vertical_scale', '%.3f' % args.hud_frame)
    ini.set('openxr', 'hud_size', '%.3f' % args.hud_size)
    # Keep the HUD plane at about 1 m when it is resized: the plane distance follows shift * size.
    ini.set('openxr', 'hud_stereo_shift_px', str(int(round(-36.0 / args.hud_size))))
    ini.set('openxr', 'hud_controller_locked', '1' if args.hud_locked else '0')
    ini.set('engine', 'dual_render_probe', '1' if dual else '0')
    ini.set('engine', 'dual_render_start', '1' if dual else '0')
    ini.set('engine', 'temporal_backend', backend)
    ini.set('engine', 'dlss_dlaa', '1' if backend == 'dlss' and quality == 0 and not remaster else '0')
    ini.set('engine', 'menu_state_probe', '1')
    if args.near_view is None:
        ini.set('engine', 'camera_mode', '0')
    else:
        ini.set('engine', 'camera_mode', '1')
        ini.set('engine', 'close_camera_offset', '%.3f' % args.near_view)
    for key in DIAGNOSTIC_KEYS:
        ini.set('debug', key, '1' if args.diagnostics else '0')
    ini.set('debug', 'route_flight_recorder', '1' if args.diagnostics else '0')
    ini.set('debug', 'pipeline_flight_recorder', '0')
    for key in ['enabled', 'scan_periodic', 'scan_unmap', 'cbv_probe', 'active_nudge', 'orbit_probe', 'copy_probe',
                'stereo_probe', 'geometry_shift']:
        ini.set('reverse', key, '0')
    for key in ['view_probe', 'frame_builder_probe', 'gameplay_entry_probe', 'scene_enqueue_probe',
                'view_factory_probe', 'frame_factory_probe']:
        ini.set('engine', key, '0')
    # Test-harness keys must never reach a headset session (openxr_loader would replace the real runtime).
    for key in ('openxr_loader', 'input_script'):
        if ini.get('test', key):
            ini.set('test', key, '')
    ini.set('launcher', 'integration_mode', 'off')
    ini.set('meta', 'config_version', '19')
    print('witcher3vr.ini (%s, %dx%d):' % (mode_name, w, h))
    write_with_backup(ini_path, ini.text().encode('ascii'), args.dry_run)

    # dx12user.settings: "Configure Settings for VR" once (template + the user's [Galaxy]/[Localization]), then
    # the launcher's per-save keys.
    current = Ini.load(settings_path)
    original = settings_path + '.w3vr-original.bak'
    if remaster:
        # The VR graphics template is a 4.04 file; on 5.01 only the keys below are changed.
        if not os.path.exists(original) and not args.dry_run:
            shutil.copy2(settings_path, original)
            print('  backup     %s' % original)
        doc = current
    elif args.reset_settings or not os.path.exists(original):
        if not os.path.exists(original) and not args.dry_run:
            shutil.copy2(settings_path, original)
            print('  backup     %s' % original)
        doc = Ini.load(os.path.join(TEMPLATES, 'vr_dx12user.settings'))
        for section in ('Galaxy', 'Localization'):
            for k, v in current.entries(section):
                doc.set(section, k.strip(), v.strip())
        # Keep the player's own difficulty, which the template would change.
        if current.get('Gameplay', 'Difficulty') is not None:
            doc.set('Gameplay', 'Difficulty', current.get('Gameplay', 'Difficulty'))
    else:
        doc = current
    doc.set('W3VRSettings', 'CameraFollowPolicy', '1')
    doc.set('Gameplay', 'AutoCameraCenter', 'false')
    doc.set('W3VRSettings', 'HideStaticHudOutsideCombat', 'false')
    doc.set('Viewport', 'Resolution', '"%dx%d"' % (w, h))
    doc.set('PostProcess', 'AAMode', str(aa_mode))
    if remaster:
        # 5.01 DLSSQuality: 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance, 5 DLAA (0 = Auto).
        doc.set('PostProcess', 'DLSSQuality', str(5 if quality == 0 else max(1, min(4, quality))))
        # Frame generation, Reflex and Ray Reconstruction off: the mod presents its own eye pairs. Sharpening
        # off: it adds shimmer after the stream's resampling.
        doc.set('Rendering', 'AllowDLSSG', 'false')
        doc.set('Rendering', 'AllowReflex', 'false')
        doc.set('Rendering', 'AllowDLSSRR', 'false')
        doc.set('PostProcess', 'EnableDLSSRR', 'false')
        doc.set('PostProcess', 'SharpenAmount', '0')
        doc.set('Rendering/TAA', 'EnableCAS', 'false')
        # Close exploration/combat cameras, as on 4.04. The 5.01 defaults (Modern / Dynamic) re-frame while
        # walking; with the game pitch locked for VR only the translation remains, and the camera snaps up to
        # ~1.3 m backwards and ~1.5 m up and glides back, which reads as zooming in and out (null-HMD traces).
        doc.set('Gameplay', 'EnableAlternateExplorationCamera', '1')
        doc.set('Gameplay', 'EnableAlternateCombatCamera', '1')
    else:
        doc.set('PostProcess', 'DLSSQuality', str(1 if quality == 0 else max(1, min(4, quality))))
    doc.set('Rendering', 'AllowDLSS', 'true' if allow_dlss else 'false')
    doc.set('DLC', 'DlcEnabled_movementinputfix',
            '1' if os.path.isdir(os.path.join(g['game'], 'dlc', 'dlcmovementinputfix')) else '0')
    doc.set('Hidden', 'uiHorizontalFrameScale', '%g' % args.hud_frame)
    doc.set('Hidden', 'uiVerticalFrameScale', '%g' % args.hud_frame)
    if aa == 'fsr':
        print('dx12user.settings (AAMode=%d, FSR; pick its quality in the game\'s graphics menu):' % aa_mode)
    else:
        print('dx12user.settings (AAMode=%d, DLSS %s):' % (aa_mode, DLSS_QUALITY_NAMES.get(quality, quality)))
    write_with_backup(settings_path, doc.text().encode('ascii'), args.dry_run)

    # HUD editor registration (launcher EnsureHudEditorReady).
    print('HUD editor registration:')
    filelist = os.path.join(g['game'], 'bin', 'config', 'r4game', 'user_config_matrix', 'pc', 'dx12filelist.txt')
    if os.path.exists(filelist):
        with open(filelist, 'rb') as f:
            data = f.read()
        if not any(l.strip() == HUD_XML_LINE for l in data.replace(b'\r\n', b'\n').split(b'\n')):
            nl = b'\r\n' if b'\r\n' in data else b'\n'
            if data and not data.endswith((b'\n', b'\r')):
                data += nl
            data += HUD_XML_LINE + nl
        write_with_backup(filelist, data, args.dry_run)
    else:
        # Remastered 5.01 has no filelist: it loads every XML in user_config_matrix/pc.
        print('  no dx12filelist.txt (Remastered loads every menu XML in user_config_matrix/pc)')
    input_path = os.path.join(docs, 'input.settings')
    if os.path.exists(input_path):
        inp = Ini.load(input_path)
        for ctx in HUD_CONTEXTS:
            if not inp.has_action(ctx, 'W3VRHudEditorToggle'):
                inp.add_line(ctx, 'IK_Insert=(Action=W3VRHudEditorToggle)')
            if not inp.has_action(ctx, 'W3VRHudEditorProfile'):
                inp.add_line(ctx, 'IK_F7=(Action=W3VRHudEditorProfile)')
        for key, action in HUD_EDITOR_BINDINGS:
            if not inp.has_action('W3VRHudEditor', action):
                inp.add_line('W3VRHudEditor', '%s=(Action=%s)' % (key, action))
        write_with_backup(input_path, inp.text().encode('utf-8'), args.dry_run)
    else:
        print('  input.settings not found yet; run configure again after the first game start')
    print('Done. Keep the headset connected to your OpenXR runtime BEFORE starting the game.')


def cmd_launch_options(args):
    kind, detail = openxr_runtime()
    steamvr = find_steamvr()
    runtime = getattr(args, 'runtime', 'auto')
    if runtime == 'auto':
        if kind == 'steamvr' or (kind is None and steamvr):
            runtime = 'steamvr'
        else:
            runtime = 'wivrn' if kind and kind.startswith('wivrn') else 'active'
    parts = ['PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1']
    if runtime == 'steamvr':
        if not steamvr:
            die('SteamVR is not installed in any Steam library (install it from Steam first)')
        # Pin SteamVR even when another runtime (WiVRn) registers itself as the active one.
        parts.append('XR_RUNTIME_JSON="%s"' % steamvr)
        detail = 'SteamVR (%s)' % steamvr
    elif kind and kind.startswith('wivrn-flatpak'):
        system = '/var/lib/flatpak/app/io.github.wivrn.wivrn'
        user = os.path.expanduser('~/.local/share/flatpak/app/io.github.wivrn.wivrn')
        parts.append('PRESSURE_VESSEL_FILESYSTEMS_RW=%s' % (user if os.path.isdir(user) and not os.path.isdir(system) else system))
    # The tested setup: load the mod's dxgi.dll, and NVAPI for DLSS on NVIDIA.
    parts.append('WINEDLLOVERRIDES="dxgi=n,b"')
    if nvidia_gpu():
        parts.append('PROTON_ENABLE_NVAPI=1')
    print('Runtime: %s' % detail)
    print('Steam > The Witcher 3 > Properties > General > Launch Options:')
    print()
    print('  %s %%command%% --launcher-skip' % ' '.join(parts))
    print()
    if runtime == 'steamvr':
        print('SteamVR: start SteamVR (and connect the headset, e.g. Steam Link on a Quest) before the game. '
              'For SteamVR\'s own resolution use `configure --res runtime`.')
    else:
        print('Prefer the exact string your runtime shows (e.g. the WiVRn dashboard), and keep %command% --launcher-skip.')


WIVRN_DESKTOP = os.path.join(os.environ.get('XDG_DATA_HOME', os.path.expanduser('~/.local/share')),
                             'applications', 'witcher3-vr-wivrn.desktop')


def wivrn_config_path():
    flatpak = os.path.expanduser('~/.var/app/io.github.wivrn.wivrn/config/wivrn/config.json')
    native = os.path.join(os.environ.get('XDG_CONFIG_HOME', os.path.expanduser('~/.config')), 'wivrn', 'config.json')
    return flatpak if os.path.exists(flatpak) or not os.path.exists(native) and os.path.isdir(os.path.dirname(flatpak)) else native


def cmd_wivrn_shortcut(args):
    """Show the game in WiVRn's in-headset app launcher (desktop entry with Categories X-WiVRn-VR)."""
    steam = 'flatpak run com.valvesoftware.Steam' if (find_game() or {}).get('flatpak_steam') else 'steam'
    entry = ('[Desktop Entry]\nType=Application\nName=The Witcher 3 VR\n'
             'Comment=The Witcher 3 with the witcher3-vr mod (Proton + WiVRn)\n'
             'Exec=%s steam://rungameid/%s\nIcon=steam_icon_%s\nTerminal=false\nCategories=Game;X-WiVRn-VR;\n'
             % (steam, APP_ID, APP_ID))
    if args.remove:
        if os.path.exists(WIVRN_DESKTOP):
            os.remove(WIVRN_DESKTOP)
            print('removed %s' % WIVRN_DESKTOP)
    else:
        os.makedirs(os.path.dirname(WIVRN_DESKTOP), exist_ok=True)
        write_with_backup(WIVRN_DESKTOP, entry.encode())
        print('The game now appears in the WiVRn launcher in the headset (shown while no VR app is running).')
    if args.autostart is not None:
        path = wivrn_config_path()
        cfg = {}
        if os.path.exists(path):
            with open(path) as f:
                cfg = json.load(f)
        if args.autostart:
            cfg['application'] = [steam.split()[0]] + steam.split()[1:] + ['steam://rungameid/%s' % APP_ID]
        else:
            cfg.pop('application', None)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        write_with_backup(path, (json.dumps(cfg, indent=2) + '\n').encode())
        print('WiVRn auto-start %s (restart the WiVRn server to apply).' % ('enabled' if args.autostart else 'disabled'))


def cmd_uninstall(args):
    g = require_game()
    require_game_closed()
    state = load_state(g)
    removed = 0
    files = state['files'] if state else ['bin/x64_dx12/dxgi.dll']
    for name in files:
        p = os.path.join(g['game'], name)
        if os.path.exists(p):
            os.remove(p)
            removed += 1
        backup = (state or {}).get('backups', {}).get(name)
        if backup and os.path.exists(backup):
            shutil.copy2(backup, p)
    # Remove now-empty directories that the release created.
    for name in sorted(files, key=lambda n: -n.count('/')):
        d = os.path.dirname(os.path.join(g['game'], name))
        while d.startswith(g['game']) and d != g['game']:
            try:
                os.rmdir(d)
            except OSError:
                break
            d = os.path.dirname(d)
    filelist = os.path.join(g['game'], 'bin', 'config', 'r4game', 'user_config_matrix', 'pc', 'dx12filelist.txt')
    if os.path.exists(filelist):
        with open(filelist, 'rb') as f:
            data = f.read()
        nl = b'\r\n' if b'\r\n' in data else b'\n'
        lines = [l for l in data.split(nl) if l.strip() != HUD_XML_LINE]
        while lines and lines[-1] == b'':
            lines.pop()
        write_with_backup(filelist, nl.join(lines), False)
    for extra in ('witcher3vr.ini',):
        p = os.path.join(g['x64'], extra)
        if os.path.exists(p):
            shutil.move(p, p + '.uninstalled')
            print('  kept your ini as %s.uninstalled' % p)
    if args.restore_settings:
        original = os.path.join(g['docs'], 'dx12user.settings.w3vr-original.bak')
        if os.path.exists(original):
            shutil.copy2(original, os.path.join(g['docs'], 'dx12user.settings'))
            print('  restored dx12user.settings from %s' % original)
    if state:
        os.remove(state_path(g))
    print('Removed %d mod files. The game runs without VR again. (Saves were not touched.)' % removed)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    sub.add_parser('status')
    sub.add_parser('backup-saves')
    sp = sub.add_parser('install')
    sp.add_argument('zip')
    sp.add_argument('--sha256', help='the release *-SHA256.txt file')
    sp.add_argument('--force', action='store_true', help='install even if witcher3.exe is not a supported build')
    sp.add_argument('--dll', help='activate this dxgi.dll instead of the release reference (e.g. a Linux-built fix)')
    sp = sub.add_parser('configure', help='write the VR config (defaults tuned for Quest 3 with WiVRn)')
    sp.add_argument('--mode', choices=['stereo', 'aer', 'mono'], default='stereo',
                    help='stereo = both eyes from one game tick (default, cleanest); aer = AER+AFW (faster, can shimmer)')
    sp.add_argument('--aa', choices=['dlss', 'taau', 'fsr', 'none'],
                    help='default: dlss on NVIDIA, otherwise taau (4.04) or none = FXAA (5.01, which has no TAAU); '
                         'fsr = the game\'s FSR with one FSR history per eye (5.01, experimental)')
    sp.add_argument('--dlss-quality', type=int, default=1, choices=range(0, 5),
                    help='0 DLAA, 1 Quality (default), 2 Balanced, 3 Performance, 4 Ultra Performance')
    sp.add_argument('--res', default='2476x2594',
                    help="per-eye render size WIDTHxHEIGHT, or 'runtime' to follow the OpenXR runtime's recommended "
                         "per-eye size at every start (good for SteamVR) (default 2476x2594, WiVRn's recommendation "
                         "for Quest 3)")
    sp.add_argument('--world-detail', type=float, default=0.7, help='0.4-1.0; lower = less CPU per eye (default 0.7)')
    sp.add_argument('--hud-size', type=float, default=0.5,
                    help='size of the VR HUD plane (stereo modes; default 0.5 = half size, chosen for Quest 3)')
    sp.add_argument('--hud-frame', type=float, default=1.0,
                    help='game HUD frame scale (1.0 = vanilla; lower values pull panels toward the centre)')
    sp.add_argument('--hud-locked', type=int, default=1, choices=[0, 1], help='1 = HUD stays in front (F9 recentres)')
    sp.add_argument('--near-view', type=float, default=-1.0,
                    help='start in Near view with this camera offset (negative = farther; '
                         'Geralt is centred by [engine] camera_center)')
    sp.add_argument('--no-near-view', dest='near_view', action='store_const', const=None, help='start in Standard view')
    sp.add_argument('--symmetric', action='store_true',
                    help='5.01: render the symmetric envelope and crop it (older projection) '
                         'instead of per-eye frusta')
    sp.add_argument('--diagnostics', action='store_true', help='enable the mod log (costs performance)')
    sp.add_argument('--reset-settings', action='store_true', help='re-apply the VR graphics template to dx12user.settings')
    sp.add_argument('--force', action='store_true', help='configure even if witcher3.exe is an unknown build')
    sp.add_argument('--dry-run', action='store_true')
    sp = sub.add_parser('launch-options')
    sp.add_argument('--runtime', choices=['auto', 'wivrn', 'steamvr'], default='auto',
                    help='auto = the active OpenXR runtime (SteamVR if none is active and SteamVR is installed)')
    sp = sub.add_parser('wivrn-shortcut', help="add the game to WiVRn's in-headset app launcher")
    sp.add_argument('--remove', action='store_true')
    sp.add_argument('--autostart', type=int, choices=[0, 1],
                    help='1 = also start the game whenever the headset connects (WiVRn config "application")')
    sp = sub.add_parser('uninstall')
    sp.add_argument('--restore-settings', action='store_true', help='restore dx12user.settings from before configure')
    args = p.parse_args()
    {'status': cmd_status, 'backup-saves': cmd_backup_saves, 'install': cmd_install, 'configure': cmd_configure,
     'launch-options': cmd_launch_options, 'wivrn-shortcut': cmd_wivrn_shortcut,
     'uninstall': cmd_uninstall}[args.cmd](args)


if __name__ == '__main__':
    main()
