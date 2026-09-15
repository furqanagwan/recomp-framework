# Contributing

This guide covers the framework and the game repositories that use it
([fightNightRecomped](https://github.com/furqanagwan/fightNightRecomped),
[liveRecomped](https://github.com/furqanagwan/liveRecomped),
[skateRecomped](https://github.com/furqanagwan/skateRecomped),
[topSpinRecomped](https://github.com/furqanagwan/topSpinRecomped)). Each game repository
links here from its own `CONTRIBUTING.md`.

## Ground rules

- **Never commit game data.** No ISOs, `default.xex`, extracted assets, generated
  code (`<GAME>/generated/*/`) or extracted metadata. `.gitignore` covers these;
  the one exception is each game's upscaled `docs/icon.png`.
- **You need your own copy of a game** to build or test it. Say in your pull
  request which games (and disc regions) you tested.
- **Don't break other games.** A framework or SDK change affects every game.
  Build at least one game you own against your change; if a change alters
  what games see at runtime (stub return values, file system, input), call it
  out so owners of the other games can test it.

## Setting up

### Windows

1. Install [Visual Studio 2022 or later](https://visualstudio.microsoft.com/)
   (Build Tools is enough) with the *Desktop development with C++* workload and
   a Windows 11 SDK.
2. Install [LLVM/Clang](https://github.com/llvm/llvm-project/releases) 18 or later,
   [CMake](https://cmake.org/download/) 3.25+, [Ninja](https://ninja-build.org/)
   and [Python](https://www.python.org/) 3.11+ (for the analysis scripts).
3. Optional: the [Microsoft GDK](https://github.com/microsoft/GDK) for Xbox PC app
   packaging.

### Linux and Steam Deck

Clang 20, CMake, Ninja and the Vulkan, GTK, Wayland and audio development
packages; `.github/workflows/common.yml` lists the exact apt packages.

### Clone and install the SDK

```
git clone --recursive https://github.com/furqanagwan/fightNightRecomped.git
cd fightNightRecomped\framework\thirdparty\rexglue-sdk
cmake --preset win-amd64 -DCMAKE_INSTALL_PREFIX=C:/ReXGlue
cmake --build --preset win-amd64-release
cmake --install out/build/win-amd64 --config Release
```

Games find the installed SDK at `C:\ReXGlue`. To build against the submodule
source instead, pass `-SdkDir framework\thirdparty\rexglue-sdk` to `build.ps1`
(or set `REXSDK_DIR` for `build.sh`). The UWP flavour installs to
`C:\ReXGlue-UWP` with `--preset win-amd64-uwp`.

## Building a game

Extract your disc into the game's `assets` folder, then build from the game
repository root:

```
rexglue extract "<your disc>.iso" <GAME>\assets
.\framework\scripts\build.ps1 -Game <GAME>
./framework/scripts/build.sh <GAME>
```

The first build runs codegen from `assets\default.xex`, which takes a few
minutes. The executable is written to `<GAME>\out\build\<preset>\`.

## Adding a game

```
rexglue extract "<disc>.iso" <FOLDER>\assets
.\framework\scripts\new_game.ps1 -Folder <FOLDER> -ProjectName <snake_case_name> `
    -DisplayName "<Display Name>" -ReleaseYear <year> -Label "EA SPORTS"
```

The script runs `rexglue init`, renders `templates/game` (including a README,
`release.json` and `docs/NOTES.md` with TODOs to fill in) and wires the codegen
config. Then:

1. `.\framework\scripts\discover_functions.ps1 -Game <FOLDER>` runs the whole
   function discovery workflow below for the executable and every DLL module,
   and builds the game.
2. `.\framework\scripts\run_game.ps1 -Game <FOLDER>` launches it and writes a
   report to `out\runs\<time>\`: outcome, screenshots, errors, frequent warnings
   and, for a crash, the faulting module and offset. With the
   `win-amd64-relwithdebinfo` preset the crash is resolved to the generated
   function and line.
3. Missing kernel imports at link time become stubs: in
   `framework/common/src/kernel` when several games share them, otherwise in the
   game's `src/kernel`.
4. Artwork: `rexglue init --project-name <name> --xex-path <FOLDER>\assets\default.xex achievements <FOLDER>\assets\default.xex <FOLDER>\metadata`,
   upscale `metadata/icons/title.png` to `metadata/gdk_hd/title_1024.png`
   (Real-ESRGAN `realesrgan-x4plus`, 4x twice), run
   `.\framework\scripts\generate_artwork.ps1 -Game <FOLDER> -ProjectName <name>`,
   and copy the 1024 image to `<FOLDER>/docs/icon.png`.
5. Fill in the TODOs in `<FOLDER>/README.md` and `release.json`, and keep research
   notes in `<FOLDER>/docs/NOTES.md`.

### What the discovery workflow does

Each step is a script in `framework/scripts/analysis` you can also run on its own:

| Step | Script | Fixes |
| --- | --- | --- |
| Stabilize | `stabilize_codegen.py` | Seeds unresolved call targets, disables seeds that split functions (recorded in `config/disabled_function_seeds.txt`), and checks the generated code for leftover unresolved-branch stubs |
| Dump | `RECOMP_DUMP_IMAGE=<file>` (plus `RECOMP_DUMP_MODULE=<Name.xex>` for a DLL) | Writes the loaded image for the scans |
| Scan | `find_missing_functions.py --write`, then with `--gaps` and `--code-refs` | Functions referenced from data, after returns, or whose address is only built in code (the usual cause of `Call to invalid or unregistered function`) |
| Prune | `prune_bad_seeds.py --image <dump>` | Gap seeds that split loops |
| Jump tables | `find_short_switch_tables.py --write` | Tables codegen sized too small (the game dies with `0xC000001D`, an illegal instruction, on a switch's out-of-range trap); adds `switch_tables.toml` to the manifest |

Some functions still need explicit bounds in `functions.toml`
(`"0xSTART" = { end = 0xEND }`): typically a leaf whose last block sits after its
`blr`, or a switch whose cases each return. The stabilizer reports these as
unresolved stubs with no seed to blame. `compare_runs.py` diffs two
`run_game.ps1` reports and exits non-zero on a regression.

Other codegen overrides (`switch_tables`, `midasm_hook`, `indirect_calls`,
`invalid_instructions`, `rexcrt`) follow `rex::codegen::RecompilerConfig`; add a
TOML file under `config/` and list it in the manifest `includes`.

### Games with DLL modules

Some games keep their code in guest DLLs (Top Spin 4: `Loader_DLL.xex`,
`Swing_DLL.xex`). `rexglue init --scan-dll` adds every DLL module under the game
root, `.dll` or `.xex`. The guest path it writes is the file's path under the game
root; if the game loads the module from somewhere else (build and run the
executable alone; the log shows the failed load), fix `guest_path` by hand:

```toml
[[modules]]
guest_path = "Loader_DLL.xex"
file_path = "assets/Loader_DLL.xex"
out_directory_path = "generated/Loader_DLL"
includes = []
```

Each module is named after its generated folder and gets its own
`config/<module>/functions.toml`, created and added to its `includes` the first
time a script touches it. `stabilize_codegen.py` handles every module in one run.
The other analysis scripts take `--module <name>` and read the module's dump,
written once the game loads it:

```
RECOMP_DUMP_IMAGE=<FOLDER>/out/image_dump_Loader_DLL.bin RECOMP_DUMP_MODULE=Loader_DLL.xex
python framework/scripts/analysis/find_missing_functions.py --game <FOLDER> --module Loader_DLL --write
```

## Code style

- C++23, formatted with the repository's `.clang-format` (Google-based, 100
  columns): `clang-format -i <files>`. The library builds without warnings; keep
  it that way.
- Analysis scripts need Python 3.11+ and have tests: `python -m pytest scripts/tests`.
  Add a test with a synthetic image when you change a scan.
- PowerShell scripts must run in Windows PowerShell 5.1: ASCII only, and native
  tools that log to stderr are checked by exit code.
- Match the surrounding code's naming and comment density; comments explain
  why, not what.
- Commit messages: a short imperative subject line, then a body saying why.

## Changing the framework or SDK

- Framework changes go to this repository; game repositories pick them up by
  updating their `framework` submodule.
- ReXGlue changes go to [furqanagwan/rexglue-sdk](https://github.com/furqanagwan/rexglue-sdk)
  (branch `main`). Prefer fixes that are behaviour-preserving for existing
  games, and send generally useful ones upstream to
  [rexglue/rexglue-sdk](https://github.com/rexglue/rexglue-sdk) as well.
- CI (`.github/workflows/common.yml`) builds `recomp_common` on Linux for every
  push. Games can't be built in CI because codegen needs the game's executable.

## Releases

Release builds are made locally by someone who owns the game, because codegen
needs `default.xex`. See the game repository's `CONTRIBUTING.md` for the release
checklist.
