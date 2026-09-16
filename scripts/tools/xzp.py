"""List or extract an Xbox 360 XUI package (.xzp).

These packages hold the resources the console's own UI draws with: icons, button
glyphs, fonts and localised string tables. They are Microsoft's files and are
never redistributed here; this reads a copy the user supplies (from their own
console, a system update or a dashboard they own), so the compatibility guide
can match the era it came from.

  python xzp.py list <file.xzp>
  python xzp.py extract <file.xzp> <output directory>

Layout, worked out from retail packages (numbers big-endian unless noted):

  'XUIZ', u32 version, u32 package size, u32 unused, u32 name table size,
  u16 entry count, u16 unused, u32 first data offset, u16 unused,
  then one record per entry, from offset 0x1E:

  version 1 (Blades):  u16 name length (little-endian), UTF-16LE name,
                       u8 unused, u16 data size, u32 data offset
  version 3 (NXE, Metro): u8 name length, name, u32 data size, u32 data offset
"""
import struct
import sys
from pathlib import Path


class Entry:
    def __init__(self, name, offset, size):
        self.name = name
        self.offset = offset
        self.size = size


def read(path: Path):
    data = path.read_bytes()
    if data[:4] != b"XUIZ":
        raise SystemExit(f"{path}: not a XUI package")
    version = struct.unpack_from(">I", data, 4)[0]
    count = struct.unpack_from(">H", data, 0x14)[0]
    if version not in (1, 3):
        raise SystemExit(f"{path}: unsupported package version {version}")

    # [header][name table][data]; entry offsets are relative to the data.
    names_size = struct.unpack_from(">I", data, 0x10)[0]
    base = names_size + 0x16

    entries = []
    offset = 0x1E
    for index in range(count):
        if version == 1:
            name_length = struct.unpack_from("<H", data, offset)[0]
            name = data[offset + 2:offset + 2 + name_length * 2].decode("utf-16-le", "replace")
            offset += 2 + name_length * 2
            size = struct.unpack_from(">H", data, offset + 1)[0]
            data_offset = struct.unpack_from(">I", data, offset + 3)[0]
            offset += 7
        else:
            name_length = data[offset]
            name = data[offset + 1:offset + 1 + name_length].decode("latin-1")
            offset += 1 + name_length
            size, data_offset = struct.unpack_from(">II", data, offset)
            offset += 8
        if data_offset + base + size > len(data):
            # The last record is a terminator rather than a file.
            print(f"{path.name}: stopping at record {index} of {count}", file=sys.stderr)
            break
        entries.append(Entry(name, data_offset + base, size))
    return version, data, entries


def describe(entry: Entry, data: bytes) -> str:
    blob = data[entry.offset:entry.offset + 8]
    if blob[:8] == b"\x89PNG\r\n\x1a\n":
        return "PNG"
    if blob[:4] in (b"XUIB", b"XUIS", b"XUIZ"):
        return blob[:4].decode()
    if blob[:4] == b"FNT ":
        return "font"
    return "".join(chr(b) if 32 <= b < 127 else "." for b in blob[:4])


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    command, package = sys.argv[1], Path(sys.argv[2])
    version, data, entries = read(package)
    if command == "list":
        print(f"{package.name}: version {version}, {len(entries)} entries")
        for entry in entries:
            print(f"  {entry.size:>8}  {describe(entry, data):<8}  {entry.name}")
        return
    if command == "extract":
        if len(sys.argv) < 4:
            raise SystemExit(__doc__)
        out_dir = Path(sys.argv[3])
        for entry in entries:
            target = out_dir / entry.name.replace("\\", "/")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data[entry.offset:entry.offset + entry.size])
        print(f"extracted {len(entries)} files to {out_dir}")
        return
    raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
