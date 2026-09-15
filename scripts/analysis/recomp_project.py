import os
import re
import struct
import tomllib
from dataclasses import dataclass
from pathlib import Path

# The framework is a submodule at <repository>/framework; override with RECOMP_REPOSITORY_ROOT.
REPOSITORY_ROOT = Path(os.environ.get("RECOMP_REPOSITORY_ROOT") or Path(__file__).resolve().parents[3])
PE_EXECUTABLE_SECTION = 0x20000000
GUEST_IMAGE_BASE = 0x82000000
SEED_LINE = re.compile(r'^"0x([0-9A-F]{8})" = ', re.MULTILINE)
BOUNDED_SEED_LINE = re.compile(r'^"0x([0-9A-F]{8})" = \{[^}\n]*\bend = 0x([0-9A-Fa-f]{8})', re.MULTILINE)


@dataclass(frozen=True)
class Section:
    name: str
    start: int
    size: int
    executable: bool

    def contains(self, address: int) -> bool:
        return self.start <= address < self.start + self.size


class RecompProject:
    """A game folder, or one module of it: "default" is the entrypoint executable, and
    each [[modules]] DLL is named after its out_directory_path (e.g. "Loader_DLL")."""

    DEFAULT_MODULE = "default"

    def __init__(self, game: str, module: str = DEFAULT_MODULE):
        self.root = REPOSITORY_ROOT / game
        if not (self.root / "CMakeLists.txt").exists():
            raise SystemExit(f"No game project at {self.root}")
        self.game = game
        self.module = module
        self.manifest_path = next(self.root.glob("*_manifest.toml"), None)
        if self.manifest_path is None:
            raise SystemExit(f"No *_manifest.toml in {self.root}")
        entry = self._manifest_entry(module)
        self.generated = self.root / entry["out_directory_path"]
        config = self.root / "config" if module == self.DEFAULT_MODULE else self.root / "config" / module
        self.functions_config = config / "functions.toml"
        self.disabled_seeds_log = config / "disabled_function_seeds.txt"
        if module != self.DEFAULT_MODULE:
            self._ensure_module_config()

    def module_names(self) -> list[str]:
        manifest = tomllib.loads(self.manifest_path.read_text())
        return [self.DEFAULT_MODULE] + [Path(entry["out_directory_path"]).name for entry in manifest.get("modules", [])]

    def module_for_binary(self, binary_name: str) -> str | None:
        """Module whose executable file name (without extension) is `binary_name`."""
        manifest = tomllib.loads(self.manifest_path.read_text())
        entries = [(self.DEFAULT_MODULE, manifest["entrypoint"])] + [
            (Path(entry["out_directory_path"]).name, entry) for entry in manifest.get("modules", [])]
        for name, entry in entries:
            if Path(entry["file_path"]).stem.lower() == binary_name.lower():
                return name
        return None

    def _manifest_entry(self, module: str) -> dict:
        manifest = tomllib.loads(self.manifest_path.read_text())
        if module == self.DEFAULT_MODULE:
            return manifest["entrypoint"]
        for entry in manifest.get("modules", []):
            if Path(entry["out_directory_path"]).name == module:
                return entry
        raise SystemExit(f"No module {module!r} in {self.manifest_path}; modules: {', '.join(self.module_names())}")

    def _ensure_module_config(self) -> None:
        """Gives a DLL module its own functions.toml and lists it in the module's manifest includes."""
        if not self.functions_config.exists():
            self.functions_config.parent.mkdir(parents=True, exist_ok=True)
            self.functions_config.write_text("[functions]\n", newline="\n")
        include = self.functions_config.relative_to(self.root).as_posix()
        if include in self._manifest_entry(self.module).get("includes", []):
            return
        text = self.manifest_path.read_text()
        block = re.search(r'^\[\[modules\]\]\n(?:(?!\[\[).*\n)*?out_directory_path = "[^"]*/'
                          + re.escape(self.module) + r'"\n(?:(?!\[\[).*\n)*?includes = \[([^\]]*)\]',
                          text, re.MULTILINE)
        if block is None:
            raise SystemExit(f"Could not find the includes of module {self.module} in {self.manifest_path}")
        existing = block.group(1).strip().rstrip(",")
        entries = f"{existing}, \"{include}\"" if existing else f"\"{include}\""
        start, end = block.span(1)
        self.manifest_path.write_text(text[:start] + entries + text[end:], newline="\n")

    @property
    def default_image_dump(self) -> Path:
        if self.module == self.DEFAULT_MODULE:
            return self.root / "out" / "image_dump.bin"
        return self.root / "out" / f"image_dump_{self.module}.bin"

    def init_source(self) -> Path:
        matches = sorted(self.generated.glob("*_init.cpp"))
        if not matches:
            raise SystemExit(f"No generated init source in {self.generated}; run codegen first")
        return matches[0]

    def recompiled_sources(self):
        return sorted(self.generated.glob("*_recomp.*.cpp"))

    def function_starts(self) -> list[int]:
        if not any(self.generated.glob("*_init.cpp")):
            return []  # a module whose codegen hasn't succeeded yet
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

    def explicit_ranges(self) -> list[tuple[int, int]]:
        """(start, end) of functions whose bounds are given in functions.toml with `end =`."""
        return [(int(start, 16), int(end, 16)) for start, end in BOUNDED_SEED_LINE.findall(self.functions_config.read_text())]

    def inside_explicit_range(self, address: int) -> bool:
        return any(start < address < end for start, end in self.explicit_ranges())


