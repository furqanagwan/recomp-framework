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
    # A plain branch into the middle of another function: usually a gap seed
    # placed after a bctr that is really the rest of the function before it.
    re.compile(r"Unresolved b target 0x([0-9A-F]{8}) from 0x([0-9A-F]{8})"),
]


def split_branches(log_text: str) -> set[tuple[int, int]]:
    return {(int(target, 16), int(source, 16))
            for pattern in SPLIT_BRANCH_WARNINGS for target, source in pattern.findall(log_text)}


GENERATED_UNRESOLVED = re.compile(
    r'REX_FATAL\("Unresolved (?:call|branch) from 0x([0-9A-F]{8}) to 0x([0-9A-F]{8})"\)')


def unresolved_in_sources(project: RecompProject) -> set[tuple[int, int]]:
    """(target, source) of branches codegen left as fatal stubs, from the generated code itself.
    Codegen only logs these for files it rewrites, so a later no-op pass hides them."""
    found = set()
    for source in project.recompiled_sources():
        for origin, target in GENERATED_UNRESOLVED.findall(source.read_text()):
            found.add((int(target, 16), int(origin, 16)))
    return found


def seeds_between(project: RecompProject, branches: set[tuple[int, int]]) -> dict[int, str]:
    """Seeds strictly between a branch and its target: they cut one function in two."""
    # Bounds written out by hand say where a function really begins and ends; a
    # split around one means those bounds are wrong, not that the entry should go.
    seeds = project.seeds() - project.bounded_seeds()
    blamed = {}
    for target, source in branches:
        low, high = sorted((target, source))
        for seed in seeds:
            if low < seed <= high:
                blamed.setdefault(seed, f"splits 0x{source:08X} -> 0x{target:08X}")
    return blamed


def containing_function(starts: list[int], address: int) -> int | None:
    index = bisect.bisect_right(starts, address) - 1
    return starts[index] if index >= 0 else None


def seeds_splitting_functions(project: RecompProject, branches: set[tuple[int, int]]) -> dict[int, str]:
    # Bounds written out by hand say where a function really begins and ends; a
    # split around one means those bounds are wrong, not that the entry should go.
    seeds = project.seeds() - project.bounded_seeds()
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
    # Keep seeds whose address is taken, from data or built in code: those are
    # real entry points even when a neighbouring stub also branches into them.
    address_taken = image.data_pointer_targets() | image.function_address_constants()
    targets = image.local_branch_targets()
    return {seed: "is a local branch target"
            for seed in project.seeds() - project.bounded_seeds()
            if seed in targets and seed not in address_taken}


def disable_seeds(project: RecompProject, blamed: dict[int, str]) -> None:
    bounded = project.bounded_seeds()
    dropped = sorted(seed for seed in blamed if seed not in bounded)
    if not dropped:
        return
    lines = project.functions_config.read_text().splitlines()
    removed = {f'"0x{seed:08X}"' for seed in dropped}
    kept = [line for line in lines if line.split(" = ", 1)[0] not in removed]
    project.functions_config.write_text("\n".join(kept) + "\n", newline="\n")
    with project.disabled_seeds_log.open("a", newline="\n") as log:
        for seed in dropped:
            log.write(f"0x{seed:08X} {blamed[seed]}\n")


def main():
    parser = argparse.ArgumentParser(description="Remove function seeds that split real functions.")
    parser.add_argument("codegen_log", type=Path, nargs="?", help="output of rexglue codegen")
    parser.add_argument("--image", type=Path,
                        help="image dump; also disable seeds that plain branches jump to")
    parser.add_argument("--game", required=True, help="game folder, e.g. fightNight4")
    parser.add_argument("--module", default="default",
                        help="DLL module to work on, named after its generated folder (e.g. Loader_DLL)")
    args = parser.parse_args()

    project = RecompProject(args.game, args.module)
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
