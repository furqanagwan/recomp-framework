"""Find a module's CRT setjmp and longjmp so codegen can map them to the host's.

Without setjmp_address/longjmp_address, a guest longjmp restores the guest stack
pointer but the host call stack keeps going: the "call" returns into a frame the
guest has already unwound, and the function crashes on garbage in a nonvolatile
register. libjpeg's error_exit is a typical caller (Top Spin 4 crashed this way in
its JPEG marker reader).

The Xbox 360 CRT functions are recognised by what they do to the jmp_buf passed in
r3: setjmp saves r1 and the nonvolatile floats f14-f31 into it, longjmp reloads
them from it. A second restore routine reloads from a context in r4; it is not
the one games call, so only restores based on r3 (or a copy of it) count.
"""
import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from recomp_project import RecompProject

FUNCTION = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)")
INSTRUCTION = re.compile(r"^\t// (\S+)(?: (.*))?$")
NONVOLATILE_FLOAT = re.compile(r"^f(1[4-9]|2\d|3[01]),-?\d+\((r\d+)\)$")
STACK_POINTER = re.compile(r"^r1,-?\d+\((r\d+)\)$")
COPY = re.compile(r"^(r\d+),(r\d+)$")
NONVOLATILE_FLOATS = 18


@dataclass
class JumpFunctions:
    setjmp: list[int]
    longjmp: list[int]


def _buffer_registers(instructions: list[tuple[str, str]]) -> set[str]:
    """r3 and every register the function copies it into with mr."""
    registers = {"r3"}
    for mnemonic, operands in instructions:
        if mnemonic == "mr" and (match := COPY.match(operands)) and match.group(2) in registers:
            registers.add(match.group(1))
    return registers


def classify(instructions: list[tuple[str, str]]) -> str | None:
    buffers = _buffer_registers(instructions)

    def float_count(mnemonic: str) -> int:
        return sum(1 for op, operands in instructions
                   if op == mnemonic and (match := NONVOLATILE_FLOAT.match(operands))
                   and match.group(2) in buffers)

    def touches_stack_pointer(mnemonics: tuple[str, ...]) -> bool:
        return any(op in mnemonics and (match := STACK_POINTER.match(operands))
                   and match.group(1) in buffers for op, operands in instructions)

    if float_count("stfd") >= NONVOLATILE_FLOATS and touches_stack_pointer(("std", "stw")):
        return "setjmp"
    if float_count("lfd") >= NONVOLATILE_FLOATS and touches_stack_pointer(("ld", "lwz")):
        return "longjmp"
    return None


def find_jump_functions(project: RecompProject) -> JumpFunctions:
    found = JumpFunctions([], [])

    def finish(start, instructions):
        kind = classify(instructions) if start is not None else None
        if kind:
            getattr(found, kind).append(start)

    for source in project.recompiled_sources():
        start, instructions = None, []
        for line in source.read_text(errors="replace").splitlines():
            if match := FUNCTION.match(line):
                finish(start, instructions)
                start, instructions = int(match.group(1), 16), []
            elif start is not None and (match := INSTRUCTION.match(line)):
                instructions.append((match.group(1), match.group(2) or ""))
        finish(start, instructions)
    found.setjmp.sort()
    found.longjmp.sort()
    return found


def write_config(project: RecompProject, found: JumpFunctions) -> Path:
    config = project.functions_config.parent / "setjmp.toml"
    config.write_text(
        "# The CRT setjmp/longjmp, mapped to the host's by codegen. Without them a guest\n"
        "# longjmp returns into an unwound host frame. Found with find_setjmp.py.\n"
        f"setjmp_address = 0x{found.setjmp[0]:08X}\n"
        f"longjmp_address = 0x{found.longjmp[0]:08X}\n", newline="\n")
    return config


def main():
    parser = argparse.ArgumentParser(description="Find the CRT setjmp and longjmp in generated code.")
    parser.add_argument("--game", required=True, help="game folder, e.g. topspin4")
    parser.add_argument("--module", default="default",
                        help="DLL module to work on, named after its generated folder (e.g. Loader_DLL)")
    parser.add_argument("--write", action="store_true",
                        help="write config/setjmp.toml and list it in the manifest includes")
    args = parser.parse_args()

    project = RecompProject(args.game, args.module)
    found = find_jump_functions(project)
    print(f"setjmp: {', '.join(f'0x{a:08X}' for a in found.setjmp) or 'none'}")
    print(f"longjmp: {', '.join(f'0x{a:08X}' for a in found.longjmp) or 'none'}")
    if len(found.setjmp) > 1 or len(found.longjmp) > 1:
        raise SystemExit("more than one candidate; pick the right pair by hand")
    if bool(found.setjmp) != bool(found.longjmp):
        raise SystemExit("found only one of the pair; check the generated code by hand")
    if args.write and found.setjmp:
        config = write_config(project, found)
        project.ensure_manifest_include(config)
        print(f"wrote {config} and listed it in the manifest includes")


if __name__ == "__main__":
    main()
