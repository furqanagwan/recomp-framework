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
   function and line. On SDKs with frame stats it also reports average and 1% low
   frame rate, stalls and draws per frame.
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
| Dump | `rexglue codegen --dump-images <dir>` | Writes decrypted, decompressed and patched module images for the scans without launching the game; disc builds can fall back to `RECOMP_DUMP_IMAGE` at runtime |
| Scan | `find_missing_functions.py --write`, then with `--gaps` and `--code-refs` | Functions referenced from data, after returns, or whose address is only built in code (the usual cause of `Call to invalid or unregistered function`) |
| Prune | `prune_bad_seeds.py --image <dump>` | Gap seeds that split loops |
| Jump tables | `find_short_switch_tables.py --write` | Tables codegen sized too small (the game dies with `0xC000001D`, an illegal instruction, on a switch's out-of-range trap); adds `switch_tables.toml` to the manifest |
| setjmp | `find_setjmp.py --write` | The CRT `setjmp`/`longjmp`, written to `setjmp.toml`. Without them a guest `longjmp` returns into a host frame that no longer exists, and the function crashes on a zeroed register soon after (Top Spin 4 in libjpeg's error handler) |

Some functions still need explicit bounds in `functions.toml`
(`"0xSTART" = { end = 0xEND }`): typically a leaf whose last block sits after its
`blr`, or a switch whose cases each return. The stabilizer reports these as
unresolved stubs with no seed to blame.

Pruning leaves those bounds alone: an entry with `end =` states where a
function really begins and ends, so a split around one means the bounds are
wrong, not that the entry should go. The stabilizer keeps reporting the split
until they are right.

`compare_runs.py` diffs two
`run_game.ps1` reports and exits non-zero on a regression.

Other codegen overrides (`switch_tables`, `midasm_hook`, `indirect_calls`,
`invalid_instructions`, `rexcrt`) follow `rex::codegen::RecompilerConfig`; add a
TOML file under `config/` and list it in the manifest `includes`.

### Title updates

Title updates remain optional at the platform level. A particular recompilation
build targets either the original disc executable or one exact update, because
updated machine code cannot be mixed with code generated from the disc XEX.

For a disc build, leave `patched_file_path` out of the codegen manifest and
leave `GameDescriptor::title_update` empty. The runtime does not apply adjacent
`.xexp` files in this mode and refuses a game folder containing one.

For an update build:

1. Extract the update package to a private staging folder. Put an unmodified
   copy of each base XEX beside its matching `.xexp` in that folder.
2. Add `patched_file_path` to the entrypoint and every patched module in the
   codegen manifest. Codegen loads the staged XEX and applies its adjacent patch.
   `discover_functions.ps1` dumps that patched image offline, so the update does
   not need to be installed in the runtime. If a base XEX, XEXP, or a rexglue
   with `--dump-images` is missing, discovery stops immediately instead of
   waiting for a game-time dump that cannot pass the update installer.
3. Fill `GameDescriptor::title_update` with the update label, title ID, media ID,
   version, and the size and lowercase XXH3-128 digest of every required `.xexp`.
4. Give the update its own codegen config and rediscover its functions. An
   update moves code, so the disc build's seeds land in the middle of its
   functions and split them; the sources still compile, but every split leaves a
   `REX_FATAL` stub that kills the game when it is reached. Point the manifest's
   `includes` at a per-version folder (`config/tu3/functions.toml`, and
   `config/tu3/<module>/functions.toml` for a patched DLL), start it at
   `[functions]` and run `discover_functions.ps1` again. The analysis scripts
   read that path from the manifest, so `disabled_function_seeds.txt`,
   `setjmp.toml` and `switch_tables.toml` are written beside it and the disc
   build's `config/` is left alone. Check for leftovers before playing:
   `Select-String -Path <FOLDER>\generated\*\*.cpp -Pattern 'REX_FATAL\("Unresolved'`
   must find nothing.
5. Record the same update in the game's README and `release.json`.

On first launch, that build asks the player for their title update package (or
uses `RECOMP_INSTALL_TU`). It verifies the package and code-patch digests, then
extracts its code and data files under the user-data folder. Updated files are
served over the disc files, while files absent from the update fall back to the
disc. Update packages and extracted contents must never be committed or shipped.

## Debugging rendering

The SDK has cvars for rendering bugs that work in release builds; pass them with
`run_game.ps1 -GameArgs`:

| Cvar | Use |
| --- | --- |
| `--frame_stats_csv=<file>` | One line per guest frame (time, draws, resolves) and an FPS summary in the log. `run_game.ps1` sets it and summarises it |
| `--gpu_trace_frame=<N>` (`--gpu_trace_frame_count`, `--gpu_trace_path`) | One JSON line per draw of frame N: render target, shader hashes, texture formats and sizes |
| `--gpu_trace_shaders=<vs>[:<ps>],...` | Traces only these shader programs' draws. A busy frame's full trace is hundreds of megabytes; one geometry program is a few |
| `--gpu_trace_constants=<N>` | Records the first N vertex shader constant vectors of each traced draw: the transform chain |
| `--gpu_trace_vertex_buffers=true` | Records each traced draw's vertex buffers: fetch slot, guest address, size and stride |
| `--gpu_skip_pixel_shaders=<hash>,...` | Drops draws by pixel shader hash |

To find the draw behind an artifact, take a screenshot where it shows, trace that
frame, and summarise the trace:

```
.\framework\scripts\run_game.ps1 -Game <FOLDER> -Seconds 60 -Screenshots 55 -GameArgs '--gpu_trace_frame=3000'
python framework/scripts/analysis/summarize_gpu_trace.py <FOLDER>/out/build/win-amd64-release/gpu_trace.jsonl
```

The summary groups draws by pass, shader program and pixel shader, and flags
texture formats whose conversion commonly goes wrong (YUV, two-channel normal
maps). Rerun with `--gpu_skip_pixel_shaders` set to a suspect's hash; when the
artifact disappears from the screenshot, that shader's draws are the ones to
investigate.

### Finding a game's own geometry

A native renderer (see the SDK's `docs/native_rendering.md`) has to know which
draws are the game's world and where its transforms live. The summary's shader
programs, ordered by draws, name the candidates and print the
`--gpu_trace_shaders=` line that traces only those. Trace them again with
constants and vertex buffers:

```
.\framework\scripts\run_game.ps1 -Game <FOLDER> -Seconds 60 -Screenshots 55 -GameArgs '--gpu_trace_frame=3000','--gpu_trace_frame_count=30','--gpu_trace_shaders=<vs>:<ps>','--gpu_trace_constants=16','--gpu_trace_vertex_buffers=true'
```

The summary then reports, per program, which of those constants never change
(the projection the game sets once), which change only between frames (the
camera) and which change per draw (that object's own transform). A program whose
vertex shader reads no transform constants and fetches no textures is interface
or overlay geometry, not the world.

### Finding the function that submits a program's draws

Naming the shader program that draws the world is half the answer. A native
renderer replaces the engine's own submission, so it has to hook the function
that submits those meshes, and nothing in the trace says which one that is: the
command processor runs behind the game, so by the time it executes a draw packet
the guest thread that wrote the packet is elsewhere.

`--gpu_trace_submitters=true` answers it from the other end. While a trace is
open, the buffers the game builds commands in are write-watched; the first write
to each page faults on the guest thread, where the call stack still says which
code is submitting, and the stack is kept against that page. Each traced draw
then carries the stack covering its packet:

```
.\framework\scripts\run_game.ps1 -Game <FOLDER> -Seconds 60 -Screenshots 55 -GameArgs '--gpu_trace_frame=3000','--gpu_trace_frame_count=30','--gpu_trace_shaders=<vs>:<ps>','--gpu_trace_submitters=true'
```

The summary groups each program's draws by stack:

```
Submitters (run with --gpu_trace_submitters=true)
  A1B2...:C3D4...: 812 draws from 3 site(s)
      782 (96%)  0x8241A0C4 <- 0x82419B38 <- 0x823F7714
```

Read a stack as return addresses, innermost first, so each one is a few
instructions past a call, inside the function that made it. Find the function
containing it in the game's `config/functions.toml` or its `.map` file: the
innermost frames are the graphics library writing the packet, and the first
address that belongs to the game's own code is the submitter to hook.

A site that covers nearly all of a program's draws is the answer. Several sites
with similar shares usually means the program is drawn from more than one place,
which a renderer has to handle, not a fault in the trace.

This samples rather than records. A page faults only on its first write after
the watch is armed, so a stack is the code that first wrote into the page a
draw's packet sits in, and one draw's stack can belong to the draw before it.
Most draws get no sample at all - around one in ten carried one in a Skate
capture - so trace enough frames that the site you are after appears many times,
and read the shares rather than any one line. What the sampling cannot do is
prove a rare site is absent.

### Embedding native-renderer shaders

List a game's shaders once after `recomp_add_game`. The stage is inferred from
the required `.vs.hlsl`, `.ps.hlsl`, or `.cs.hlsl` suffix, and the entry point
defaults to `main`:

```cmake
recomp_add_shaders(skate_3
    SOURCES
        shaders/scene.vs.hlsl
        shaders/scene.ps.hlsl
    INCLUDE_DIRS shaders/include
)
```

The normal build needs no shader compiler. It recursively expands quoted HLSL
includes and generates `<target>_shaders.h` in the target's include path. Each
shader is an `EmbeddedShader` containing `hlsl` as a `std::string_view`, `spirv`
as a `std::span<const uint32_t>`, `dxil` as a `std::span<const uint8_t>`, and
`entry_point`. The identifier is the source path converted to a C++ identifier.
For example:

```cpp
#include "skate_3_shaders.h"

using namespace recomp::shaders::skate_3;
const auto& shader = shaders_scene_vs_hlsl;
```

Both backends' bytecode lives beside the source: `scene.vs.hlsl` uses the
committed `scene.vs.spv` and `scene.vs.dxil`. DXC builds both, at the same
`_6_0` profile, so the two backends run the same shader rather than the same
text through two compilers. After changing HLSL, regenerate them explicitly and
commit both:

```powershell
cmake --build out/build/win-amd64-release --target skate_3_shaders
```

That maintenance target looks for `dxc` on `PATH`. Set `RECOMP_DXC_EXECUTABLE`
at configure time when it is elsewhere. Editing an HLSL file or any `.hlsli`
below an include directory regenerates the embedded header on the next ordinary
build.

One compiler will not do: **the `dxc.exe` that ships in the Windows SDK is built
without SPIR-V**, and it is the one found on `PATH` on a machine with no Vulkan
SDK. The DXIL half works with it and the SPIR-V half cannot. Install the Vulkan
SDK, or a DirectX Shader Compiler release from Microsoft's GitHub, and point
`RECOMP_DXC_EXECUTABLE` at that one. The build says so rather than failing per
shader.

The normal build needs neither compiler, only the committed bytecode.

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

## Issues

Work that is not being done right now lives in an issue, so a person or an agent
can pick it up without the context that produced it. The three repositories use
the same labels:

| Label | Means |
| --- | --- |
| `area: codegen` / `renderer` / `tooling` / `kernel` / `docs` | Which part of the stack |
| `type: bug` / `feature` / `research` | Whether something is wrong, missing, or unknown |
| `effort: hours` / `days` / `weeks` | Rough size, from having done comparable work |
| `ready` | Self-contained: the issue carries the addresses, commands and file paths needed to start |
| `blocked` | Waiting on other issues, which the issue names |
| `needs a game` | Cannot be finished without the disc and a play test |
| `epic` | A tracking issue holding the order of work for a group |

An issue worth picking up says what happened (with the log line or address), what
to do, and how its author will know it is done. It ends with a "How this fits"
block naming its epic, what it blocks and what blocks it - an agent landing on one
issue can then see the order without reading the others.

Cross-repository work is normal here: the SDK, the framework and a game repository
each hold part of it. Link across with full URLs, and keep the order of work in one
epic rather than in each issue.

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
