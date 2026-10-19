#!/usr/bin/env python3
"""Summarise CAMTRACE lines from witcher3vr.log ([debug] camera_offset_trace=1).

usage: camtrace.py witcher3vr.log

Reports, for gameplay factory cameras (menu=0):
  - how often the classification flags change (world / hmd_scene / full_vr / suppress) and the
    near-view offset flag flips while camera_mode is 1 (a flip = the camera jumps by the offset),
  - the distance between consecutive game cameras of the same eye (entry position): large swings
    while the player only pans point at the game camera itself,
  - the applied move length (|moved|) distribution.
With camera centring fields (player=, lat=, lat_after=, ...; logged when [engine] camera_center is active) also:
  - per segment (split where auth or cine changes): lateral offset of the player from the game
    camera before (lat) and after (lat_after) the centring shift, in metres and degrees,
  - the shift difference between the two eyes of each stereo pair (must be 0),
  - lat/lat_after over the first 3 s after each authority regain (cutscene end, load).
"""
import math
import re
import sys
from collections import Counter

LINE = re.compile(r'CAMTRACE present=(\d+) eye=(-?\d+) pair=(\d+) menu=(\d+) world=(\d) hmd_scene=(\d) '
                  r'full_vr=(\d) suppress=(\d) mode=(\d) near=(\d) entry=([-\d.]+),([-\d.]+),([-\d.]+) '
                  r'moved=([-\d.]+),([-\d.]+),([-\d.]+) \|([-\d.]+)\| pitch=([-\d.]+) yaw=([-\d.]+)')
CENTRE = re.compile(r'player=([-\d.]+),([-\d.]+),([-\d.]+) meas=(\d) lat=([-\d.]+) fwd=([-\d.]+) '
                    r'shift=([-\d.]+) lat_after=([-\d.]+) centre=(\d) epoch=(\d+) auth=(\d) top=(\d) '
                    r'ncut=(-?\d+) nscene=(-?\d+) cine=(\d)')
STAMP = re.compile(r'\[(\d+):(\d+):([\d.]+)\]')


