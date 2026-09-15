"""Tests for the codegen analysis scripts, using a synthetic guest image and project.

Run from the framework root: python -m pytest scripts/tests
"""
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "analysis"))

import recomp_project  # noqa: E402
from find_missing_functions import candidates_from_code_constants, candidates_from_data_pointers  # noqa: E402
from find_setjmp import find_jump_functions  # noqa: E402
from find_short_switch_tables import short_tables  # noqa: E402
from prune_bad_seeds import seeds_between, split_branches, unresolved_in_sources  # noqa: E402
from recomp_project import GuestImage, PowerPc, RecompProject  # noqa: E402
from stabilize_codegen import output_by_module  # noqa: E402

BASE = 0x82000000
TEXT = BASE + 0x10000
DATA = BASE + 0x20000
BLR = 0x4E800020


def lis(register: int, value: int) -> int:
    return (15 << 26) | (register << 21) | ((value >> 16) & 0xFFFF)


def addi(destination: int, source: int, value: int) -> int:
    return (14 << 26) | (destination << 21) | (source << 16) | (value & 0xFFFF)


def add(destination: int, left: int, right: int) -> int:
    return (31 << 26) | (destination << 21) | (left << 16) | (right << 11) | (266 << 1)


def lwz(destination: int, offset: int, base: int) -> int:
    return (32 << 26) | (destination << 21) | (base << 16) | (offset & 0xFFFF)


def split_address(value: int) -> tuple[int, int]:
    """lis/addi operands that rebuild `value` (addi sign-extends its immediate)."""
    low = value & 0xFFFF
    high = (value >> 16) + (1 if low & 0x8000 else 0)
    return (high << 16) & 0xFFFFFFFF, low


class ImageBuilder:
    def __init__(self, image_base: int = BASE):
        self.base = image_base
        self.bytes = bytearray(0x30000)
        self._write_pe_header()

    def _write_pe_header(self):
        pe = 0x40
        self.bytes[0:2] = b"MZ"
        struct.pack_into("<I", self.bytes, 0x3C, pe)
        self.bytes[pe:pe + 4] = b"PE\0\0"
        struct.pack_into("<HH", self.bytes, pe + 4, 0x1F2, 2)          # machine, section count
        struct.pack_into("<H", self.bytes, pe + 20, 224)                 # optional header size
        struct.pack_into("<H", self.bytes, pe + 24, 0x10B)               # PE32
        struct.pack_into("<I", self.bytes, pe + 24 + 28, self.base)      # ImageBase
        table = pe + 24 + 224
        for index, (name, rva, flags) in enumerate([(b".text", 0x10000, 0x20000020), (b".data", 0x20000, 0x40000040)]):
            entry = table + index * 40
            self.bytes[entry:entry + 8] = name.ljust(8, b"\0")
            struct.pack_into("<II", self.bytes, entry + 8, 0x1000, rva)
            struct.pack_into("<I", self.bytes, entry + 36, flags)

    def word(self, address: int, value: int):
        struct.pack_into(">I", self.bytes, address - self.base, value)

    def words(self, address: int, values: list[int]):
        for index, value in enumerate(values):
            self.word(address + 4 * index, value)

    def save(self, path: Path) -> GuestImage:
        path.write_bytes(bytes(self.bytes))
        return GuestImage(path)


@pytest.fixture
def game(tmp_path, monkeypatch):
    """A minimal game folder with an entrypoint and one DLL module."""
    root = tmp_path / "game"
    (root / "config").mkdir(parents=True)
    (root / "CMakeLists.txt").write_text("")
    (root / "config" / "functions.toml").write_text("[functions]\n")
    (root / "game_manifest.toml").write_text(
        '[project]\nname = "game"\n\n'
        '[entrypoint]\nfile_path = "assets/default.xex"\nout_directory_path = "generated/default"\n'
        'includes = [\n    "config/functions.toml",\n]\n\n'
        '[[modules]]\nguest_path = "Engine_DLL.xex"\nfile_path = "assets/Engine_DLL.xex"\n'
        'out_directory_path = "generated/Engine_DLL"\nincludes = []\n')
    monkeypatch.setattr(recomp_project, "REPOSITORY_ROOT", tmp_path)
    return root


def test_image_base_comes_from_the_pe_header(tmp_path):
    image = ImageBuilder(0x83000000)
    image.word(0x83010000, BLR)
    guest = image.save(tmp_path / "dll.bin")
    assert guest.base == 0x83000000
    assert guest.word(0x83010000) == BLR
    assert guest.is_code_address(0x83010004)
    assert not guest.is_code_address(0x83020000)


def test_data_pointer_after_return_is_a_candidate(tmp_path):
    image = ImageBuilder()
    image.words(TEXT, [BLR, 0x7C0802A6])          # blr, then an unseen function
    image.word(DATA, TEXT + 4)
    guest = image.save(tmp_path / "image.bin")
    assert TEXT + 4 in candidates_from_data_pointers(guest, known=set())
    assert candidates_from_data_pointers(guest, known={TEXT + 4}) == {}


