"""Summarise a gpu_trace.jsonl written by the SDK's gpu_trace_frame cvar.

Run the game with, for example:
  run_game.ps1 -Game fightNightChampion -GameArgs '--gpu_trace_frame=1800','--gpu_trace_path=trace.jsonl'
then:
  python summarize_gpu_trace.py <build dir>/trace.jsonl

The summary lists each pass (render target size and format) with its draws, the
shader programs and pixel shaders by draw count, and texture formats. With
--gpu_trace_constants=N it also reports, per shader program, which of the traced
vertex constants hold still and which change per draw: the still ones are the
camera and projection candidates a native renderer sets once a frame, the moving
ones its per-object transforms. Formats whose conversion is a
common source of colour bugs are flagged: a green or magenta tint usually means a
YUV or two-channel normal map texture reached a shader as colour. To find the draw
behind an artifact, skip its pixel shader with --gpu_skip_pixel_shaders=<hash> and
compare screenshots.

With --gpu_trace_submitters=true each draw also carries the guest code that wrote
its packet, and the summary groups a shader program's draws by it: that is the
engine function a native renderer hooks. Look the addresses up in the game's
functions.toml or its .map file; they are return addresses, so each one is a few
instructions past the call, inside the calling function.
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


def constants_report(draws: list[dict]) -> list[str]:
    """Per shader program, which traced vertex constants hold still and which move.

    A constant that never changes over a program's draws is a candidate for the
    camera or projection the game sets once a frame; one that changes per draw is
    a candidate for that draw's own transform. A constant that holds within each
    frame but differs between frames is a camera that moved.
    """
    by_program = defaultdict(list)
    for draw in draws:
        if draw.get("constants"):
            by_program[(draw.get("vs") or "(none)", draw["ps"] or "(none)")].append(draw)
    if not by_program:
        return []

    lines = ["", "Vertex constants (run with --gpu_trace_constants=N)"]
    for (vertex, pixel), members in sorted(by_program.items(), key=lambda item: -len(item[1])):
        vectors = min(len(draw["constants"]) for draw in members)
        still, per_frame, per_draw = [], [], []
        for vector in range(vectors):
            values = {tuple(draw["constants"][vector]) for draw in members}
            if len(values) == 1:
                still.append(vector)
                continue
            within_frame = defaultdict(set)
            for draw in members:
                within_frame[draw["frame"]].add(tuple(draw["constants"][vector]))
            (per_frame if all(len(v) == 1 for v in within_frame.values()) else per_draw).append(vector)
        lines.append(f"  {vertex}:{pixel} ({len(members)} draws, c0-c{vectors - 1})")
        for label, group in (("same every draw", still), ("same within a frame", per_frame),
                             ("per draw", per_draw)):
            if group:
                lines.append(f"    {label}: {format_vectors(group)}")
    return lines


def format_vectors(vectors: list[int]) -> str:
    """Constant vector numbers as ranges: [0,1,2,5] -> "c0-c2, c5"."""
    parts = []
    start = previous = vectors[0]
    for vector in vectors[1:] + [None]:
        if vector == previous + 1:
            previous = vector
            continue
        parts.append(f"c{start}" if start == previous else f"c{start}-c{previous}")
        start = previous = vector
    return ", ".join(parts)


def submitters_report(draws: list[dict]) -> list[str]:
    """Per shader program, the guest code that wrote its draws' packets.

    The addresses are sampled per command buffer page, so a stack is the code
    that first wrote into the page a draw's packet sits in. One draw's stack can
    therefore belong to the draw before it; a program whose draws nearly all
    report the same stack has been submitted by that code, which is the reading
    this is for.
    """
    tagged = [draw for draw in draws if draw.get("submitter")]
    if not tagged:
        return []

    lines = ["", "Submitters (run with --gpu_trace_submitters=true)"]
    by_program = defaultdict(Counter)
    for draw in tagged:
        program = (draw.get("vs") or "(none)", draw["ps"] or "(none)")
        by_program[program][tuple(draw["submitter"])] += 1
    for (vertex, pixel), stacks in sorted(by_program.items(), key=lambda item: -sum(item[1].values())):
        lines.append(f"  {vertex}:{pixel}: {sum(stacks.values())} draws from {len(stacks)} site(s)")
        for stack, count in stacks.most_common(3):
            share = 100.0 * count / sum(stacks.values())
            lines.append(f"    {count:>5} ({share:.0f}%)  {' <- '.join(stack[:6])}")
    missing = len(draws) - len(tagged)
    if missing:
        lines.append(f"  {missing} draw(s) with no sample: their page was written before the"
                     " watch was armed")
    return lines


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

    programs = Counter((draw.get("vs") or "(none)", draw["ps"] or "(none)") for draw in draws)
    lines.append("")
    lines.append("Shader programs (vertex, pixel) by draws")
    for (vertex, pixel), count in programs.most_common(15):
        lines.append(f"  {vertex}:{pixel}: {count}")
    if programs:
        selection = ",".join(f"{vertex}:{pixel}" for (vertex, pixel), _ in programs.most_common(3))
        lines.append(f"  trace only these: --gpu_trace_shaders={selection}")

    lines.extend(constants_report(draws))
    lines.extend(submitters_report(draws))

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
