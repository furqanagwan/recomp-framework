import argparse
import bisect
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from recomp_project import GuestImage, RecompProject

SPLIT_BRANCH_WARNINGS = [
    re.compile(r"Unresolved conditional branch to 0x([0-9A-F]{8}) from 0x([0-9A-F]{8})"),
    re.compile(r"Jump target 0x([0-9A-F]{8}) unresolved at bctr 0x([0-9A-F]{8})"),
]


def split_branches(log_text: str) -> set[tuple[int, int]]:
    return {(int(target, 16), int(source, 16))
            for pattern in SPLIT_BRANCH_WARNINGS for target, source in pattern.findall(log_text)}


def containing_function(starts: list[int], address: int) -> int | None:
    index = bisect.bisect_right(starts, address) - 1
    return starts[index] if index >= 0 else None


def seeds_splitting_functions(project: RecompProject, branches: set[tuple[int, int]]) -> dict[int, str]:
    seeds = project.seeds()
    starts = project.function_starts()
    blamed = {}
    for target, source in branches:
        low, high = sorted((target, source))
        culprits = {seed for seed in seeds if low < seed <= high}
        culprits |= {start for start in (containing_function(starts, source), containing_function(starts, target))
                     if start in seeds}
        for seed in culprits:
            blamed.setdefault(seed, f"splits 0x{source:08X} -> 0x{target:08X}")
    return blamed


def seeds_on_local_branch_targets(project: RecompProject, image: GuestImage) -> dict[int, str]:
    referenced_from_data = image.data_pointer_targets()
    targets = image.local_branch_targets()
    return {seed: "is a local branch target"
            for seed in project.seeds() if seed in targets and seed not in referenced_from_data}


def disable_seeds(project: RecompProject, blamed: dict[int, str]) -> None:
    lines = project.functions_config.read_text().splitlines()
    removed = {f'"0x{seed:08X}"' for seed in blamed}
    kept = [line for line in lines if line.split(" = ", 1)[0] not in removed]
    project.functions_config.write_text("\n".join(kept) + "\n", newline="\n")
    with project.disabled_seeds_log.open("a", newline="\n") as log:
        for seed in sorted(blamed):
            log.write(f"0x{seed:08X} {blamed[seed]}\n")


def main():
    parser = argparse.ArgumentParser(description="Remove function seeds that split real functions.")
    parser.add_argument("codegen_log", type=Path, nargs="?", help="output of rexglue codegen")
    parser.add_argument("--image", type=Path,
                        help="image dump; also disable seeds that plain branches jump to")
    parser.add_argument("--game", default="fightNight4", help="game folder, e.g. fightNight4")
    args = parser.parse_args()

    project = RecompProject(args.game)
    blamed = {}
    if args.codegen_log:
        branches = split_branches(args.codegen_log.read_text(errors="replace"))
        blamed.update(seeds_splitting_functions(project, branches))
        print(f"{len(branches)} split branches")
    if args.image:
        blamed.update(seeds_on_local_branch_targets(project, GuestImage(args.image)))
    disable_seeds(project, blamed)
    print(f"{len(blamed)} seeds disabled")


if __name__ == "__main__":
    main()
