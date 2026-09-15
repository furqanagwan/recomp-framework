import argparse
import json
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from recomp_project import RecompProject


def main():
    parser = argparse.ArgumentParser(
        description="Print a game's modules as JSON: module name -> guest file the game loads ('' for the executable).")
    parser.add_argument("--game", required=True, help="game folder, e.g. topspin4")
    args = parser.parse_args()

    project = RecompProject(args.game)
    manifest = tomllib.loads(project.manifest_path.read_text())
    modules = {RecompProject.DEFAULT_MODULE: ""}
    modules.update({Path(entry["out_directory_path"]).name: Path(entry["guest_path"]).name
                    for entry in manifest.get("modules", [])})
    print(json.dumps(modules))


if __name__ == "__main__":
    main()
