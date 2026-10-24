#!/usr/bin/env python3
"""Extract files from a PSP UMD ISO9660 image without external tools.

Usage:
  extract_iso.py <image.iso> --list
  extract_iso.py <image.iso> <output_dir> [path_prefix ...]

Only paths starting with one of the prefixes are extracted (all files when no
prefix is given). Paths use the ISO layout, e.g. /PSP_GAME/SYSDIR/EBOOT.BIN.
"""

import os
import struct
import sys

from extraction_paths import archive_component, extraction_path

SECTOR = 2048


def read_sectors(image, lba, size):
    image.seek(lba * SECTOR)
    return image.read(size)


def walk(image, record, parent, visit, ancestors=()):
    lba = struct.unpack_from("<I", record, 2)[0]
    size = struct.unpack_from("<I", record, 10)[0]
    if lba in ancestors or len(ancestors) >= 64:
        raise ValueError("cyclic or excessively nested ISO directory")
    data = read_sectors(image, lba, size)
    offset = 0
    while offset < len(data):
        length = data[offset]
        if length == 0:
            offset = (offset // SECTOR + 1) * SECTOR
            continue
        entry = data[offset:offset + length]
        offset += length
        if len(entry) != length or length < 34:
            raise ValueError("truncated ISO directory record")
        name_length = entry[32]
        if not name_length or 33 + name_length > length:
            raise ValueError("invalid ISO directory filename length")
        raw_name = entry[33:33 + name_length]
        if raw_name in (b"\0", b"\1"):
            continue
        name = archive_component(raw_name.decode("ascii", errors="strict").split(";")[0])
        path = f"{parent}/{name}"
        if entry[25] & 2:
            walk(image, entry, path, visit, ancestors + (lba,))
        else:
            visit(path, struct.unpack_from("<I", entry, 2)[0], struct.unpack_from("<I", entry, 10)[0])


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(argv[1], "rb") as image:
        primary = read_sectors(image, 16, SECTOR)
        if primary[1:6] != b"CD001":
            print("not an ISO9660 image", file=sys.stderr)
            return 1
        root = primary[156:190]
        if argv[2] == "--list":
            walk(image, root, "", lambda path, lba, size: print(f"{size:12d} {path}"))
            return 0

        output_dir = argv[2]
        prefixes = argv[3:]

        def extract(path, lba, size):
            if prefixes and not any(path.startswith(prefix) for prefix in prefixes):
                return
            destination = extraction_path(output_dir, path.lstrip("/").split("/"))
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            image.seek(lba * SECTOR)
            remaining = size
            with open(destination, "wb") as out:
                while remaining:
                    chunk = image.read(min(remaining, 1 << 24))
                    if not chunk:
                        raise IOError(f"truncated image while reading {path}")
                    out.write(chunk)
                    remaining -= len(chunk)
            print(f"{size:12d} {path}")

        walk(image, root, "", extract)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