def stats(values):
    if not values:
        return 'n/a'
    s = sorted(values)
    return 'median %.3f p95 %.3f max %.3f (n=%d)' % (s[len(s) // 2], s[min(len(s) - 1, int(len(s) * 0.95))],
                                                    s[-1], len(s))


def angle(lat, fwd):
    return math.degrees(math.atan2(abs(lat), fwd)) if fwd > 0 else float('nan')


def centre_report(game):
    rows = [r for r in game if 'lat' in r]
    if not rows:
        print('no centring fields (camera centring was not active in this log)')
        return
    print('centring lines: %d (centre=1: %d, meas=0: %d)' % (
        len(rows), sum(r['centre'] for r in rows), sum(1 for r in rows if not r['meas'])))
    # Segments: consecutive eye-0 (or mono) lines with the same (auth, cine, mode, centre).
    seq = [r for r in rows if r['eye'] in (0, -1)]
    segments = []
    for r in seq:
        key = (r['auth'], r['cine'], r['mode'], r['centre'])
        if not segments or segments[-1][0] != key:
            segments.append((key, []))
        segments[-1][1].append(r)
    print('segments (auth, cine, mode, centre): lateral before -> after, metres | degrees')
    for key, seg in segments:
        good = [r for r in seg if r['meas'] and 0.4 <= r['fwd'] <= 8.0]
        if len(seg) < 4 or not good:
            print('  %s presents %d-%d: %d lines, %d measurable' % (
                key, seg[0]['present'], seg[-1]['present'], len(seg), len(good)))
            continue
        print('  %s presents %d-%d (%d lines)' % (key, seg[0]['present'], seg[-1]['present'], len(seg)))
        print('    |lat|       %s' % stats([abs(r['lat']) for r in good]))
        print('    |lat_after| %s' % stats([abs(r['lat_after']) for r in good]))
        print('    deg before  %s' % stats([angle(r['lat'], r['fwd']) for r in good]))
        print('    deg after   %s' % stats([angle(r['lat_after'], r['fwd']) for r in good]))
        print('    fwd         %s' % stats([r['fwd'] for r in good]))
    # Pair consistency: both eyes of a pair must get the same shift.
    by_pair = {}
    for r in rows:
        if r['centre'] and r['eye'] in (0, 1) and r['pair'] != 0:
            by_pair.setdefault(r['pair'], {})[r['eye']] = r['shift']
    diffs = [abs(v[0] - v[1]) for v in by_pair.values() if 0 in v and 1 in v]
    print('stereo pairs with both eyes centred: %d; max |shift eye0 - eye1| = %s' % (
        len(diffs), '%.4f' % max(diffs) if diffs else 'n/a'))
    # Authority regain edges (eye 0 / mono): auth 0 -> 1.
    edges = [i for i in range(1, len(seq)) if seq[i - 1]['auth'] == 0 and seq[i]['auth'] == 1]
    print('authority regain edges: %d' % len(edges))
    for i in edges:
        t0 = seq[i]['t']
        window = [r for r in seq[i:] if r['t'] is not None and t0 is not None and r['t'] - t0 <= 3.0]
        first = seq[i]
        print('  present %d epoch %d: first lat %.3f lat_after %.3f centre=%d meas=%d; 3 s: |lat_after| %s' % (
            first['present'], first['epoch'], first['lat'], first['lat_after'], first['centre'], first['meas'],
            stats([abs(r['lat_after']) for r in window if r['meas'] and r['auth']])))


def main(path):
    rows = []
    for line in open(path, errors='replace'):
        m = LINE.search(line)
        if m:
            g = m.groups()
            row = dict(present=int(g[0]), eye=int(g[1]), pair=int(g[2]), menu=int(g[3]), world=int(g[4]),
                       hmd=int(g[5]), full_vr=int(g[6]), suppress=int(g[7]), mode=int(g[8]), near=int(g[9]),
                       entry=tuple(map(float, g[10:13])), moved=float(g[16]), pitch=float(g[17]),
                       yaw=float(g[18]))
            t = STAMP.match(line)
            row['t'] = int(t.group(1)) * 3600 + int(t.group(2)) * 60 + float(t.group(3)) if t else None
            c = CENTRE.search(line)
            if c:
                h = c.groups()
                row.update(player=tuple(map(float, h[0:3])), meas=int(h[3]), lat=float(h[4]), fwd=float(h[5]),
                           shift=float(h[6]), lat_after=float(h[7]), centre=int(h[8]), epoch=int(h[9]),
                           auth=int(h[10]), top=int(h[11]), ncut=int(h[12]), nscene=int(h[13]), cine=int(h[14]))
            rows.append(row)
    game = [r for r in rows if r['menu'] == 0]
    print('CAMTRACE lines: %d (gameplay %d)' % (len(rows), len(game)))
    if not game:
        return
    flags = Counter((r['world'], r['hmd'], r['full_vr'], r['suppress'], r['mode'], r['near']) for r in game)
    print('flag combos (world, hmd_scene, full_vr, suppress, mode, near): count')
    for k, v in flags.most_common():
        print('  %s: %d' % (k, v))
    flips = 0
    near_flips = []
    last = {}
    for r in game:
        key = r['eye']
        if key in last:
            p = last[key]
            if (p['world'], p['hmd'], p['full_vr'], p['suppress']) != (r['world'], r['hmd'], r['full_vr'], r['suppress']):
                flips += 1
            if p['mode'] == 1 and r['mode'] == 1 and p['near'] != r['near']:
                near_flips.append(r['present'])
        last[key] = r
    print('classification changes: %d; near-offset flips while mode=1: %d %s' % (
        flips, len(near_flips), near_flips[:20]))
    for eye in sorted({r['eye'] for r in game}):
        seq = [r for r in game if r['eye'] == eye]
        steps = [math.dist(a['entry'], b['entry']) for a, b in zip(seq, seq[1:])]
        if steps:
            steps_sorted = sorted(steps)
            print('eye %d: %d cameras; entry step median %.3f m, p95 %.3f m, max %.3f m' % (
                eye, len(seq), steps_sorted[len(steps) // 2], steps_sorted[int(len(steps) * 0.95)], steps_sorted[-1]))
        moved = sorted(r['moved'] for r in seq)
        print('eye %d: |moved| min %.3f median %.3f max %.3f' % (eye, moved[0], moved[len(moved) // 2], moved[-1]))
        pitches = [r['pitch'] for r in seq]
        print('eye %d: game pitch min %.2f max %.2f' % (eye, min(pitches), max(pitches)))
    centre_report(game)


if __name__ == '__main__':
    main(sys.argv[1])
