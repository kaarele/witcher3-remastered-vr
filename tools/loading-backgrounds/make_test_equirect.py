#!/usr/bin/env python3
"""Synthetic 360-degree test pictures for [openxr] loading_background.

Writes two equirectangular pictures into the given folder (default:
./w3vr_backgrounds):

  test_mono.png    2:1 mono. Grid every 15 degrees, FRONT/RIGHT/BACK/LEFT
                   labels at the horizon, UP/DOWN near the poles.
  test_TB.png      1:1 top-bottom stereo (left eye on top). The labels sit at
                   about 2 m and the grid at about 20 m, so the labels must
                   float in front of the grid, and each half also names its eye.

Check in the headset: FRONT straight ahead where you looked when the loading
screen appeared, RIGHT to your right, the horizon level, the stereo labels in
front of the grid, and "LEFT EYE" seen only by the left eye.

Needs Pillow and numpy:  python3 make_test_equirect.py [folder] [width]
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

LABELS = [("FRONT", 0.0), ("RIGHT", 90.0), ("BACK", 180.0), ("LEFT", -90.0)]
IPD = 0.064


def font(size):
    for name in ("DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf", "Arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def background(width, height):
    """Gradient sky/ground with a 15-degree grid, as an RGB array."""
    u, v = np.meshgrid((np.arange(width) + 0.5) / width, (np.arange(height) + 0.5) / height)
    latitude = (0.5 - v) * 180.0
    longitude = (u - 0.5) * 360.0
    sky = np.stack([40 + 60 * (latitude > 0), 70 + 50 * (latitude > 0),
                    110 - 50 * (latitude <= 0)], axis=-1)
    image = sky.astype(np.float32)
    image = image * (0.75 + 0.25 * np.cos(np.radians(longitude)))[..., None]
    grid = (np.abs(((latitude + 7.5) % 15.0) - 7.5) < 0.12) | \
        (np.abs(((longitude + 7.5) % 15.0) - 7.5) < 0.12 * 2)
    image[grid] = (220, 220, 220)
    horizon = np.abs(latitude) < 0.25
    image[horizon] = (255, 200, 40)
    return np.clip(image, 0, 255).astype(np.uint8)


def draw_labels(image, width, height, shift_deg=0.0, eye=None):
    draw = ImageDraw.Draw(image)
    big = font(max(16, height // 18))
    for text, lon in LABELS:
        x = (0.5 + (lon + shift_deg) / 360.0) * width
        for wrap in (-width, 0, width):
            draw.text((x + wrap, height * 0.5 - height // 12), text, fill=(255, 255, 255),
                      font=big, anchor="mm", stroke_width=3, stroke_fill=(0, 0, 0))
    small = font(max(12, height // 30))
    draw.text((width * 0.5, height * 0.06), "UP", fill=(255, 255, 255), font=small, anchor="mm")
    draw.text((width * 0.5, height * 0.94), "DOWN", fill=(255, 255, 255), font=small, anchor="mm")
    if eye:
        draw.text((width * 0.5, height * 0.62), eye + " EYE", fill=(255, 80, 80), font=small,
                  anchor="mm", stroke_width=2, stroke_fill=(0, 0, 0))


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "w3vr_backgrounds"
    width = int(sys.argv[2]) if len(sys.argv) > 2 else 4096
    os.makedirs(folder, exist_ok=True)
    height = width // 2

    mono = Image.fromarray(background(width, height))
    draw_labels(mono, width, height)
    mono.save(os.path.join(folder, "test_mono.png"))

    # Stereo: labels at ~2 m, grid at ~20 m. For a point straight ahead of the
    # viewer, each eye sees it shifted by about atan(IPD/2 / distance).
    halves = []
    for eye, sign in (("LEFT", +1.0), ("RIGHT", -1.0)):
        grid_shift = sign * math.degrees(math.atan(IPD / 2 / 20.0))
        label_shift = sign * math.degrees(math.atan(IPD / 2 / 2.0))
        pixels = background(width, height)
        roll = int(round(grid_shift / 360.0 * width))
        half = Image.fromarray(np.roll(pixels, roll, axis=1))
        draw_labels(half, width, height, shift_deg=label_shift, eye=eye)
        halves.append(half)
    stereo = Image.new("RGB", (width, height * 2))
    stereo.paste(halves[0], (0, 0))
    stereo.paste(halves[1], (0, height))
    stereo.save(os.path.join(folder, "test_TB.png"))
    print("wrote", os.path.join(folder, "test_mono.png"), "and", os.path.join(folder, "test_TB.png"))


if __name__ == "__main__":
    main()
