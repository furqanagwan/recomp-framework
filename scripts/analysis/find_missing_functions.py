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


def append_seeds(project: RecompProject, candidates: dict[int, str]) -> int:
    existing = project.seeds()
    new_lines = [f'"0x{address:08X}" = {{}}' for address in sorted(candidates) if address not in existing]
    if new_lines:
        text = project.functions_config.read_text().rstrip("\n")
        project.functions_config.write_text(text + "\n" + "\n".join(new_lines) + "\n", newline="\n")
    return len(new_lines)


def main():
    parser = argparse.ArgumentParser(description="Find guest functions that codegen did not discover.")
    parser.add_argument("--game", default="fightNight4", help="game folder, e.g. fightNight4")
    parser.add_argument("--dump", type=Path, help="image dump written with RECOMP_DUMP_IMAGE")
    parser.add_argument("--gaps", action="store_true", help="scan unreached code after blr/bctr/b")
    parser.add_argument("--write", action="store_true", help="append candidates to config/functions.toml")
    args = parser.parse_args()

    project = RecompProject(args.game)
    image = GuestImage(args.dump or project.default_image_dump)
    known = set(project.function_starts()) | project.branch_labels() | project.disabled_seeds()
    candidates = (candidates_from_code_gaps if args.gaps else candidates_from_data_pointers)(image, known)

    print(f"{len(candidates)} candidate function starts")
    for address, reason in list(sorted(candidates.items()))[:40]:
        print(f"  0x{address:08X}  {reason}")
    if args.write:
        print(f"added {append_seeds(project, candidates)} seeds to {project.functions_config}")


if __name__ == "__main__":
    main()
