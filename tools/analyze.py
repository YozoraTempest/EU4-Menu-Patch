"""Index local PE references and inspect functions without modifying the executable."""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import struct

import capstone
import pefile


class Image:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        self.pe = pefile.PE(data=self.data)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.functions = sorted(
            (entry.struct.BeginAddress, entry.struct.EndAddress)
            for entry in self.pe.DIRECTORY_ENTRY_EXCEPTION
        )
        self.starts = [start for start, _ in self.functions]
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.skipdata = True

    def owner(self, rva: int):
        index = bisect.bisect_right(self.starts, rva) - 1
        if index >= 0:
            start, end = self.functions[index]
            if start <= rva < end:
                return start, end
        return None

    def instructions(self, start: int, length: int):
        data = self.pe.get_data(start, length)
        return self.md.disasm_lite(data, self.base + start)

    def strings(self):
        for section in self.pe.sections:
            if section.Characteristics & 0x20000000:
                continue
            data = section.get_data()
            for match in re.finditer(rb"[\x20-\x7e]{4,}\x00", data):
                yield section.VirtualAddress + match.start(), match.group()[:-1].decode("ascii")


def index(image: Image, database: Path):
    database.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(database)
    connection.executescript("""
        DROP TABLE IF EXISTS refs;
        DROP TABLE IF EXISTS strings;
        DROP TABLE IF EXISTS functions;
        DROP TABLE IF EXISTS imports;
        CREATE TABLE refs(src INTEGER, dst INTEGER, kind TEXT, asm TEXT);
        CREATE TABLE strings(rva INTEGER PRIMARY KEY, value TEXT);
        CREATE TABLE functions(start INTEGER PRIMARY KEY, end INTEGER);
        CREATE TABLE imports(rva INTEGER PRIMARY KEY, name TEXT);
    """)
    connection.executemany("INSERT INTO functions VALUES (?, ?)", image.functions)
    connection.executemany("INSERT OR IGNORE INTO strings VALUES (?, ?)", image.strings())
    for descriptor in image.pe.DIRECTORY_ENTRY_IMPORT:
        connection.executemany(
            "INSERT INTO imports VALUES (?, ?)",
            [(item.address - image.base, descriptor.dll.decode() + "!" +
              (item.name.decode() if item.name else str(item.ordinal)))
             for item in descriptor.imports],
        )
    rip = re.compile(r"\[rip(?: ([+-]) (0x[0-9a-f]+))?\]")
    pending = []
    count = 0
    for section in image.pe.sections:
        if not section.Characteristics & 0x20000000:
            continue
        for address, size, mnemonic, operands in image.instructions(section.VirtualAddress, section.SizeOfRawData):
            count += 1
            match = rip.search(operands)
            if match:
                displacement = int(match[2], 16) if match[2] else 0
                if match[1] == "-":
                    displacement = -displacement
                target = address + size + displacement - image.base
                pending.append((address - image.base, target, "rip", mnemonic + " " + operands))
            elif mnemonic in ("call", "jmp") and operands.startswith("0x"):
                pending.append((address - image.base, int(operands, 16) - image.base, mnemonic, mnemonic + " " + operands))
            if len(pending) >= 10000:
                connection.executemany("INSERT INTO refs VALUES (?, ?, ?, ?)", pending)
                pending.clear()
        print(f"Indexed {section.Name!r}: {count} instructions", flush=True)
    connection.executemany("INSERT INTO refs VALUES (?, ?, ?, ?)", pending)
    connection.executescript("CREATE INDEX refs_dst ON refs(dst); CREATE INDEX refs_src ON refs(src);")
    connection.commit()
    summary = {
        "sha256": hashlib.sha256(image.data).hexdigest(),
        "image_base": hex(image.base),
        "entrypoint_rva": hex(image.pe.OPTIONAL_HEADER.AddressOfEntryPoint),
        "instructions": count,
        "functions": len(image.functions),
        "sections": [{"name": s.Name.rstrip(b"\0").decode(), "rva": hex(s.VirtualAddress),
                      "size": s.Misc_VirtualSize} for s in image.pe.sections],
        "debug": [str(d.entry) for d in getattr(image.pe, "DIRECTORY_ENTRY_DEBUG", [])],
    }
    database.with_suffix(".json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))


def search(image: Image, database: Path, patterns: list[str]):
    connection = sqlite3.connect(database)
    for pattern in patterns:
        print(f"\nPattern: {pattern}")
        for rva, value in connection.execute("SELECT rva,value FROM strings WHERE value LIKE ? LIMIT 100", ("%" + pattern + "%",)):
            print(f"STRING {rva:#x}: {value}")
            for src, asm in connection.execute("SELECT src,asm FROM refs WHERE dst=?", (rva,)):
                print(f"  REF {src:#x} owner={image.owner(src)} {asm}")


def disassemble(image: Image, database: Path, rva: int, length: int | None):
    if rva >= image.base:
        rva -= image.base
    owner = image.owner(rva)
    if length is None:
        if owner is None:
            raise ValueError("No unwind function contains this address; provide --length")
        start, end = owner
    else:
        start, end = rva, rva + length
    connection = sqlite3.connect(database)
    print(f"Function {start:#x}..{end:#x}")
    for address, size, mnemonic, operands in image.instructions(start, end - start):
        instruction_rva = address - image.base
        annotations = []
        for (target,) in connection.execute("SELECT dst FROM refs WHERE src=?", (instruction_rva,)):
            for (name,) in connection.execute("SELECT name FROM imports WHERE rva=?", (target,)):
                annotations.append(name)
            for (value,) in connection.execute("SELECT value FROM strings WHERE rva=?", (target,)):
                annotations.append(repr(value))
        raw = image.pe.get_data(instruction_rva, size).hex(" ")
        print(f"{instruction_rva:08x}  {raw:44} {mnemonic:8} {operands}" + (" ; " + "; ".join(annotations) if annotations else ""))


def references(image: Image, database: Path, rva: int):
    connection = sqlite3.connect(database)
    for src, kind, asm in connection.execute("SELECT src,kind,asm FROM refs WHERE dst=?", (rva,)):
        print(f"{src:#x}: owner={image.owner(src)} {kind}: {asm}")
    pointer = struct.pack("<Q", image.base + rva)
    for section in image.pe.sections:
        data = section.get_data()
        offset = data.find(pointer)
        while offset >= 0:
            print(f"POINTER {section.VirtualAddress + offset:#x} -> {rva:#x}")
            offset = data.find(pointer, offset + 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, default=Path(r"D:\SteamLibrary\steamapps\common\Europa Universalis IV\eu4.exe"))
    parser.add_argument("--db", type=Path, default=Path(__file__).resolve().parents[1] / "private" / "analysis.sqlite")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("index")
    find = subparsers.add_parser("search")
    find.add_argument("patterns", nargs="+")
    disasm = subparsers.add_parser("disasm")
    disasm.add_argument("rva", type=lambda value: int(value, 0))
    disasm.add_argument("--length", type=lambda value: int(value, 0))
    refs = subparsers.add_parser("refs")
    refs.add_argument("rva", type=lambda value: int(value, 0))
    args = parser.parse_args()
    image = Image(args.exe)
    if args.command == "index":
        index(image, args.db)
    elif args.command == "search":
        search(image, args.db, args.patterns)
    elif args.command == "disasm":
        disassemble(image, args.db, args.rva, args.length)
    else:
        references(image, args.db, args.rva)


if __name__ == "__main__":
    main()
