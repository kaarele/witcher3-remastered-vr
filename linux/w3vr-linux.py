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
  setup [--payload DIR]        one-step install from an extracted release (files, VR settings, saves backup,
                               Steam launch options); this is what "Install (Linux)" runs
  remove                       undo setup completely (saves are never touched); "Uninstall (Linux)" runs this
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
    sys.stdout.flush()
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

    def remove(self, section, key):
        b = self._bounds(section)
        if not b:
            return
        for i in range(b[0] + 1, b[1]):
            line = self.lines[i]
            if ('=' in line and not line.lstrip().startswith((';', '#'))
                    and line.split('=', 1)[0].strip().lower() == key.lower()):
                del self.lines[i]
                return

    def sections(self):
        return [l.strip()[1:-1].strip() for l in self.lines if l.strip().startswith('[') and l.strip().endswith(']')]

    def drop_empty_sections(self, keep=()):
        """Remove sections (not in keep) that have no entries left, with the blank line before them."""
        keep = {k.lower() for k in keep}
        for section in self.sections():
            b = self._bounds(section)
            if not b or section.lower() in keep or any(l.strip() for l in self.lines[b[0] + 1:b[1]]):
                continue
            start = b[0]
            while start > 0 and not self.lines[start - 1].strip():
                start -= 1
            del self.lines[start:b[1]]

    def snapshot(self, skip_sections=('Galaxy',)):
        """{(section, key): value} of every entry, without the sections in skip_sections."""
        out, section = {}, None
        for line in self.lines:
            t = line.strip()
            if t.startswith('[') and t.endswith(']'):
                section = t[1:-1].strip()
                continue
            if section is None or section in skip_sections or '=' not in line or t.startswith((';', '#')):
                continue
            k, v = line.split('=', 1)
            out.setdefault((section, k.strip()), v.strip())
        return out

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
        os.path.expanduser('~/snap/steam/common/.local/share/Steam'),
        os.path.expanduser('~/snap/steam/common/.local/share/steam'),
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
    override = os.environ.get('W3VR_ASSUME_GAME')  # tests: running | closed
    if override in ('running', 'closed'):
        return override == 'running'
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
    n = 2
    while os.path.exists(dest):  # two backups in the same second
        dest = os.path.join(BACKUP_ROOT, '%s-%d' % (stamp, n))
        n += 1
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
    w, h = (int(v) for v in args.res.lower().split('x'))
    if args.res.lower() == '2476x2594':
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
    if not getattr(args, 'from_setup', False):
        print('Done. Keep the headset connected to your OpenXR runtime BEFORE starting the game.')


def launch_option_parts():
    """(environment assignments before %command%, detail) for this PC's OpenXR runtime and GPU."""
    kind, detail = openxr_runtime()
    parts = ['PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1']
    system = '/var/lib/flatpak/app/io.github.wivrn.wivrn'
    user = os.path.expanduser('~/.local/share/flatpak/app/io.github.wivrn.wivrn')
    if kind and kind.startswith('wivrn-flatpak'):
        # The Steam runtime container must see the WiVRn Flatpak to load its OpenXR runtime.
        parts.append('PRESSURE_VESSEL_FILESYSTEMS_RW=%s' % (user if os.path.isdir(user) and not os.path.isdir(system) else system))
    # The tested setup: load the mod's dxgi.dll, and NVAPI for DLSS on NVIDIA.
    parts.append('WINEDLLOVERRIDES="dxgi=n,b"')
    if nvidia_gpu():
        parts.append('PROTON_ENABLE_NVAPI=1')
    return parts, detail


LAUNCH_ARGS = ['--launcher-skip']


def cmd_launch_options(args):
    parts, detail = launch_option_parts()
    print('Runtime: %s' % detail)
    print('Steam > The Witcher 3 > Properties > General > Launch Options:')
    print()
    print('  %s %%command%% --launcher-skip' % ' '.join(parts))
    print()
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


# ------------------------------------------------------------------------------ Steam launch options (localconfig.vdf)
# Steam keeps each account's per-game launch options in userdata/<account id>/config/localconfig.vdf, under
# UserLocalConfigStore/Software/Valve/Steam/apps/<appid>/LaunchOptions. Steam rewrites that file when it exits, so it
# may only be edited while Steam is closed.
STEAM_ID64_BASE = 76561197960265728
VDF_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|(\{)|(\})|(//[^\n]*)')


def vdf_unescape(value):
    return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m.group(1), m.group(1)), value)


