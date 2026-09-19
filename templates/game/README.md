<p align="center">
  <img src="docs/icon.png" alt="@DISPLAY_NAME@" width="320">
</p>

<h1 align="center">@DISPLAY_NAME@</h1>

<p align="center">
  Native PC static recompilation of the Xbox 360 version, built on the
  <a href="../README.md">recomp</a> framework and ReXGlue.
</p>

<!-- Fill in every TODO before the first push; rexglue init achievements and the
     codegen log give most of the disc facts. -->

## Game

| | |
| --- | --- |
| Developer | TODO |
| Publisher | @PUBLISHER@ |
| Series | TODO |
| Platform recompiled | Xbox 360 |
| Released | @RELEASE_YEAR@ |
| Genre | TODO |
| Achievements | TODO (count from `metadata/achievements.toml`), 1000 Gamerscore |

## Regions

| Region | Serial | Status |
| --- | --- | --- |
| TODO flag and region | `TODO` | ⬜ Not tested |

Only the tested disc's `default.xex` has been recompiled. Other regional
executables are likely to differ and may need their own codegen pass.
Region list from [Redump](http://redump.org/discs/system/xbox360/).

## Disc

| | |
| --- | --- |
| Region | TODO |
| Title ID | `TODO` |
| Media ID | `TODO` |
| Executable version | TODO |
| Title update | None (disc executable) |
| Languages | TODO |
| Contents | TODO files, TODO bytes |
| Executable | `default.xex`, TODO bytes |
| DLL modules | TODO |

## Status

| Area | State |
| --- | --- |
| Boot | Not yet tested |
| Controller input, gameplay | Not yet tested |
| DLC | Installer in place; no packages tested |
| Windows GDK PC | Configured; the Helix console profile requires secure SDK access and hardware validation |

## Play

1. Download `TODO-v<version>-windows-x64.zip` from the repository's Releases page
   and extract it to a folder you can write to.
2. Run `@EXECUTABLE_NAME@.exe` and choose your Xbox 360 ISO (see
   [Regions](#regions)); the files are copied once.
3. Open the system menu with **View + Menu** (or **Esc**) for Settings and Exit.

## System requirements

Keep this table in step with `release.json`.

| | Required |
| --- | --- |
| OS | Windows 10 version 2004 (build 19041) or Windows 11, 64-bit |
| Processor | 64-bit x86 CPU with SSE4.1 |
| Graphics | DirectX 12 GPU (feature level 11_0) |
| Memory | 8 GB RAM recommended |
| Storage | TODO GB, plus room for the ISO while it is copied |
| Software | [Microsoft Visual C++ Redistributable 2015-2022 (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe) |
| Game | Your own @DISPLAY_NAME@ Xbox 360 disc image |
| Title update | Not required by this disc build |

## Build from source

```
rexglue extract "<your disc>.iso" @FOLDER@\assets
.\framework\scripts\discover_functions.ps1 -Game @FOLDER@
.\framework\scripts\run_game.ps1 -Game @FOLDER@
```

Setup is described in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Default settings

`settings/@PROJECT_NAME@.toml` starts from the framework defaults (`rov` / `fsi`
render target paths, no background pipeline creation, 60 Hz vsync).

## Recompilation notes

| | |
| --- | --- |
| Function seeds | TODO in `config/functions.toml` |
| Disabled seeds | TODO in `config/disabled_function_seeds.txt` |
| Kernel stubs | None needed beyond the framework's |

The full log of what was found and fixed is in [docs/NOTES.md](docs/NOTES.md).

## Artwork

`docs/icon.png` is the title image from `default.xex`, upscaled to 1024x1024.

1. `rexglue init --project-name @PROJECT_NAME@ --xex-path assets\default.xex achievements assets\default.xex metadata`
2. Upscale `metadata/icons/title.png` 4x twice with Real-ESRGAN
   (`realesrgan-x4plus`) to `metadata/gdk_hd/title_1024.png`.
3. `.\framework\scripts\generate_artwork.ps1 -Game @FOLDER@ -ProjectName @PROJECT_NAME@`
4. Copy `metadata/gdk_hd/title_1024.png` to `docs/icon.png`.

## Legal

Not affiliated with or endorsed by @PUBLISHER@ or Microsoft. You must own the game.
