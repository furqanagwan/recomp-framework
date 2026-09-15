import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from recomp_project import GuestImage, RecompProject

FUNCTION = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)")
LABEL = re.compile(r"^loc_([0-9A-F]{8}):")
INSTRUCTION = re.compile(r"^\t// (\S+)(?: (.*))?$")
SWITCH = re.compile(r"^\tswitch \(ctx\.r(\d+)\.u32\) \{")
CASE = re.compile(r"^\tcase (\d+):")
MAX_TABLE_SLOTS = 1024


@dataclass
class ShortTable:
    function: int
    bctr: int
    register: int
    table: int
    generated_cases: int
    labels: list[int]


def signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def scan_function(image: GuestImage, start: int, lines: list[str]) -> list[ShortTable]:
    labels = {start} | {int(match.group(1), 16) for line in lines if (match := LABEL.match(line))}
    register_values: dict[str, int] = {}
    block_address = start
    instructions_in_block = 0
    table_base_register = None
    found = []
    index = 0
    while index < len(lines):
        line = lines[index]
        if match := LABEL.match(line):
            block_address = int(match.group(1), 16)
            instructions_in_block = 0
        elif match := INSTRUCTION.match(line):
            mnemonic, operands = match.group(1), (match.group(2) or "").split(",")
            if mnemonic == "lis" and len(operands) == 2:
                register_values[operands[0]] = (signed16(int(operands[1]) & 0xFFFF) << 16) & 0xFFFFFFFF
            elif mnemonic == "addi" and len(operands) == 3 and operands[0] == operands[1] \
                    and operands[1] in register_values:
                register_values[operands[0]] = (register_values[operands[1]] + int(operands[2])) & 0xFFFFFFFF
            elif mnemonic == "lwzx" and len(operands) == 3:
                table_base_register = operands[1]
            instructions_in_block += 1
        elif match := SWITCH.match(line):
            bctr = block_address + 4 * (instructions_in_block - 1)
            cases = 0
            while index + 1 < len(lines) and not lines[index + 1].startswith("\tdefault:"):
                index += 1
                if CASE.match(lines[index]):
                    cases += 1
            table = register_values.get(table_base_register)
            if table is not None and image.word(table + 4 * cases) in labels:
                slots = cases
                while slots < MAX_TABLE_SLOTS and image.word(table + 4 * slots) in labels:
                    slots += 1
                found.append(ShortTable(start, bctr, int(match.group(1)), table, cases,
                                        [image.word(table + 4 * slot) for slot in range(slots)]))
        index += 1
    return found


def short_tables(project: RecompProject, image: GuestImage) -> list[ShortTable]:
    found = []
    for source in project.recompiled_sources():
        start, body = None, []
        for line in source.read_text().splitlines():
            if match := FUNCTION.match(line):
                if start is not None:
                    found.extend(scan_function(image, start, body))
                start, body = int(match.group(1), 16), []
            elif start is not None:
                body.append(line)
        if start is not None:
            found.extend(scan_function(image, start, body))
    return found


def write_config(project: RecompProject, tables: list[ShortTable]) -> Path:
    config = project.functions_config.parent / "switch_tables.toml"
    existing = config.read_text() if config.exists() else (
        "# Jump tables whose size codegen under-counts. Codegen sizes a table from the\n"
        "# bounds check before the bctr; when another path reaches the table with a\n"
        "# larger index, the generated switch hits its out-of-range trap. Found with\n"
        "# find_short_switch_tables.py; each entry lists every slot read from the image.\n")
    known = {int(address, 16) for address in re.findall(r"^address = 0x([0-9A-F]{8})", existing, re.MULTILINE)}
    with config.open("w", newline="\n") as output:
        output.write(existing.rstrip("\n") + "\n")
        for table in tables:
            if table.bctr in known:
                continue
            labels = ", ".join(f"0x{label:08X}" for label in table.labels)
            output.write(f"\n# sub_{table.function:08X}: table 0x{table.table:08X}, codegen found "
                         f"{table.generated_cases} of {len(table.labels)} slots\n"
                         f"[[switch_tables]]\naddress = 0x{table.bctr:08X}\n"
                         f"register = {table.register}\nlabels = [{labels}]\n")
    return config


def main():
    parser = argparse.ArgumentParser(
        description="Find jump tables that codegen sized too small, using a guest image dump.")
    parser.add_argument("--game", required=True, help="game folder, e.g. skate2")
    parser.add_argument("--module", default="default",
                        help="DLL module to work on, named after its generated folder (e.g. Loader_DLL)")
    parser.add_argument("--image", type=Path, help="image dump (default <game>/out/image_dump.bin)")
    parser.add_argument("--write", action="store_true",
                        help="append the tables to config/switch_tables.toml")
    args = parser.parse_args()

    project = RecompProject(args.game, args.module)
    image = GuestImage(args.image or project.default_image_dump)
    tables = short_tables(project, image)
    for table in tables:
        print(f"  0x{table.bctr:08X} in sub_{table.function:08X}: "
              f"{table.generated_cases} cases generated, table has {len(table.labels)}")
    print(f"{len(tables)} short jump tables")
    if args.write and tables:
        config = write_config(project, tables)
        print(f"wrote {config}; add it to the module's manifest includes if missing")


if __name__ == "__main__":
    main()
