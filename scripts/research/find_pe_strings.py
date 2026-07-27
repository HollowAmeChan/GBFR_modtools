#!/usr/bin/env python3
"""Locate exact ASCII strings in a PE image and report file offsets and VAs."""

from __future__ import annotations

import argparse
import csv
import pathlib
import sys

from find_hash_constants import sections


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("text", nargs="+")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
    rows = []
    for text in args.text:
        needle = text.encode("ascii")
        start = 0
        while True:
            offset = data.find(needle, start)
            if offset < 0:
                break
            section = next(
                (
                    item for item in section_list
                    if item["raw_offset"] <= offset < item["raw_offset"] + item["raw_size"]
                ),
                None,
            )
            rva = section["rva"] + offset - section["raw_offset"] if section else None
            rows.append({
                "text": text,
                "file_offset": f"0x{offset:X}",
                "section": section["name"] if section else "<headers/overlay>",
                "rva": f"0x{rva:X}" if rva is not None else "",
                "va": f"0x{image_base + rva:X}" if rva is not None else "",
            })
            start = offset + 1

    rows.sort(key=lambda row: (row["text"], row["file_offset"]))
    fields = ["text", "file_offset", "section", "rva", "va"]
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8-sig", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(f"strings={len(args.text)} occurrences={len(rows)}", file=sys.stderr)
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
