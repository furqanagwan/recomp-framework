import argparse
import json
import sys
from pathlib import Path


def load(run: Path) -> dict:
    summary = run / "summary.json" if run.is_dir() else run
    return json.loads(summary.read_text(encoding="utf-8-sig"))


def describe(summary: dict) -> str:
    crash = summary.get("crash")
    text = f"{summary['outcome']} after {summary['seconds']} s"
    if crash:
        text += f", {crash['code']} in {crash['module']}"
        if crash.get("location"):
            text += f" at {crash['location'].split(' <- ')[0]}"
    return text


RANK = {"ran for the full time": 3, "exited normally": 2, "exited with an error": 1, "crashed": 0}
FPS_REGRESSION_RATIO = 0.8


def compare(before: dict, after: dict) -> tuple[list[str], bool]:
    """Returns report lines and whether `after` is a regression."""
    lines = [f"before: {describe(before)}", f"after:  {describe(after)}"]
    regression = RANK.get(after["outcome"], 0) < RANK.get(before["outcome"], 0)
    if after["outcome"] == before["outcome"] == "crashed" and after["seconds"] < before["seconds"] * 0.8:
        regression = True
        lines.append("crashes earlier than before")
    new_errors = sorted(set(after.get("errors", [])) - set(before.get("errors", [])))
    fixed_errors = sorted(set(before.get("errors", [])) - set(after.get("errors", [])))
    lines += [f"new error: {error}" for error in new_errors]
    lines += [f"gone:      {error}" for error in fixed_errors]
    before_warnings = {w["message"] for w in before.get("warnings", [])}
    lines += [f"new frequent warning: {w['count']} x {w['message']}"
              for w in after.get("warnings", []) if w["message"] not in before_warnings]
    before_perf, after_perf = before.get("performance"), after.get("performance")
    if before_perf and after_perf:
        lines.append(f"frame rate: {before_perf['average_fps']} -> {after_perf['average_fps']} fps average, "
                     f"{before_perf['one_percent_low_fps']} -> {after_perf['one_percent_low_fps']} fps 1% low")
        # Runs stop at different points of a game's attract loop, so only a
        # large drop counts.
        if after_perf["average_fps"] < before_perf["average_fps"] * FPS_REGRESSION_RATIO:
            regression = True
            lines.append("frame rate dropped")
    return lines, regression or bool(new_errors)


def main():
    parser = argparse.ArgumentParser(
        description="Compare two run_game.ps1 reports (run folders or summary.json files).")
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args()

    lines, regression = compare(load(args.before), load(args.after))
    print("\n".join(lines))
    print("REGRESSION" if regression else "no regression")
    sys.exit(1 if regression else 0)


if __name__ == "__main__":
    main()
