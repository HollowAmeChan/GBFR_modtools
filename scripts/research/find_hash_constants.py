#!/usr/bin/env python3
"""Locate little-endian MMAT hash constants in a PE image."""

from __future__ import annotations

import argparse
import csv
import pathlib
import struct


def sections(data: bytes) -> tuple[int, list[dict]]:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    image_base = struct.unpack_from("<Q" if magic == 0x20B else "<I", data, optional + (24 if magic == 0x20B else 28))[0]
    result = []
    section_table = optional + optional_size
    for index in range(count):
        offset = section_table + index * 40
        name = data[offset:offset + 8].split(b"\0", 1)[0].decode("ascii", errors="replace")
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        result.append({"name": name, "rva": rva, "virtual_size": virtual_size,
                       "raw_size": raw_size, "raw_offset": raw_offset})
    return image_base, result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("hashes", type=pathlib.Path, help="CSV containing a hash column")
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
    with args.hashes.open("r", encoding="utf-8-sig", newline="") as stream:
        hashes = sorted({int(row["hash"], 16) for row in csv.DictReader(stream)})
    rows = []
    for hash_value in hashes:
        needle = struct.pack("<I", hash_value)
        start = 0
        while True:
            offset = data.find(needle, start)
            if offset < 0:
                break
            section = next((item for item in section_list
                            if item["raw_offset"] <= offset < item["raw_offset"] + item["raw_size"]), None)
            rva = section["rva"] + offset - section["raw_offset"] if section else None
            rows.append({"hash": f"0x{hash_value:08X}", "file_offset": f"0x{offset:X}",
                         "section": section["name"] if section else "<headers/overlay>",
                         "rva": f"0x{rva:X}" if rva is not None else "",
                         "va": f"0x{image_base + rva:X}" if rva is not None else ""})
            start = offset + 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=["hash", "file_offset", "section", "rva", "va"])
        writer.writeheader(); writer.writerows(rows)
    print(f"hashes={len(hashes)} occurrences={len(rows)} output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