def vdf_escape(value):
    return value.replace('\\', '\\\\').replace('"', '\\"')


def vdf_block(text, path):
    """(open brace index, close brace index) of the block at path (keys compared case-insensitively), or None."""
    want = [k.lower() for k in path]
    stack, last = [], None
    for m in VDF_TOKEN.finditer(text):
        if m.group(4):
            continue
        if m.group(2):
            stack.append(((last or '').lower(), m.start()))
            last = None
            if [k for k, _ in stack] == want:
                depth = 1
                for n in VDF_TOKEN.finditer(text, m.end()):
                    if n.group(2):
                        depth += 1
                    elif n.group(3):
                        depth -= 1
                        if depth == 0:
                            return m.start(), n.start()
                return None
        elif m.group(3):
            if stack:
                stack.pop()
            last = None
        else:
            last = None if last is not None else vdf_unescape(m.group(1))
    return None


def vdf_child_value(text, span, key):
    """(value, value token span) of a direct key/value child of the block span, or (None, None)."""
    depth, expect_key, current = 0, True, None
    for m in VDF_TOKEN.finditer(text, span[0] + 1, span[1]):
        if m.group(4):
            continue
        if m.group(2):
            depth += 1
            expect_key = True
        elif m.group(3):
            depth -= 1
            expect_key = True
        elif depth == 0:
            if expect_key:
                current = vdf_unescape(m.group(1))
                expect_key = False
            else:
                if current is not None and current.lower() == key.lower():
                    return vdf_unescape(m.group(1)), (m.start(), m.end())
                expect_key = True
    return None, None


def vdf_line_indent(text, index):
    start = text.rfind('\n', 0, index) + 1
    return text[start:index][:len(text[start:index]) - len(text[start:index].lstrip('\t '))]


def vdf_set_child_value(text, path, key, value):
    """Set (value str) or remove (value None) path/key, creating missing blocks under the deepest existing one."""
    span = vdf_block(text, path)
    if span is None:
        if value is None:
            return text
        parent_path = list(path[:-1])
        while parent_path and vdf_block(text, parent_path) is None:
            parent_path.pop()
        if not parent_path:
            raise ValueError('localconfig.vdf has no %s block' % path[0])
        parent = vdf_block(text, parent_path)
        missing = path[len(parent_path):]
        indent = vdf_line_indent(text, parent[0]) + '\t'
        block = ''
        for depth, name in enumerate(missing):
            pad = indent + '\t' * depth
            block += '%s"%s"\n%s{\n' % (pad, vdf_escape(name), pad)
        pad = indent + '\t' * len(missing)
        block += '%s"%s"\t\t"%s"\n' % (pad, vdf_escape(key), vdf_escape(value))
        for depth in reversed(range(len(missing))):
            block += '%s}\n' % (indent + '\t' * depth)
        close = parent[1]
        line_start = text.rfind('\n', 0, close) + 1
        return text[:line_start] + block + text[line_start:]
    current, token = vdf_child_value(text, span, key)
    if current is not None:
        if value is None:
            line_start = text.rfind('\n', 0, token[0]) + 1
            line_end = text.find('\n', token[1])
            return text[:line_start] + text[line_end + 1 if line_end >= 0 else len(text):]
        return text[:token[0]] + '"%s"' % vdf_escape(value) + text[token[1]:]
    if value is None:
        return text
    pad = vdf_line_indent(text, span[0]) + '\t'
    line_start = text.rfind('\n', 0, span[1]) + 1
    return text[:line_start] + '%s"%s"\t\t"%s"\n' % (pad, vdf_escape(key), vdf_escape(value)) + text[line_start:]


def vdf_remove_block_if_empty(text, path):
    span = vdf_block(text, path)
    if span is None:
        return text
    inner = VDF_TOKEN.search(text, span[0] + 1, span[1])
    if inner and not inner.group(4):
        return text  # still has entries (Steam keeps e.g. LastPlayed there)
    # Remove the line with the block's key, the braces and everything between.
    key_line = text.rfind('\n', 0, text.rfind('"', 0, span[0])) + 1
    end = text.find('\n', span[1])
    return text[:key_line] + text[end + 1 if end >= 0 else len(text):]