def test_code_built_function_address_is_a_candidate(tmp_path):
    target = TEXT + 0x100
    high, low = split_address(target)
    image = ImageBuilder()
    image.words(TEXT, [lis(11, high), addi(4, 11, low), BLR])
    image.words(target - 4, [BLR, 0x7C0802A6])
    guest = image.save(tmp_path / "image.bin")
    assert target in candidates_from_code_constants(guest, function_starts=set())


def test_computed_jump_base_is_not_a_function(tmp_path):
    target = TEXT + 0x100
    high, low = split_address(target)
    image = ImageBuilder()
    image.words(TEXT, [lis(12, high), addi(12, 12, low), add(12, 12, 0), BLR])
    image.words(target - 4, [BLR, 0x7C0802A6])
    guest = image.save(tmp_path / "image.bin")
    assert candidates_from_code_constants(guest, function_starts=set()) == {}


def test_table_address_used_as_load_base_is_not_a_function(tmp_path):
    target = TEXT + 0x100
    high, low = split_address(target)
    image = ImageBuilder()
    image.words(TEXT, [lis(12, high), addi(12, 12, low), lwz(0, 0, 12), BLR])
    image.words(target - 4, [BLR, 0x7C0802A6])
    guest = image.save(tmp_path / "image.bin")
    assert candidates_from_code_constants(guest, function_starts=set()) == {}


def test_split_branch_warnings_include_plain_branches():
    log = ("Unresolved conditional branch to 0x82426694 from 0x82426684\n"
           "Jump target 0x82BC2164 unresolved at bctr 0x82BC20D4\n"
           "Unresolved b target 0x8232F344 from 0x8232F6A0\n")
    assert split_branches(log) == {(0x82426694, 0x82426684), (0x82BC2164, 0x82BC20D4), (0x8232F344, 0x8232F6A0)}


def test_seeds_between_branch_and_target_are_blamed(game):
    (game / "config" / "functions.toml").write_text('[functions]\n"0x82001000" = {}\n"0x82002000" = {}\n')
    project = RecompProject("game")
    blamed = seeds_between(project, {(0x82000F00, 0x82001100)})
    assert set(blamed) == {0x82001000}


def test_unresolved_stubs_are_read_from_generated_sources(game):
    generated = game / "generated" / "default"
    generated.mkdir(parents=True)
    (generated / "game_recomp.0.cpp").write_text(
        'REX_FATAL("Unresolved call from 0x8244E3C0 to 0x8244E37C");\n'
        'REX_FATAL("Unresolved branch from 0x8317F658 to 0x8317F634");\n')
    assert unresolved_in_sources(RecompProject("game")) == {(0x8244E37C, 0x8244E3C0), (0x8317F634, 0x8317F658)}


def test_explicit_ranges_come_from_end_bounds(game):
    (game / "config" / "functions.toml").write_text('[functions]\n"0x83024968" = { end = 0x83024D40 }\n"0x82001000" = {}\n')
    project = RecompProject("game")
    assert project.explicit_ranges() == [(0x83024968, 0x83024D40)]
    assert project.inside_explicit_range(0x83024B00)
    assert not project.inside_explicit_range(0x83024968)


def test_module_gets_its_own_config_and_manifest_include(game):
    module = RecompProject("game", "Engine_DLL")
    assert module.functions_config == game / "config" / "Engine_DLL" / "functions.toml"
    assert module.functions_config.read_text() == "[functions]\n"
    assert module.default_image_dump.name == "image_dump_Engine_DLL.bin"
    module.ensure_manifest_include(game / "config" / "Engine_DLL" / "switch_tables.toml")
    module.ensure_manifest_include(game / "config" / "Engine_DLL" / "switch_tables.toml")
    manifest = recomp_project.tomllib.loads((game / "game_manifest.toml").read_text())
    assert manifest["modules"][0]["includes"] == ["config/Engine_DLL/functions.toml", "config/Engine_DLL/switch_tables.toml"]
    assert manifest["entrypoint"]["includes"] == ["config/functions.toml"]


def test_unknown_module_is_rejected(game):
    with pytest.raises(SystemExit):
        RecompProject("game", "Missing_DLL")


def test_codegen_output_is_split_by_module(game):
    output = ("  start  default.xex\nUnexpected float16_4 pack instruction at 82000000\n"
              "  start  Engine_DLL.xex\n  0x83185C20 from 0x83185DA4: b 0x83185C20 from 0x83185DA4 - target not in any function\n"
              "Analysis failed for 'Engine_DLL'\n")
    sections = output_by_module(RecompProject("game"), output)
    assert "float16_4" in sections["default"]
    assert "0x83185C20" in sections["Engine_DLL"] and "0x83185C20" not in sections["default"]


