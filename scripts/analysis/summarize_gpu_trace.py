"""Summarise a gpu_trace.jsonl written by the SDK's gpu_trace_frame cvar.

Run the game with, for example:
  run_game.ps1 -Game fightNightChampion -GameArgs '--gpu_trace_frame=1800','--gpu_trace_path=trace.jsonl'
then:
  python summarize_gpu_trace.py <build dir>/trace.jsonl

The summary lists each pass (render target size and format) with its draws, the
pixel shaders by draw count, and texture formats. Formats whose conversion is a
common source of colour bugs are flagged: a green or magenta tint usually means a
YUV or two-channel normal map texture reached a shader as colour. To find the draw
behind an artifact, skip its pixel shader with --gpu_skip_pixel_shaders=<hash> and
compare screenshots.
"""
import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

# Formats that need a conversion the host does in a shader or on upload; wrong
# swizzles or decoders show up as tinted or blocky output.
SUSPICIOUS_FORMATS = {
    "k_Y1_Cr_Y0_Cb_REP": "packed YUV (video); wrong decode tints green or magenta",
    "k_Cr_Y1_Cb_Y0_REP": "packed YUV (video); wrong decode tints green or magenta",
    "k_DXN": "two-channel normal map; blue must be reconstructed",
    "k_CTX1": "two-channel normal map, converted on upload",
    "k_DXT3A": "alpha-only DXT3 block, expanded on upload",
    "k_DXT3A_AS_1_1_1_1": "alpha-only DXT3 block, expanded on upload",
    "k_DXT5A": "single-channel DXT5 block, expanded on upload",
}


def load(path: Path) -> list[dict]:
    draws = []
    for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        if not line.strip():
            continue
        try:
            draws.append(json.loads(line))
        except json.JSONDecodeError:
            print(f"line {number}: not JSON (trace cut off?)", file=sys.stderr)
    return draws


def summarize(draws: list[dict]) -> list[str]:
    lines = []
    frames = sorted({draw["frame"] for draw in draws})
    lines.append(f"{len(draws)} draws over {len(frames)} frame(s)"
                 f" ({sum(d['skipped'] for d in draws)} skipped)")

    passes = defaultdict(list)
    for draw in draws:
        passes[(draw["surface_pitch"], draw["color_format"], draw["msaa"])].append(draw)
    lines.append("")
    lines.append("Passes (surface width, colour format, MSAA): draws")
    for (pitch, color_format, msaa), members in sorted(passes.items(), key=lambda item: -len(item[1])):
        lines.append(f"  {pitch:>5} {color_format:<28} x{msaa}: {len(members)}")

    shaders = Counter(draw["ps"] or "(none)" for draw in draws)
    lines.append("")
    lines.append("Pixel shaders by draws")
    for shader, count in shaders.most_common(15):
        lines.append(f"  {shader}: {count}")

    formats = Counter()
    shaders_by_format = defaultdict(set)
    for draw in draws:
        for texture in draw["textures"]:
            formats[texture["format"]] += 1
            shaders_by_format[texture["format"]].add(draw["ps"] or "(none)")
    lines.append("")
    lines.append("Texture formats by bindings")
    for texture_format, count in formats.most_common():
        note = SUSPICIOUS_FORMATS.get(texture_format)
        lines.append(f"  {texture_format}: {count}" + (f"  <- {note}" if note else ""))

    flagged = [name for name in formats if name in SUSPICIOUS_FORMATS]
    if flagged:
        lines.append("")
        lines.append("Shaders sampling flagged formats (skip candidates)")
        for texture_format in flagged:
            hashes = ",".join(sorted(shaders_by_format[texture_format]))
            lines.append(f"  {texture_format}: --gpu_skip_pixel_shaders={hashes}")
    return lines


def main():
    parser = argparse.ArgumentParser(description="Summarise an SDK gpu_trace.jsonl.")
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    print("\n".join(summarize(load(args.trace))))


if __name__ == "__main__":
    main()
