"""Produce compact summaries of reverse-engineering evidence."""

import argparse
from pathlib import Path
import re
import sqlite3

from analyze import Image

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["field", "calls", "strings"])
    parser.add_argument("target")
    args = parser.parse_args()
    image = Image(ROOT / "private/runtime/eu4.exe")
    connection = sqlite3.connect(ROOT / "private/analysis.sqlite")
    if args.mode == "field":
        displacement = int(args.target, 0).to_bytes(4, "little")
        for section in image.pe.sections:
            if not section.Characteristics & 0x20000000:
                continue
            data = section.get_data()
            # Byte stores with a 32-bit displacement. Capstone verifies candidates.
            for match in re.finditer(b"\xc6[\x80-\x87]" + re.escape(displacement) + b"[\x00\x01]", data):
                rva = section.VirtualAddress + match.start()
                instructions = list(image.instructions(rva, 7))
                owner = image.owner(rva)
                print(hex(rva), tuple(hex(x) for x in owner) if owner else None, instructions)
    else:
        rva = int(args.target, 0)
        start, end = image.owner(rva)
        print(f"Function {start:#x}..{end:#x}")
        for source, target, kind, assembly in connection.execute(
            "SELECT src,dst,kind,asm FROM refs WHERE src>=? AND src<? ORDER BY src", (start, end)
        ):
            literal = connection.execute("SELECT value FROM strings WHERE rva=?", (target,)).fetchone()
            if args.mode == "strings" and not literal:
                continue
            if args.mode == "calls" and kind not in ("call", "jmp") and not literal:
                continue
            print(f"{source:#x} -> {target:#x}: {assembly}", repr(literal[0]) if literal else "")


if __name__ == "__main__":
    main()
