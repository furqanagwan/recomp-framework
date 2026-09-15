import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from recomp_project import GuestImage, PowerPc, RecompProject


def candidates_from_data_pointers(image: GuestImage, known: set[int]) -> dict[int, str]:
    references: dict[int, list[int]] = {}
    for section in image.data_sections():
        first = section.start - (section.start % 4) + (4 if section.start % 4 else 0)
        for address in range(first, section.start + section.size - 3, 4):
            value = image.word(address)
            if value & 0x3 == 0 and image.is_code_address(value):
                references.setdefault(value, []).append(address)
    found = {}
    for target, sites in sorted(references.items()):
        if target in known or image.word(target) == 0:
            continue
        if PowerPc.ends_function(image.word(target - 4)):
            found[target] = f"referenced from 0x{sites[0]:08X}"
    return found


def candidates_from_code_gaps(image: GuestImage, known: set[int]) -> dict[int, str]:
    excluded = known | image.local_branch_targets()
    found = {}
    for section in image.executable_sections():
        for address in range(section.start + 4, section.start + section.size, 4):
            previous = image.word(address - 4)
            if address in excluded or image.word(address) == 0 or previous == 0:
                continue
            if PowerPc.ends_function(previous):
                found[address] = f"follows 0x{previous:08X}"
    return found


def candidates_from_code_constants(image: GuestImage, function_starts: set[int]) -> dict[int, str]:
    # A function whose address is only built in code (lis rN,hi / addi rN,rN,lo)
    # and passed on or called through ctr is invisible to the data scan, and is
    # often a label inside the function before it (a stub that falls into it).
    found = {}
    for address, target, base_register in image.code_address_constants():
        if target in function_starts or target in found or image.word(target) == 0:
            continue
        if image.is_code_address(image.word(target)) or image.used_as_base(address, base_register):
            continue  # a jump table, computed local jump or data kept in the text section
        if PowerPc.ends_function(image.word(target - 4)) or PowerPc.is_unconditional_branch(image.word(target - 4)):
            found[target] = f"address built at 0x{address:08X}"
    return found


def append_seeds(project: RecompProject, candidates: dict[int, str]) -> int:
    existing = project.seeds()
    # Codegen rejects a seed that falls inside a function given explicit bounds.
    new_lines = [f'"0x{address:08X}" = {{}}' for address in sorted(candidates)
                 if address not in existing and not project.inside_explicit_range(address)]
    if new_lines:
        text = project.functions_config.read_text().rstrip("\n")
        project.functions_config.write_text(text + "\n" + "\n".join(new_lines) + "\n", newline="\n")
    return len(new_lines)


def main():
    parser = argparse.ArgumentParser(description="Find guest functions that codegen did not discover.")
    parser.add_argument("--game", required=True, help="game folder, e.g. fightNight4")
    parser.add_argument("--dump", type=Path, help="image dump written with RECOMP_DUMP_IMAGE")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--gaps", action="store_true", help="scan unreached code after blr/bctr/b")
    mode.add_argument("--code-refs", action="store_true",
                      help="scan code for lis/addi pairs that build a function address")
    parser.add_argument("--write", action="store_true", help="append candidates to config/functions.toml")
    args = parser.parse_args()

    project = RecompProject(args.game)
    image = GuestImage(args.dump or project.default_image_dump)
    known = set(project.function_starts()) | project.branch_labels() | project.disabled_seeds()
    if args.code_refs:
        candidates = candidates_from_code_constants(image, set(project.function_starts()))
    elif args.gaps:
        candidates = candidates_from_code_gaps(image, known)
    else:
        candidates = candidates_from_data_pointers(image, known)

    print(f"{len(candidates)} candidate function starts")
    for address, reason in list(sorted(candidates.items()))[:40]:
        print(f"  0x{address:08X}  {reason}")
    if args.write:
        print(f"added {append_seeds(project, candidates)} seeds to {project.functions_config}")


if __name__ == "__main__":
    main()
