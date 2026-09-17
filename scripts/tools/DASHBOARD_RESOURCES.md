# Inspecting dashboard scenes

The guide can load artwork from `shrdres.xzp`, an individual XUI package, or a
folder of packages/loose files using `--recomp_guide_resources=<path>`. Packages
inside folders are recognized by `XUIZ` magic, including extensionless resources
written by `rexglue resources`. Files are visited in sorted path order; duplicate
basenames currently use the last file. This is an artwork loader, not an XUI
scene renderer or a locale/URI resolver.

The Metro palette is visually matched to the supplied guide reference (gray
panels, slate tabs, dark green selection), not claimed to be extracted HUD
colors. The guide and settings use Windows Segoe UI when available, replacing
the scaled debug font. Supply `--recomp_guide_font=<TTF or OTF>` before startup
to use another font; Segoe UI is a substitute, not a verified Xbox font match.

Guide tabs are ordered Games, player, Settings. LB/L1 and RB/R1 move one tab
per press without wrapping; keyboard Left/Right do the same. Games contains
Achievements; Settings links to Scaling & Display, Controls and Game Files.
Achievements retain Up/Down and PageUp/PageDown scrolling; bumpers navigate
tabs. Exit confirmation keeps input modal. Tab-navigation boundary assertions
can be checked with:

```powershell
clang++ -std=c++17 -fsyntax-only -I framework/common/include framework/common/tests/guide_navigation_test.cpp
```

## Reproduce the inspection

Use a dashboard you supply. Keep extracted data in an ignored directory.

```powershell
rexglue resources 'path/to/dash.xex' -o 'work/packages'
python framework/scripts/tools/dashboard_resources.py 'work/packages' 'work/inspection' --xuihelper 'path/to/XUIHelper.CLI.exe'
```

The output directory must be empty. The command unpacks every XUI package and
converts `MPDashSkin.xur` and `MiniGamercard.xur` by default. Use repeated
`--scene` patterns to select other scenes, or `--scene '*'` to attempt all scenes.
Without `--xuihelper`, extraction and inventory still work. Conversion errors
appear in the report and cause a nonzero exit status. Package virtual `..`
components are mapped to `__parent__` directories; the inventory retains both
the original name and extracted path.

Outputs:

- `files/<package>/`: resources, separated by package to retain provenance.
- `xml/<package>/`: converted XUI scenes, including timelines and figure data.
- `report.json`: package inventories, PNG sizes, SHA-256 hashes, and explicit
  scene properties with element ancestry.
- `report.md`: conversion status, text sizes, font properties and image references.