class GuestImage:
    def __init__(self, dump: Path):
        self.bytes = dump.read_bytes()
        self.base = self._read_image_base()
        self.sections = self._read_sections()

    def word(self, address: int) -> int:
        offset = address - self.base
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

    def code_address_constants(self, window: int = 8):
        """Yields (addi address, code address, register) for `lis rN,hi` ... `addi rM,rN,lo` pairs."""
        for section in self.executable_sections():
            end = section.start + section.size
            for address in range(section.start, end, 4):
                instruction = self.word(address)
                if not PowerPc.is_lis(instruction):
                    continue
                register = PowerPc.destination_register(instruction)
                high = PowerPc.signed_immediate(instruction) << 16
                for follower in range(address + 4, min(address + 4 * window, end), 4):
                    next_instruction = self.word(follower)
                    if PowerPc.is_addi(next_instruction) and PowerPc.base_register(next_instruction) == register:
                        target = (high + PowerPc.signed_immediate(next_instruction)) & 0xFFFFFFFF
                        if target & 0x3 == 0 and self.is_code_address(target):
                            yield follower, target, PowerPc.destination_register(next_instruction)
                        break
                    if PowerPc.destination_register(next_instruction) == register:
                        break

    def used_as_base(self, address: int, register: int, window: int = 8) -> bool:
        """True when the register soon becomes a load base or an offset base (`add`), i.e. it
        addresses a table or a computed jump inside the current function, not another function."""
        for follower in range(address + 4, address + 4 * window, 4):
            instruction = self.word(follower)
            if PowerPc.is_load(instruction) and PowerPc.base_register(instruction) == register:
                return True
            if PowerPc.is_add(instruction) and register in PowerPc.add_operands(instruction):
                return True
        return False

    def function_address_constants(self) -> set[int]:
        """Code addresses built by lis/addi and not used as a table or jump base."""
        return {target for address, target, register in self.code_address_constants()
                if not self.is_code_address(self.word(target)) and not self.used_as_base(address, register)}

    def _read_image_base(self) -> int:
        """ImageBase from the PE32 optional header: 0x82000000 for executables, higher for DLLs."""
        if self.bytes[:2] != b"MZ":
            raise SystemExit("Image dump does not start with an MZ header")
        pe_offset = struct.unpack_from("<I", self.bytes, 0x3C)[0]
        magic = struct.unpack_from("<H", self.bytes, pe_offset + 24)[0]
        if magic != 0x10B:
            return GUEST_IMAGE_BASE
        return struct.unpack_from("<I", self.bytes, pe_offset + 24 + 28)[0]

    def _read_sections(self) -> list[Section]:
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
            sections.append(Section(name, self.base + virtual_address, size,
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

    # Opcodes of the D-form loads (lwz, lwzu, lbz, lbzu, lhz, lhzu, lha, lhau, lfs, lfsu, lfd, lfdu).
    D_FORM_LOADS = {32, 33, 34, 35, 40, 41, 42, 43, 48, 49, 50, 51}
    # Extended opcodes of the X-form indexed loads (lwzx, lbzx, lhzx, lhax, lfsx, lfdx).
    X_FORM_LOADS = {23, 87, 279, 343, 535, 599}

    @staticmethod
    def is_lis(instruction: int) -> bool:
        return (instruction >> 26) == 15 and PowerPc.base_register(instruction) == 0

    @staticmethod
    def is_addi(instruction: int) -> bool:
        return (instruction >> 26) == 14

    @classmethod
    def is_load(cls, instruction: int) -> bool:
        opcode = instruction >> 26
        return opcode in cls.D_FORM_LOADS or (opcode == 31 and ((instruction >> 1) & 0x3FF) in cls.X_FORM_LOADS)

    @staticmethod
    def is_add(instruction: int) -> bool:
        return (instruction >> 26) == 31 and ((instruction >> 1) & 0x1FF) == 266

    @staticmethod
    def add_operands(instruction: int) -> tuple[int, int]:
        return (instruction >> 16) & 0x1F, (instruction >> 11) & 0x1F

    @staticmethod
    def destination_register(instruction: int) -> int:
        return (instruction >> 21) & 0x1F

    @staticmethod
    def base_register(instruction: int) -> int:
        return (instruction >> 16) & 0x1F

    @staticmethod
    def signed_immediate(instruction: int) -> int:
        value = instruction & 0xFFFF
        return value - 0x10000 if value & 0x8000 else value

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
