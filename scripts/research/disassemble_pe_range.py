#!/usr/bin/env python3
"""Disassemble an x86-64 PE virtual-address range with Capstone."""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

from find_hash_constants import sections


def parse_address(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("start", type=parse_address, help="Start virtual address")
    parser.add_argument("--end", type=parse_address, help="Exclusive end virtual address")
    parser.add_argument("--size", type=parse_address, default=0x100,
                        help="Byte count when --end is omitted (default: 0x100)")
    parser.add_argument("--function", action="store_true",
                        help="Expand start to the enclosing x64 .pdata function range")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    try:
        from capstone import CS_ARCH_X86, CS_MODE_64, Cs
    except ImportError as error:
        parser.error(f"x64 disassembly requires the capstone Python package: {error}")

    data = args.binary.read_bytes()
    image_base, section_list = sections(data)
    start = args.start
    end = args.end if args.end is not None else start + args.size
    if args.function:
        target_rva = args.start - image_base
        pdata = next((item for item in section_list if item["name"] == ".pdata"), None)
        if pdata is None:
            parser.error("PE image has no .pdata section")
        raw = data[pdata["raw_offset"]:pdata["raw_offset"] + pdata["raw_size"]]
        match = next(((begin, finish) for offset in range(0, len(raw) - 11, 12)
                      for begin, finish, _ in (struct.unpack_from("<III", raw, offset),)
                      if begin <= target_rva < finish), None)
        if match is None:
            parser.error("start is not covered by an x64 .pdata runtime-function entry")
        start, end = image_base + match[0], image_base + match[1]
    if end <= start:
        parser.error("end must be greater than start")

    section = next((item for item in section_list
                    if image_base + item["rva"] <= start
                    and end <= image_base + item["rva"] + item["raw_size"]), None)
    if section is None:
        parser.error("requested virtual-address range is not fully backed by one PE section")

    section_va = image_base + section["rva"]
    file_offset = section["raw_offset"] + start - section_va
    raw = data[file_offset:file_offset + end - start]
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    decoder.skipdata = True
    lines = [
        f"{address:#011x}  {bytes_.hex(' '):<31}  {mnemonic:<8} {operands}".rstrip()
        for address, size, mnemonic, operands in decoder.disasm_lite(raw, start)
        for bytes_ in (raw[address - start:address - start + size],)
    ]
    output = "\n".join(lines) + ("\n" if lines else "")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    else:
        sys.stdout.write(output)
    print(f"range=0x{start:X}..0x{end:X} bytes={end-start}", file=sys.stderr)
    return 0 if lines else 1


if __name__ == "__main__":
    raise SystemExit(main())
