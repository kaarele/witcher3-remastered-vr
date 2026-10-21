#!/usr/bin/env python3
"""Stitch rectilinear views into a 360-degree picture for loading_background.

Input: a JSON manifest describing square rectilinear captures (the same
horizontal field of view for all of them), for example a ring of 8 views
45 degrees apart per eye plus one view straight up and one straight down:

  {"fov_deg": 90,
   "views": [{"file": "ring0_left.png", "eye": "left",  "yaw": 0,   "pitch": 0},
             {"file": "ring0_right.png", "eye": "right", "yaw": 0,   "pitch": 0},
             {"file": "ring1_left.png", "eye": "left",  "yaw": 45,  "pitch": 0},
             ...
             {"file": "up.png",   "eye": "both", "yaw": 0, "pitch": 90},
             {"file": "down.png", "eye": "both", "yaw": 0, "pitch": -90}]}

yaw: degrees, positive = turned right; pitch: positive = looking up. Paths
are relative to the manifest. "both" views are used for both eyes (zero
disparity, the comfortable choice near the poles).

Output: an equirectangular picture, top-bottom stereo (left eye on top) when
the manifest has left and right views, otherwise mono 2:1.

Stereo from a ring of views is an approximation of omni-directional stereo:
the depth is exact in the middle of each view and slightly reduced towards
its edges, and objects closer than about 1.5 m can show small seams where
two views meet. Put the subject (for example Geralt meditating) in the
middle of the yaw-0 view.

  python3 stitch_views_to_equirect.py manifest.json out_TB.jpg [width]
  python3 stitch_views_to_equirect.py --selftest equirect.png
Needs Pillow and numpy.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image


def rotation(yaw_deg, pitch_deg):
    """World-from-view rotation: yaw right about +Y, then pitch up about +X."""
    yaw = math.radians(-yaw_deg)  # positive yaw turns right = negative rotation about +Y
    pitch = math.radians(pitch_deg)
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rx = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]])
    return ry @ rx


def directions(width, height):
    """Unit view directions (x right, y up, -z forward) of an equirect grid."""
    lon = (np.arange(width) + 0.5) / width * 2 * math.pi - math.pi
    lat = math.pi / 2 - (np.arange(height) + 0.5) / height * math.pi
    lon, lat = np.meshgrid(lon, lat)
    return np.stack([np.sin(lon) * np.cos(lat), np.sin(lat), -np.cos(lon) * np.cos(lat)], -1), lon, lat


def bilinear(image, u, v):
    h, w, _ = image.shape
    x = np.clip(u * w - 0.5, 0, w - 1.001)
    y = np.clip(v * h - 0.5, 0, h - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = (x - x0)[..., None], (y - y0)[..., None]
    a = image[y0, x0] * (1 - fx) + image[y0, x0 + 1] * fx
    b = image[y0 + 1, x0] * (1 - fx) + image[y0 + 1, x0 + 1] * fx
    return a * (1 - fy) + b * fy


def project(view, d, tan_half):
    """Sample a rectilinear view for world directions d; returns colour, validity."""
    local = d @ view["rot"]  # view-from-world = transpose, applied to row vectors
    z = -local[..., 2]
    valid = z > 1e-6
    safe = np.where(valid, z, 1.0)
    tx, ty = local[..., 0] / safe / tan_half, local[..., 1] / safe / tan_half
    valid &= (np.abs(tx) <= 1) & (np.abs(ty) <= 1)
    colour = bilinear(view["pixels"], (tx + 1) / 2, (1 - ty) / 2)
    return colour, valid


def stitch_eye(views, width, tan_half, fov_deg):
    height = width // 2
    d, lon, lat = directions(width, height)
    total = np.zeros((height, width, 3))
    weight_sum = np.zeros((height, width))
    ring = [v for v in views if abs(v["pitch"]) < 1]
    step = 360.0 / max(len(ring), 1)
    for view in views:
        colour, valid = project(view, d, tan_half)
        if abs(view["pitch"]) < 1:
            # Triangle weights around the view's yaw; neighbours sum to 1.
            delta = np.degrees(np.angle(np.exp(1j * (lon - math.radians(view["yaw"])))))
            w = np.clip(1 - np.abs(delta) / step, 0, 1)
            w *= 1 - np.clip((np.abs(np.degrees(lat)) - 35) / 15, 0, 1)
        else:
            # Pole views take over from 35 to 50 degrees of latitude.
            towards = np.degrees(lat) * np.sign(view["pitch"])
            w = np.clip((towards - 35) / 15, 0, 1)
        w = np.where(valid, w, 0)
        total += colour * w[..., None]
        weight_sum += w
    covered = weight_sum > 1e-6
    out = np.where(covered[..., None], total / np.maximum(weight_sum, 1e-6)[..., None], 0)
    return np.clip(out, 0, 255).astype(np.uint8), float(covered.mean())


def load(manifest_path):
    manifest = json.load(open(manifest_path))
    base = os.path.dirname(os.path.abspath(manifest_path))
    views = []
    for entry in manifest["views"]:
        pixels = np.asarray(Image.open(os.path.join(base, entry["file"])).convert("RGB"), np.float32)
        views.append({"eye": entry.get("eye", "both"), "yaw": float(entry.get("yaw", 0)),
                      "pitch": float(entry.get("pitch", 0)), "pixels": pixels,
                      "rot": rotation(float(entry.get("yaw", 0)), float(entry.get("pitch", 0)))})
    return float(manifest.get("fov_deg", 90)), views


def stitch(manifest_path, out_path, width=4096):
    fov_deg, views = load(manifest_path)
    tan_half = math.tan(math.radians(fov_deg) / 2)
    stereo = any(v["eye"] == "right" for v in views)
    eyes = ["left", "right"] if stereo else ["left"]
    halves = []
    for eye in eyes:
        chosen = [v for v in views if v["eye"] in (eye, "both")] if stereo else views
        pixels, coverage = stitch_eye(chosen, width, tan_half, fov_deg)
        print("%s eye: %d views, %.1f%% covered" % (eye, len(chosen), coverage * 100))
        halves.append(pixels)
    Image.fromarray(np.concatenate(halves, axis=0)).save(out_path, quality=92)
    print("wrote", out_path, "(top-bottom stereo)" if stereo else "(mono)")


def selftest(equirect_path):
    """Render 8 ring views + up/down from a mono equirect, stitch them back, compare."""
    import tempfile
    source = np.asarray(Image.open(equirect_path).convert("RGB"), np.float32)
    size, fov = 1024, 90.0
    tan_half = math.tan(math.radians(fov) / 2)
    folder = tempfile.mkdtemp()
    entries = []
    t = (np.arange(size) + 0.5) / size * 2 - 1
    tx, ty = np.meshgrid(t * tan_half, -t * tan_half)
    local = np.stack([tx, ty, -np.ones_like(tx)], -1)
    local /= np.linalg.norm(local, axis=-1, keepdims=True)
    poses = [(45.0 * k, 0.0) for k in range(8)] + [(0.0, 90.0), (0.0, -90.0)]
    for index, (yaw, pitch) in enumerate(poses):
        world = local @ rotation(yaw, pitch).T
        lon = np.arctan2(world[..., 0], -world[..., 2])
        lat = np.arcsin(np.clip(world[..., 1], -1, 1))
        view = bilinear(source, 0.5 + lon / (2 * math.pi), 0.5 - lat / math.pi)
        name = "v%02d.png" % index
        Image.fromarray(np.clip(view, 0, 255).astype(np.uint8)).save(os.path.join(folder, name))
        entries.append({"file": name, "eye": "both", "yaw": yaw, "pitch": pitch})
    manifest = os.path.join(folder, "manifest.json")
    json.dump({"fov_deg": fov, "views": entries}, open(manifest, "w"))
    out = os.path.join(folder, "stitched.png")
    stitch(manifest, out, width=source.shape[1])
    result = np.asarray(Image.open(out).convert("RGB"), np.float32)
    band = slice(source.shape[0] // 8, source.shape[0] * 7 // 8)
    mse = np.mean((result[band] - source[band]) ** 2)
    psnr = 10 * math.log10(255 ** 2 / max(mse, 1e-9))
    print("selftest PSNR (latitudes within 67.5 deg): %.1f dB" % psnr)
    return psnr


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--selftest":
        sys.exit(0 if selftest(sys.argv[2]) > 28 else 1)
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    stitch(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 4096)