def test_short_jump_table_is_found(game, tmp_path):
    function = TEXT
    bctr = TEXT + 0x14
    table = TEXT + 0x18
    case_label = TEXT + 0x2C
    high, low = split_address(table)
    image = ImageBuilder()
    image.words(table, [case_label] * 5 + [BLR])
    guest = image.save(tmp_path / "image.bin")
    generated = game / "generated" / "default"
    generated.mkdir(parents=True)
    (generated / "game_recomp.0.cpp").write_text(
        f"DEFINE_REX_FUNC(sub_{function:08X}) {{\n"
        f"\t// lis r12,{(high >> 16) - 0x10000 if high >> 16 & 0x8000 else high >> 16}\n"
        f"\t// addi r12,r12,{low - 0x10000 if low & 0x8000 else low}\n"
        "\t// rlwinm r0,r11,2,0,29\n\t// lwzx r0,r12,r0\n\t// mtctr r0\n\t// bctr \n"
        "\tswitch (ctx.r11.u32) {\n\tcase 0:\n\t\tgoto loc_x;\n\tcase 1:\n\t\tgoto loc_x;\n\tcase 2:\n\t\tgoto loc_x;\n"
        "\tdefault:\n\t\t__builtin_trap();\n\t}\n"
        f"loc_{case_label:08X}:\n\t// blr \n}}\n")
    tables = short_tables(RecompProject("game"), guest)
    assert len(tables) == 1
    assert tables[0].bctr == bctr
    assert tables[0].generated_cases == 3
    assert tables[0].labels == [case_label] * 5


def _float_lines(mnemonic: str, base: str, offset: int = 0) -> str:
    return "".join(f"\t// {mnemonic} f{14 + index},{offset + 8 * index}({base})\n" for index in range(18))


def test_setjmp_and_longjmp_are_found_by_jmp_buf_use(game):
    generated = game / "generated" / "default"
    generated.mkdir(parents=True)
    (generated / "game_recomp.0.cpp").write_text(
        "DEFINE_REX_FUNC(sub_82010000) {\n\t// mflr r0\n" + _float_lines("stfd", "r3")
        + "\t// std r1,144(r3)\n\t// blr \n}\n\n"
        # The CRT longjmp copies the jmp_buf pointer before restoring from it.
        + "DEFINE_REX_FUNC(sub_82010200) {\n\t// mr r7,r3\n" + _float_lines("lfd", "r7")
        + "\t// ld r1,144(r7)\n\t// blr \n}\n\n"
        # A context restore that reads from r4 is not the longjmp games call.
        + "DEFINE_REX_FUNC(sub_82010400) {\n" + _float_lines("lfd", "r4", 408)
        + "\t// ld r1,32(r4)\n\t// blr \n}\n\n"
        # Ordinary epilogues restore r1 from the stack.
        + "DEFINE_REX_FUNC(sub_82010600) {\n\t// lwz r1,0(r1)\n\t// blr \n}\n")
    found = find_jump_functions(RecompProject("game"))
    assert found.setjmp == [0x82010000]
    assert found.longjmp == [0x82010200]


def test_power_pc_decoding():
    assert PowerPc.is_lis(lis(11, 0x82000000))
    assert not PowerPc.is_lis(addi(11, 11, 4))
    assert PowerPc.signed_immediate(addi(3, 3, -8)) == -8
    assert PowerPc.is_add(add(12, 12, 0)) and PowerPc.add_operands(add(12, 12, 0)) == (12, 0)
    assert PowerPc.is_load(lwz(0, 4, 12)) and PowerPc.base_register(lwz(0, 4, 12)) == 12


def test_compare_runs_flags_a_large_frame_rate_drop():
    from compare_runs import compare

    def run(fps):
        return {"outcome": "ran for the full time", "seconds": 90, "errors": [], "warnings": [],
                "performance": {"average_fps": fps, "one_percent_low_fps": fps / 2}}

    assert compare(run(60.0), run(55.0))[1] is False
    assert compare(run(60.0), run(40.0))[1] is True
    # Reports from runtimes without frame stats still compare.
    assert compare({**run(60.0), "performance": None}, run(40.0))[1] is False


def test_gpu_trace_summary_flags_formats_and_names_skip_candidates():
    from summarize_gpu_trace import summarize

    def draw(ps, texture_format, pitch=1280):
        return {"frame": 1, "draw": 0, "skipped": False, "surface_pitch": pitch,
                "color_format": "k_8_8_8_8", "msaa": 1, "ps": ps,
                "textures": [{"format": texture_format}]}

    report = "\n".join(summarize([draw("AAAA", "k_DXT1"), draw("BBBB", "k_DXN"), draw("BBBB", "k_DXN", 640)]))
    assert "3 draws over 1 frame(s)" in report
    assert "k_DXN: 2  <- two-channel normal map" in report
    assert "--gpu_skip_pixel_shaders=BBBB" in report
    assert "AAAA" in report and "--gpu_skip_pixel_shaders=AAAA" not in report