[XUIHelper](https://github.com/SGCSam/XUIHelper) is a separately built GPL-3.0
command-line converter; it is not bundled, linked into the runtime, or copied
into this framework. Its documented XUR-to-XUI conversion is used as an offline
inspection step. Build its CLI with `dotnet build XUIHelper.CLI -c Release`.

## Metro 17559 observations

Inspected the workspace's `Metro/V2/Retail/17559/dash.xex`:

- 36 embedded resources: **35 XUI packages and one title database**.
  The packages contain 5,035 files, including 363 XUR scenes.
- `SharedUI/MPDashSkin.xur`: 518 reported elements, canvas 400 by 400.
  Explicit point sizes: 12, 15, 16, 18, 20, 21 and 22. No explicit font face.
- Its `Label_TabTitle_Centered` visual and text presenter are 420 by 47.
  The presenter specifies PointSize 20, TextColor `0xffebebeb`,
  DropShadowColor `0x7f0f0f0f`, TextStyle 1024, LineSpacingAdjust -2.
  This is a dashboard label style, **not a measured guide side tab**.
- `dashcomm/MiniGamercard.xur`: 43 reported elements. The canvas is 1120 by
  770, while the scene is 323 by 39 at local position (0, 2, 0). Eight image
  presenters are 32 by 32. It references mail, friend and community-star art;
  it does not establish the guide header's gamer tile dimensions.
- Neither scene explicitly selects a font face. Inheritance/default font
  resolution remains necessary. PointSize must not simply be assumed to mean
  ImGui pixels.
- `ico_96x_gamerpic.png` (96 by 96) and `button_White.png` (26 by 26) are
  in `dashcomm`, not `SharedUI`. Image dimensions alone do not establish their
  intended display size or role in the guide.
- `MPDashSkin` references `common://updefault.png`, `common://downdefault.png`,
  `sharedres://loadingRing.png`, `ico_32x_FullScreen.png`, and an empty `file://`
  placeholder. Its figures/timelines are retained in XML, not rasterized.
- No `GuideMain.xur` was found among these package entries. The workspace
  dashboard archive contains no `hud.xex`. The guide's actual HUD scenes are
  still needed before replacing the approximate guide layout with measured
  guide metrics. `controlp/GuideItemScene.xur` is not sufficient evidence of
  the in-game guide: the same package contains television guide/recording art.

These findings correct the earlier assumption that the dashboard's shared skin
and mini gamercard alone establish the compatibility guide's layout. No guide
dimensions or font substitutions were made from unrelated dashboard controls.

## On-screen keyboard (17559 system update)

The console's keyboard is `vk.xex` in flash; a system update carries only a
patch for its package (`L.vk.xex.vk.xzp` starts `BDES`). Two full executables
in the update embed the same keyboard media, which the framework's keyboard
uses when they are in the guide's resources folder:

```powershell
rexglue resources '$SystemUpdate/Dash.Search.xex' -o 'work/dash.search'
# copy work/dash.search/vkmedia and work/dash.search/dashsear to resources/guide
```

- `vkmedia` (also in `Title.Zune.xex`): `KeyboardMain.xur`, the HUD scene
  (`XuiHUDKeyboardScene`, class `CKeyboardScene`, 852 by 480, legend A Select,
  B Back, X Backspace, Y Space), and `KeyboardBase.xur`, the keys: five rows of
  ten 32 by 25 keys on a 34 by 27 pitch, Backspace and Space under them, and
  92 by 52 side keys for LB/RB (cursor), LT/RT, Caps and Done, with the button
  pictures `LB.png`, `RB.png`, `LT.png`, `RT.png`, `Caps.png` (left stick
  click), `Done.png` (START), `btn_x.png` and `btn_y.png`. The characters and
  the LT/RT page names are set by vk.xex's code and are not in the scene.
- `dashsear`: `vk/vk_Focus.xma` and `vk/vk_Select.xma`, and `skin_search.xur`
  with the key visuals. `btn_KbrdChar` plays vk_Focus on focus and vk_Select on
  press, turns a green highlight (#008A00) on at once, and brightens it to
  #1CB61C over frames 16 to 21 while pressed. `evk_EditCaret` blinks a 3-wide
  green caret every 30 frames.
- The only timeline in `KeyboardBase` is the Japanese kana flick menu.

## Guide skin, fonts and message boxes (17559 system update)

More of the framework's system screens draw from full executables in the same
update. Extract each with `rexglue resources` and copy the named packages to
`resources/guide`:

| Executable | Package | Used for |
| --- | --- | --- |
| `dash.ClosedCaptionDll.xex` | `ccfonts` | `segoer.ttf`, "Segoe Xbox Regular": the guide, keyboard and message box type, ahead of Windows Segoe UI. `XenonSCLatin.xtt`, the console's UI font, is encrypted |
| `Guide.AccountRecovery.xex` | `shdmedia` | `btn_focusG.xma`, `btn_selectG.xma`, `btn_backG.xma`: the guide apps' own focus, select and back sounds |
| `dash.xex` | `memory` | `ico_64x_warning.png` for error, warning and alert message boxes |
| (any dashboard) | `shrdres.xzp` | `A-`, `B-`, `X-`, `Y-Button_32.png`: the legend buttons |

`shdmedia`'s `skin.xur` is the guide apps' skin, and supplies the numbers the
screens use even without its files:

- `legend_A` to `legend_Y`: the `<letter>-Button_32.png` disc (flat, about 22 of
  its 32 pixels) with the letter drawn over it in #F5F5F5 with a #2E000000
  shadow, beside an 18 point #EBEBEB label. Without the pictures the discs are
  drawn in their sampled colours: A #6CB733, B #B12B36, X #3064A1, Y #E0B41C.
- `TransOpen` fades a scene in over 10 frames and `TransFrom` out over 15, both
  linear, at 60 frames a second.
- `Label_Head` is 12 point #EBFFFFFF, `Label_Body` 16 point #0F1214, and
  `TwoThirdsPane` fills with #EBEBEB.

XAM's own message box scene is in `xam.xex`, which the update only patches, so
the message box follows this skin rather than a scene of its own.

## Validation

```powershell
python -m unittest discover -s framework/scripts/tools -p 'test_dashboard_resources.py' -v
```

Tests use synthetic packages and XML, not dashboard assets. The real inspection
also checks both selected scenes through the external converter. A successful
conversion is not a visual fidelity or runtime rendering test.
