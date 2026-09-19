# recomp-framework

Shared framework for native PC static recompilations of Xbox 360 games built on
[ReXGlue](https://github.com/rexglue/rexglue-sdk). It is what turns a game's
recompiled code into an app a player can run from their own disc image: first-run
ISO install, DLC install, controller-driven system and settings menus, portable
mode and Xbox PC app packaging.

Used by:

- [fightNightRecomped](https://github.com/furqanagwan/fightNightRecomped): Fight Night Round 4, Fight Night Champion
- [liveRecomped](https://github.com/furqanagwan/liveRecomped): NBA LIVE 09, NBA LIVE 10
- [skateRecomped](https://github.com/furqanagwan/skateRecomped): Skate, Skate 2
- [topSpinRecomped](https://github.com/furqanagwan/topSpinRecomped): Top Spin 3, Top Spin 4 (in progress)

The framework contains no game data and no generated code.

## Layout

```
cmake/Recomp.cmake          recomp_add_game(): shared build setup for every game
common/                     recomp_common library (namespace recomp)
  include/recomp/app        GameRecompApp base class, GameDescriptor, GamePaths
  include/recomp/installer  DiscImageInstaller (ISO), ContentPackageInstaller (DLC)
  include/recomp/input      ControllerMenuWatcher, GuestInputGate, ImGuiGamepadBridge
  include/recomp/platform   NativeFilePicker, GamingRuntimeSession (Xbox PC app)
  include/recomp/settings   UserSettingsStore
  include/recomp/ui         DiscInstallDialog, SystemMenuDialog, SettingsDialog, MonochromeTheme
  include/recomp/debug      GuestImageDump
  src/kernel                Kernel stubs shared by every game (Xbox Live Vision camera)
scripts/                    build, new game, artwork, packaging, codegen analysis
templates/game/             Starting point for a new game folder
thirdparty/rexglue-sdk      ReXGlue fork (branch main) with the fixes these games need
```

## Using it in a game repository

A game repository adds this repository as a submodule named `framework`, next to
one folder per game:

```
<game repository>/
  framework/                this repository
  <GameFolder>/             CMakeLists.txt includes ../framework/cmake/Recomp.cmake
```

```
git submodule add https://github.com/furqanagwan/recomp-framework.git framework
git submodule update --init --recursive
```

Scripts are run from the game repository root and find it automatically
(override with `RECOMP_REPOSITORY_ROOT`):

```
.\framework\scripts\build.ps1 -Game <GameFolder>
./framework/scripts/build.sh <GameFolder>
```

Adding a game, the codegen workflow and artwork are described in
[CONTRIBUTING.md](CONTRIBUTING.md).

Native-renderer shader embedding and fault-safe guest-memory reads are described
in [docs/native_rendering.md](docs/native_rendering.md).

Disc builds run without title updates. Builds that target a particular update
pin and verify that exact package; see [TITLE_UPDATES.md](TITLE_UPDATES.md).

## Runtime behaviour every game gets

| Feature | Details |
| --- | --- |
| First run | If `default.xex` is missing, a setup window asks for the player's Xbox 360 ISO and extracts it once. Unattended: `RECOMP_INSTALL_ISO=<iso>` |
| DLC | Packages in the `dlc` folder next to the executable are installed on start. Unattended: `RECOMP_INSTALL_DLC=<package or folder>` |
| System menu | **View + Menu** or **Esc**: Resume, Settings, Exit Game |
| Controllers | Xbox, PlayStation, Switch and Steam Deck through SDL; all drive player 1 unless `recomp_shared_controllers = false` |
| Portable mode | An empty `portable.txt` next to the executable keeps saves, cache and settings beside it |

## ReXGlue fork

`thirdparty/rexglue-sdk` tracks
[furqanagwan/rexglue-sdk@main](https://github.com/furqanagwan/rexglue-sdk), which
is upstream ReXGlue plus:

- `rexglue extract <iso> <dir>`: extracts an Xbox 360 disc image, so projects
  don't need extract-xiso
- codegen: `vpkuwus`/`vpkuhus` read aliased sources before writing (VP6 video colour)
- input: `InputSystem` entry points are serialized (concurrent polling crash)
- gpu/d3d12: issued draws feed the debug overlay counter
- kernel: 64-bit export arguments (XUIDs, file times) are no longer truncated,
  which broke NBA LIVE 10 profile saves
- system: repeated export lookups reuse their thunk (upstream #420)
- filesystem: relative guest paths resolve against `game:` (upstream #405)
- upstream PRs #422, #423, #424 (Windows clone and install build fixes),
  #384 (config loaded before path settings) and #382 (host FP exceptions stay masked)

## License

BSD 3-Clause, see [LICENSE](LICENSE). ReXGlue is BSD 3-Clause and derived from
Xenia.