APP_PATH = ['UserLocalConfigStore', 'Software', 'Valve', 'Steam', 'apps', APP_ID]


def localconfig_path(g):
    """localconfig.vdf of the Steam account that last logged in (or the only/most recent one), or None."""
    userdata = os.path.join(g['steam_root'], 'userdata')
    if not os.path.isdir(userdata):
        return None
    candidates = [os.path.join(userdata, d, 'config', 'localconfig.vdf') for d in os.listdir(userdata) if d.isdigit()]
    candidates = [c for c in candidates if os.path.exists(c)]
    loginusers = os.path.join(g['steam_root'], 'config', 'loginusers.vdf')
    if os.path.exists(loginusers):
        with open(loginusers, encoding='utf-8', errors='replace') as f:
            text = f.read()
        for m in re.finditer(r'"(7656\d{13})"\s*\{([^}]*)\}', text):
            if re.search(r'"MostRecent"\s+"1"', m.group(2), re.I):
                account = str(int(m.group(1)) - STEAM_ID64_BASE)
                path = os.path.join(userdata, account, 'config', 'localconfig.vdf')
                if os.path.exists(path):
                    return path
    return max(candidates, key=os.path.getmtime) if candidates else None


def steam_running():
    override = os.environ.get('W3VR_ASSUME_STEAM')  # tests: running | closed
    if override in ('running', 'closed'):
        return override == 'running'
    for pid in os.listdir('/proc'):
        if not pid.isdigit():
            continue
        try:
            with open('/proc/%s/comm' % pid) as f:
                comm = f.read().strip()
            with open('/proc/%s/cmdline' % pid, 'rb') as f:
                argv0 = f.read().split(b'\0', 1)[0]
        except OSError:
            continue
        if comm == 'steam' and (argv0.endswith(b'/steam') or b'ubuntu12_32/steam' in argv0):
            return True
    return False


def split_launch_options(value):
    """(tokens before %command%, tokens after it). Options without %command% are game arguments."""
    value = (value or '').strip().replace('"%command%"', '%command%')
    if '%command%' in value:
        before, after = value.split('%command%', 1)
        return before.split(), after.split()
    return [], value.split()


def merge_launch_options(existing, env, args):
    """The user's launch options plus ours: our variables replace same-named ones, other tokens are kept."""
    before, after = split_launch_options(existing)
    names = {e.split('=', 1)[0] for e in env}
    # Environment assignments must come first: anything after a wrapper command (gamemoderun, mangohud) would be
    # taken as the program to run.
    before = env + [t for t in before if t.split('=', 1)[0] not in names]
    after = after + [a for a in args if a not in after]
    return ('%s %%command%% %s' % (' '.join(before), ' '.join(after))).strip()


def unmerge_launch_options(current, env, args):
    """Remove exactly our tokens again; '' when nothing else is left."""
    before, after = split_launch_options(current)
    before = [t for t in before if t not in env]
    after = [t for t in after if t not in args]
    if not before and not after:
        return ''
    if not before:
        return ' '.join(after)
    return ('%s %%command%% %s' % (' '.join(before), ' '.join(after))).strip()


def ask(prompt):
    if not sys.stdin.isatty():
        return None
    try:
        return input(prompt)
    except EOFError:
        return None


