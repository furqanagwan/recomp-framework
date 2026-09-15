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

The script runs `rexglue init`, renders `templates/game` and wires the codegen
config. Then:

1. `python framework/scripts/analysis/stabilize_codegen.py --game <FOLDER>` runs
   codegen until it is clean: it seeds unresolved call targets and disables seeds
   that split functions, recording them in `config/disabled_function_seeds.txt`.
2. Dump the loaded image with `RECOMP_DUMP_IMAGE=<FOLDER>/out/image_dump.bin`, run
   `python framework/scripts/analysis/find_missing_functions.py --game <FOLDER> --write`
   and again with `--gaps` and with `--code-refs` (functions whose address is only
   built in code, the usual cause of `Call to invalid or unregistered function`), then
   `python framework/scripts/analysis/prune_bad_seeds.py --game <FOLDER> --image <dump>`
   to drop gap seeds that split loops. Repeat step 1.
3. `python framework/scripts/analysis/find_short_switch_tables.py --game <FOLDER> --write`
   finds jump tables codegen sized too small (the game dies with an illegal
   instruction, `0xC000001D`, on a switch's out-of-range trap) and writes
   `config/switch_tables.toml`; add it to the manifest `includes` and run codegen
   again.
4. Missing kernel imports at link time become stubs: in
   `framework/common/src/kernel` when several games share them, otherwise in the
   game's `src/kernel`.
5. Artwork: `rexglue init --project-name <name> --xex-path <FOLDER>\assets\default.xex achievements <FOLDER>\assets\default.xex <FOLDER>\metadata`,
   upscale `metadata/icons/title.png` to `metadata/gdk_hd/title_1024.png`
   (Real-ESRGAN `realesrgan-x4plus`, 4x twice), run
   `.\framework\scripts\generate_artwork.ps1 -Game <FOLDER> -ProjectName <name>`,
   and copy the 1024 image to `<FOLDER>/docs/icon.png`.
6. Write `<FOLDER>/README.md` (copy an existing game's) and keep research notes
   in `<FOLDER>/docs/NOTES.md`.

Other codegen overrides (`switch_tables`, `midasm_hook`, `indirect_calls`,
`invalid_instructions`, `rexcrt`) follow `rex::codegen::RecompilerConfig`; add a
TOML file under `config/` and list it in the manifest `includes`.

### Games with DLL modules

Some games keep their code in guest DLLs (Top Spin 4: `Loader_DLL.xex`,
`Swing_DLL.xex`). `rexglue init --scan-dll` only finds `.dll` files, so add other
modules to the manifest by hand, using the path the game loads them from (build
and run the executable alone; the log shows the failed load):

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
  columns): `clang-format -i <files>`.
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
