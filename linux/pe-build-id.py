#!/usr/bin/env python3
"""pe-build-id.py - identify a witcher3.exe the way the VR mod does.

Usage: python3 -I pe-build-id.py [--scan] [--no-sig] [--src=DIR] <witcher3.exe>

Prints TimeDateStamp, SizeOfImage and FileVersion of the exe, and the build the
mod's src/game_build.h maps that (TimeDateStamp, SizeOfImage) pair to.
That is the exact check running_game_build() does at runtime (src/dxgi_proxy.cpp).

For anything other than the 4.04 build it also checks the 5.01 engine function
signatures from src/engine_addresses.h at their 5.01 RVAs (the same check
engine_function_target() does before placing each hook). --scan additionally
searches the whole image for each signature and prints where it is found, which
hints at moved functions on an unknown build. --scan is implied for unknown builds.

Read-only: the exe is only opened for reading. Standard library only.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# Fallback when src/game_build.h is not next to this script.
FALLBACK_BUILDS = [("kNextGen404", 0x664FB0DC, 0x0612E000),
                   ("kRemaster501", 0x6AC555A3, 0x06402000)]
BUILD_NAMES = {"kNextGen404": "next-gen-4.04 (engine hooks enabled)",
               "kRemaster501": "remaster-5.01 (per-function hooks, signature-checked)"}


def load_builds(src):
    path = os.path.join(src, "game_build.h")
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        return FALLBACK_BUILDS, "built-in table"
    rows = [(m[0], int(m[1], 16), int(m[2], 16)) for m in re.findall(
        r"PeIdentity\s+(k\w+)\s*\{\s*0x([0-9A-Fa-f]+)u?\s*,\s*0x([0-9A-Fa-f]+)u?\s*\}", text)]
    return (rows or FALLBACK_BUILDS), path


def load_signatures(src):
    path = os.path.join(src, "engine_addresses.h")
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        return []
    arrays = {name: bytes(int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})\b", body))
              for name, body in re.findall(
                  r"constexpr\s+uint8_t\s+(k\w+)\[\]\s*=\s*\{([^}]*)\}", text)}
    out = []
    for name, rva404, rva501, arr in re.findall(
            r'\{\s*"([\w.]+)"\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*0x([0-9A-Fa-f]+)\s*,\s*\{\s*(k\w+)\s*,',
            text):
        out.append((name, int(rva404, 16), int(rva501, 16), arrays.get(arr, b"")))
    return out


class PE:
    def __init__(self, data):
        self.d = data
        if data[:2] != b"MZ":
            raise ValueError("not an MZ executable")
        nt = struct.unpack_from("<I", data, 0x3C)[0]
        if data[nt:nt + 4] != b"PE\0\0":
            raise ValueError("no PE signature")
        (self.machine, nsec, self.time_date_stamp, _, _, opt_size,
         _) = struct.unpack_from("<HHIIIHH", data, nt + 4)
        opt = nt + 24
        magic = struct.unpack_from("<H", data, opt)[0]
        self.size_of_image, = struct.unpack_from("<I", data, opt + 56)
        self.checksum, = struct.unpack_from("<I", data, opt + 64)
        ddir = opt + (112 if magic == 0x20B else 96)
        self.res_rva, self.res_size = struct.unpack_from("<II", data, ddir + 2 * 8)
        self.sections = []
        for i in range(nsec):
            s = opt + opt_size + 40 * i
            name = data[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, s + 8)
            self.sections.append((name, va, max(vsize, rawsize), rawptr, rawsize))

    def off(self, rva):
        for _, va, size, rawptr, rawsize in self.sections:
            if va <= rva < va + size and rva - va < rawsize:
                return rawptr + rva - va
        return None

    def rva(self, off):
        for _, va, _, rawptr, rawsize in self.sections:
            if rawptr <= off < rawptr + rawsize:
                return va + off - rawptr
        return None

    def version_resource(self):
        """Return the raw RT_VERSION (16) resource of the first name/language."""
        base = self.off(self.res_rva)
        if base is None:
            return None

        def entries(dir_off):
            n_named, n_id = struct.unpack_from("<HH", self.d, dir_off + 12)
            for i in range(n_named + n_id):
                ident, target = struct.unpack_from("<II", self.d, dir_off + 16 + 8 * i)
                yield ident, target

        for ident, target in entries(base):
            if ident == 16 and target & 0x80000000:
                lvl2 = base + (target & 0x7FFFFFFF)
                for _, t2 in entries(lvl2):
                    lvl3 = base + (t2 & 0x7FFFFFFF) if t2 & 0x80000000 else None
                    leaf = next(entries(lvl3))[1] if lvl3 is not None else t2
                    data_rva, size = struct.unpack_from("<II", self.d, base + leaf)
                    o = self.off(data_rva)
                    return self.d[o:o + size] if o is not None else None
        return None


def parse_version(blob):
    """VS_FIXEDFILEINFO file/product version plus the StringFileInfo strings."""
    fixed = strings = None
    i = blob.find(struct.pack("<I", 0xFEEF04BD)) if blob else -1
    if i >= 0:
        ms, ls, pms, pls = struct.unpack_from("<IIII", blob, i + 8)
        fixed = ("%d.%d.%d.%d" % (ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF),
                 "%d.%d.%d.%d" % (pms >> 16, pms & 0xFFFF, pls >> 16, pls & 0xFFFF))
    if blob:
        strings = {}
        for key in ("FileVersion", "ProductVersion", "ProductName", "CompanyName"):
            k = (key + "\0").encode("utf-16-le")
            j = blob.find(k)
            if j >= 0:
                j += len(k)
                j += (-j) % 4
                end = blob.find(b"\0\0", j)
                while end >= 0 and (end - j) % 2:
                    end = blob.find(b"\0\0", end + 1)
                strings[key] = blob[j:end].decode("utf-16-le", "replace") if end > j else ""
    return fixed, strings


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    flags = {a for a in argv if a.startswith("--") and not a.startswith("--src")}
    src = os.path.normpath(os.path.join(HERE, "..", "src"))
    for a in argv:
        if a.startswith("--src="):
            src = a.split("=", 1)[1]
    if len(args) != 1:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    path = args[0]
    with open(path, "rb") as f:
        data = f.read()
    pe = PE(data)
    fixed, strings = parse_version(pe.version_resource())
    builds, table = load_builds(src)

    print("file:           %s" % path)
    print("size:           %d bytes" % len(data))
    print("machine:        0x%04X" % pe.machine)
    print("TimeDateStamp:  0x%08X" % pe.time_date_stamp)
    print("SizeOfImage:    0x%08X" % pe.size_of_image)
    print("CheckSum:       0x%08X" % pe.checksum)
    print("FileVersion:    %s (fixed)" % (fixed[0] if fixed else "?"))
    print("ProductVersion: %s (fixed)" % (fixed[1] if fixed else "?"))
    for k, v in (strings or {}).items():
        print("  %-14s %s" % (k + ":", v))

    match = next((n for n, t, s in builds if t == pe.time_date_stamp and s == pe.size_of_image), None)
    print("table:          %s" % table)
    for n, t, s in builds:
        print("  %-13s TimeDateStamp=0x%08X SizeOfImage=0x%08X%s"
              % (n, t, s, "   <== MATCH" if n == match else ""))
    print("mod build:      %s" % (BUILD_NAMES.get(match, match) if match
                                  else "unknown (all engine hooks stay disabled)"))

    sigs = load_signatures(src)
    if "--no-sig" in flags or not sigs or (match == "kNextGen404" and "--scan" not in flags):
        return 0 if match else 1
    scan = "--scan" in flags or match is None
    print("5.01 engine signatures (%s):" % os.path.join(src, "engine_addresses.h"))
    ok = 0
    for name, rva404, rva501, sig in sigs:
        o = pe.off(rva501)
        hit = o is not None and sig and data[o:o + len(sig)] == sig
        ok += bool(hit)
        line = "  %-38s rva501=0x%08X %2dB %s" % (name, rva501, len(sig), "OK" if hit else "MISMATCH")
        if scan and sig:
            found, j = [], data.find(sig)
            while j >= 0 and len(found) < 4:
                r = pe.rva(j)
                found.append("0x%08X" % r if r is not None else "off:0x%X" % j)
                j = data.find(sig, j + 1)
            line += "  found at: %s" % (", ".join(found) or "-")
            if rva404 and ("0x%08X" % rva404) in found:
                line += " (=4.04 rva)"
        print(line)
    print("signatures OK:  %d/%d" % (ok, len(sigs)))
    return 0 if match else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