def copy_to_clipboard(text):
    if not sys.stdin.isatty():
        return False  # only for a person at a terminal
    for cmd in (['wl-copy'], ['xclip', '-selection', 'clipboard'], ['xsel', '--clipboard', '--input']):
        if shutil.which(cmd[0]):
            try:
                import subprocess
                # The clipboard tools keep running to serve the selection: never let them hold our output.
                subprocess.run(cmd, input=text.encode(), timeout=5, check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return True
            except Exception:
                continue
    return False


def wait_for_steam_closed(action):
    """True once Steam is closed; False when the user skips (or there is no terminal to ask in)."""
    while steam_running():
        answer = ask('Steam is running. To %s automatically, close Steam completely (Steam menu > Exit),\n'
                     'then press Enter. Or type s and press Enter to skip and do it by hand: ' % action)
        if answer is None or answer.strip().lower().startswith('s'):
            return False
    return True


def apply_launch_options(g, state):
    env, detail = launch_option_parts()
    print('OpenXR runtime: %s' % detail)
    vdf = localconfig_path(g)
    wanted_for_print = merge_launch_options('', env, LAUNCH_ARGS)
    if vdf and wait_for_steam_closed('set the Steam launch options'):
        with open(vdf, encoding='utf-8', errors='surrogateescape') as f:
            text = f.read()
        span = vdf_block(text, APP_PATH)
        current = vdf_child_value(text, span, 'LaunchOptions')[0] if span else None
        wanted = merge_launch_options(current, env, LAUNCH_ARGS)
        record = state.setdefault('launch_options', {})
        if 'original' not in record:
            record['original'] = current  # None = there were none
            record['created_block'] = span is None
        record.update({'file': vdf, 'env': env, 'args': LAUNCH_ARGS, 'written': wanted})
        if current == wanted:
            print('Steam launch options already set.')
            return True
        backup = vdf + '.w3vr.bak'
        shutil.copy2(vdf, backup)
        new_text = vdf_set_child_value(text, APP_PATH, 'LaunchOptions', wanted)
        tmp = vdf + '.w3vr.tmp'
        with open(tmp, 'w', encoding='utf-8', errors='surrogateescape') as f:
            f.write(new_text)
        os.replace(tmp, vdf)
        print('Steam launch options set (backup: %s):\n  %s' % (backup, wanted))
        return True
    print()
    print('Set the Steam launch options by hand: Steam > right-click The Witcher 3 > Properties > General >')
    print('Launch Options, and paste this line:')
    print()
    print('  ' + wanted_for_print)
    print()
    if copy_to_clipboard(wanted_for_print):
        print('(The line is already copied to your clipboard.)')
    state.setdefault('launch_options', {})['manual'] = wanted_for_print
    return False


def revert_launch_options(state):
    record = (state or {}).get('launch_options') or {}
    vdf = record.get('file')
    if not vdf or 'written' not in record:
        if record.get('manual'):
            print('Remember to clear the Steam launch options you added by hand (Steam > The Witcher 3 > Properties).')
        return True
    if not os.path.exists(vdf):
        return True
    if not wait_for_steam_closed('remove the Steam launch options'):
        print('Remove these parts from the Steam launch options by hand: %s ... --launcher-skip' % ' '.join(record['env']))
        return False
    with open(vdf, encoding='utf-8', errors='surrogateescape') as f:
        text = f.read()
    span = vdf_block(text, APP_PATH)
    current = vdf_child_value(text, span, 'LaunchOptions')[0] if span else None
    if current is None:
        return True
    if current == record['written']:
        restored = record.get('original')
    else:
        restored = unmerge_launch_options(current, record['env'], record['args'])  # the user edited them since
    new_text = vdf_set_child_value(text, APP_PATH, 'LaunchOptions', restored if restored else None)
    if record.get('created_block'):
        new_text = vdf_remove_block_if_empty(new_text, APP_PATH)
    shutil.copy2(vdf, vdf + '.w3vr.bak')
    tmp = vdf + '.w3vr.tmp'
    with open(tmp, 'w', encoding='utf-8', errors='surrogateescape') as f:
        f.write(new_text)
    os.replace(tmp, vdf)
    print('Steam launch options restored%s.' % ((': ' + restored) if restored else ' (cleared)'))
    return True


# ------------------------------------------------------------------------------------ one-step setup / remove
SETTINGS_SKIP = ('Galaxy', 'Localization')


def record_settings_changes(state, before, after):
    """Remember the original value of every dx12user.settings key the setup changed (first install wins)."""
    state.setdefault('settings_sections_before', sorted({k[0] for k in before}))
    changes = {tuple(c[:2]): c for c in state.get('settings_changes', [])}
    for key in set(before) | set(after):
        if before.get(key) != after.get(key) and key not in changes:
            changes[key] = [key[0], key[1], before.get(key), after.get(key)]
        elif key in changes:
            changes[key][3] = after.get(key)
    state['settings_changes'] = sorted(changes.values())


def revert_settings_changes(g, state):
    path = os.path.join(g['docs'], 'dx12user.settings')
    changes = (state or {}).get('settings_changes') or []
    if not changes or not os.path.exists(path):
        return
    doc = Ini.load(path)
    reverted = kept = 0
    for section, key, original, written in changes:
        if doc.get(section, key) != written:
            kept += 1  # changed again since (in the game menu): leave the player's choice
            continue
        if original is None:
            doc.remove(section, key)
        else:
            doc.set(section, key, original)
        reverted += 1
    if 'settings_sections_before' in state:
        doc.drop_empty_sections(keep=list(state['settings_sections_before']) + list(SETTINGS_SKIP))
    write_with_backup(path, doc.text().encode('ascii', errors='replace'))
    print('dx12user.settings: %d VR settings restored%s.' % (
        reverted, ', %d you changed since were kept' % kept if kept else ''))


def payload_files(payload):
    out = []
    for top in ('bin', 'mods', 'dlc', 'Witcher3VR'):
        root = os.path.join(payload, top)
        for base, _, files in os.walk(root):
            for name in files:
                rel = os.path.relpath(os.path.join(base, name), payload).replace(os.sep, '/')
                if '__pycache__' not in rel:
                    out.append(rel)
    return sorted(out)


def foreign_file(dest, ours):
    """True when dest is someone else's file worth restoring on uninstall (e.g. a ReShade dxgi.dll)."""
    name = os.path.basename(dest).lower()
    if name not in ('dxgi.dll', 'openxr_loader.dll'):
        return False  # our own names: an earlier version of this mod
    if sha256_file(dest) == sha256_file(ours):
        return False
    if name == 'dxgi.dll':
        with open(dest, 'rb') as f:
            return b'witcher3vr' not in f.read()
    return True


def cmd_setup(args):
    payload = os.path.abspath(args.payload)
    if not os.path.isfile(os.path.join(payload, 'bin', 'x64_dx12', 'dxgi.dll')):
        die('no bin/x64_dx12/dxgi.dll in %s: run the installer from the extracted release folder' % payload)
    g = find_game()
    if not g:
        die('The Witcher 3 was not found in your Steam libraries (native, Flatpak or Snap Steam). Install it in '
            'Steam first.')
    print('Found the game: %s' % g['game'])
    if game_running():
        die('The Witcher 3 is running. Quit the game, then run the installer again.')
    build, build_description = build_info(g)
    if build != '501':
        if build == '404':
            print('This is the Next-Gen 4.04 version. This mod is made and tested for Remastered 5.01 (Steam\'s '
                  'default version).')
        else:
            print('This game version is not supported yet (%s). The mod would stay inactive; a mod update for '
                  'this version is needed.' % build_description)
        if not args.force:
            die('Nothing was changed.')
    else:
        print('Game version: %s (supported)' % build_description)
    if os.path.isdir(g['docs']):
        print('Backing up your saves ...')
        print('  -> %s' % backup_saves(g))

    state = load_state(g) or {'files': [], 'backups': {}}
    installed = set(state['files'])
    ini_path = os.path.join(g['x64'], 'witcher3vr.ini')
    keep_res = None
    if os.path.exists(ini_path):
        old = Ini.load(ini_path)
        w, h = old.get('openxr', 'render_width'), old.get('openxr', 'render_height')
        if w and h and w.isdigit() and h.isdigit():
            keep_res = '%sx%s' % (w, h)
        saved = os.path.join(STATE_DIR, 'ini-backups', 'witcher3vr.ini.%s' % datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
        os.makedirs(os.path.dirname(saved), exist_ok=True)
        shutil.copy2(ini_path, saved)
        print('Updating: witcher3vr.ini is replaced by this release\'s tested settings%s.' % (
            ' (your render size %s is kept)' % keep_res if keep_res else ''))
        print('  your previous one is saved as %s' % saved)
    print('Copying the mod files ...')
    for rel in payload_files(payload):
        dest = os.path.join(g['game'], rel)
        if os.path.exists(dest) and rel not in installed and foreign_file(dest, os.path.join(payload, rel)):
            backup = os.path.join(STATE_DIR, 'overwritten', hashlib.sha1(g['game'].encode()).hexdigest()[:12], rel)
            os.makedirs(os.path.dirname(backup), exist_ok=True)
            shutil.copy2(dest, backup)
            state['backups'][rel] = backup
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copyfile(os.path.join(payload, rel), dest)
        if os.access(os.path.join(payload, rel), os.X_OK):
            os.chmod(dest, 0o755)
        installed.add(rel)
    fix_state_bridge(g, build)
    state.update({'files': sorted(installed), 'source': payload, 'game': g['game'],
                  'installed_at': datetime.datetime.now().isoformat(timespec='seconds')})
    save_state(g, state)
    print('  %d files in place.' % len(installed))

    settings_path = os.path.join(g['docs'], 'dx12user.settings')
    settings_done = False
    if os.path.exists(settings_path):
        current = Ini.load(settings_path)
        before = current.snapshot(SETTINGS_SKIP)
        conf = ['configure'] + (['--res', keep_res] if keep_res else []) + (['--force'] if args.force else [])
        if keep_res and current.get('PostProcess', 'AAMode') == '6':
            # Updating: keep the DLSS mode the player picked in the game (5 = DLAA, 0 = Auto).
            q = {'5': '0', '1': '1', '2': '2', '3': '3', '4': '4'}.get(current.get('PostProcess', 'DLSSQuality'))
            if q:
                conf += ['--dlss-quality', q]
        conf_args = build_parser().parse_args(conf)
        conf_args.from_setup = True
        cmd_configure(conf_args)
        record_settings_changes(state, before, Ini.load(settings_path).snapshot(SETTINGS_SKIP))
        save_state(g, state)
        settings_done = True
    else:
        print('The game has not created its settings yet (first start). The mod files are in place; to apply the VR')
        print('graphics settings, start the game once (main menu is enough), quit, and run the installer again.')
    launch_done = apply_launch_options(g, state)
    save_state(g, state)
    print()
    print('=' * 72)
    print('Done.' if settings_done and launch_done else 'Done, with the step(s) above to finish by hand.')
    print('Play: start your VR runtime (WiVRn or SteamVR), connect the headset, then start The Witcher 3 in Steam.')
    print('Remove the mod again any time with "Uninstall (Linux)".')
    print('=' * 72)


def cmd_remove(args):
    g = find_game()
    if not g:
        die('The Witcher 3 was not found in your Steam libraries.')
    if game_running():
        die('The Witcher 3 is running. Quit the game, then run the uninstaller again.')
    state = load_state(g)
    revert_launch_options(state)
    revert_settings_changes(g, state)
    cmd_uninstall(argparse.Namespace(restore_settings=False))
    for leftover in ('witcher3vr.ini.w3vr.bak', 'witcher3vr.ini.w3vr.tmp'):
        p = os.path.join(g['x64'], leftover)
        if os.path.exists(p) and state:
            os.remove(p)
    if os.path.isdir(BACKUP_ROOT):
        print('Save backups made by the installer stay in %s (delete them when you no longer need them).' % BACKUP_ROOT)


def build_parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    sp = sub.add_parser('setup', help='one-step install from an extracted release folder (used by "Install (Linux)")')
    sp.add_argument('--payload', default=os.path.join(TOOL_DIR, '..', '..'),
                    help='the release payload folder (mirrors the game folder)')
    sp.add_argument('--force', action='store_true', help='install even if the game version is not supported')
    sub.add_parser('remove', help='undo setup: files, VR settings and Steam launch options (saves are kept)')
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
                    help="per-eye render size WIDTHxHEIGHT; use your OpenXR runtime's recommended per-eye size "
                         "(default 2476x2594, WiVRn's recommendation for Quest 3)")
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
    sub.add_parser('launch-options')
    sp = sub.add_parser('wivrn-shortcut', help="add the game to WiVRn's in-headset app launcher")
    sp.add_argument('--remove', action='store_true')
    sp.add_argument('--autostart', type=int, choices=[0, 1],
                    help='1 = also start the game whenever the headset connects (WiVRn config "application")')
    sp = sub.add_parser('uninstall')
    sp.add_argument('--restore-settings', action='store_true', help='restore dx12user.settings from before configure')
    return p


def main():
    args = build_parser().parse_args()
    {'setup': cmd_setup, 'remove': cmd_remove, 'status': cmd_status, 'backup-saves': cmd_backup_saves, 'install': cmd_install, 'configure': cmd_configure,
     'launch-options': cmd_launch_options, 'wivrn-shortcut': cmd_wivrn_shortcut,
     'uninstall': cmd_uninstall}[args.cmd](args)


if __name__ == '__main__':
    main()
