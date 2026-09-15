import os
import re
import struct
from dataclasses import dataclass
from pathlib import Path

# The framework is a submodule at <repository>/framework; override with RECOMP_REPOSITORY_ROOT.
REPOSITORY_ROOT = Path(os.environ.get("RECOMP_REPOSITORY_ROOT") or Path(__file__).resolve().parents[3])
PE_EXECUTABLE_SECTION = 0x20000000
GUEST_IMAGE_BASE = 0x82000000
SEED_LINE = re.compile(r'^"0x([0-9A-F]{8})" = ', re.MULTILINE)


@dataclass(frozen=True)
class Section:
    name: str
    start: int
    size: int
    executable: bool

    def contains(self, address: int) -> bool:
        return self.start <= address < self.start + self.size


class RecompProject:
    def __init__(self, game: str):
        self.root = REPOSITORY_ROOT / game
        if not (self.root / "CMakeLists.txt").exists():
            raise SystemExit(f"No game project at {self.root}")
        self.generated = self.root / "generated" / "default"
        self.functions_config = self.root / "config" / "functions.toml"
        self.disabled_seeds_log = self.root / "config" / "disabled_function_seeds.txt"

    @property
    def default_image_dump(self) -> Path:
        return self.root / "out" / "image_dump.bin"

    def init_source(self) -> Path:
        matches = sorted(self.generated.glob("*_init.cpp"))
        if not matches:
            raise SystemExit(f"No generated init source in {self.generated}; run codegen first")
        return matches[0]

    def recompiled_sources(self):
        return sorted(self.generated.glob("*_recomp.*.cpp"))

    def function_starts(self) -> list[int]:
        text = self.init_source().read_text()
        return sorted(int(address, 16) for address in re.findall(r"\{ 0x([0-9A-F]{8}), ", text))

    def branch_labels(self) -> set[int]:
        label = re.compile(r"^loc_([0-9A-F]{8}):", re.MULTILINE)
        labels = set()
        for source in self.recompiled_sources():
            labels.update(int(address, 16) for address in label.findall(source.read_text()))
        return labels

    def disabled_seeds(self) -> set[int]:
        if not self.disabled_seeds_log.exists():
            return set()
        return {int(line.split()[0], 16) for line in self.disabled_seeds_log.read_text().splitlines() if line.strip()}

    def seeds(self) -> set[int]:
        return {int(address, 16) for address in SEED_LINE.findall(self.functions_config.read_text())}


class GuestImage:
    def __init__(self, dump: Path):
        self.bytes = dump.read_bytes()
        self.sections = self._read_sections()

    def word(self, address: int) -> int:
        offset = address - GUEST_IMAGE_BASE
        if offset < 0 or offset + 4 > len(self.bytes):
            return 0
        return struct.unpack_from(">I", self.bytes, offset)[0]

    def executable_sections(self):
        return [section for section in self.sections if section.executable]

    def data_sections(self):
        return [section for section in self.sections if not section.executable]

    def is_code_address(self, address: int) -> bool:
        return any(section.contains(address) for section in self.executable_sections())

    def local_branch_targets(self) -> set[int]:
        targets = set()
        for section in self.executable_sections():
            for address in range(section.start, section.start + section.size, 4):
                instruction = self.word(address)
                if PowerPc.is_conditional_branch(instruction):
                    targets.add(PowerPc.conditional_branch_target(address, instruction))
                elif PowerPc.is_unconditional_branch(instruction):
                    targets.add(PowerPc.unconditional_branch_target(address, instruction))
        return targets

    def data_pointer_targets(self) -> set[int]:
        targets = set()
        for section in self.data_sections():
            first = section.start - (section.start % 4) + (4 if section.start % 4 else 0)
            for address in range(first, section.start + section.size - 3, 4):
                value = self.word(address)
                if value & 0x3 == 0 and self.is_code_address(value):
                    targets.add(value)
        return targets

    def _read_sections(self) -> list[Section]:
        if self.bytes[:2] != b"MZ":
            raise SystemExit("Image dump does not start with an MZ header")
        pe_offset = struct.unpack_from("<I", self.bytes, 0x3C)[0]
        count = struct.unpack_from("<H", self.bytes, pe_offset + 6)[0]
        optional_header_size = struct.unpack_from("<H", self.bytes, pe_offset + 20)[0]
        table = pe_offset + 24 + optional_header_size
        sections = []
        for index in range(count):
            entry = table + index * 40
            name = self.bytes[entry:entry + 8].rstrip(b"\0").decode("ascii", "replace")
            size, virtual_address = struct.unpack_from("<II", self.bytes, entry + 8)
            flags = struct.unpack_from("<I", self.bytes, entry + 36)[0]
            sections.append(Section(name, GUEST_IMAGE_BASE + virtual_address, size,
                                    bool(flags & PE_EXECUTABLE_SECTION)))
        return sections


class PowerPc:
    BLR = 0x4E800020
    BCTR = 0x4E800420

    @staticmethod
    def is_unconditional_branch(instruction: int) -> bool:
        return (instruction >> 26) == 18 and (instruction & 0x3) == 0

    @staticmethod
    def is_conditional_branch(instruction: int) -> bool:
        return (instruction >> 26) == 16 and (instruction & 0x3) == 0

    @classmethod
    def ends_function(cls, instruction: int) -> bool:
        return instruction in (cls.BLR, cls.BCTR) or cls.is_unconditional_branch(instruction)

    @staticmethod
    def unconditional_branch_target(address: int, instruction: int) -> int:
        displacement = instruction & 0x03FFFFFC
        if displacement & 0x02000000:
            displacement -= 0x04000000
        return (address + displacement) & 0xFFFFFFFF

    @staticmethod
    def conditional_branch_target(address: int, instruction: int) -> int:
        displacement = instruction & 0xFFFC
        if displacement & 0x8000:
            displacement -= 0x10000
        return (address + displacement) & 0xFFFFFFFF
