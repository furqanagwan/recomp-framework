# Project Helix readiness

## Status

This repository currently ships a **Windows GDK PC** build. It is structured as a
foundation for Microsoft's Project Helix console, but it is not a native Helix
console build and has not been validated on console hardware.

Microsoft's public GDK is enough to build, package and test the PC target. Native
Xbox console compilation, deployment, profiling and certification require the
secure Xbox GDK, Partner Center identity, an approved Xbox developer account and
console hardware. Those materials are not stored or guessed in this repository.

## Deployment profiles

| `RECOMP_DEPLOYMENT_TARGET` | State | Output |
| --- | --- | --- |
| `windows_pc` | Supported and tested | Win32 AMD64 executable; loose deployment or PC MSIXVC/MSIXVC2 |
| `xbox_console` | Reserved for secure integration | Stops at configure time unless the secure console toolchain explicitly enables it |

Game presets set `windows_pc` explicitly. A private console integration may set
`RECOMP_SECURE_XBOX_TOOLCHAIN_READY` only after it supplies the real Xbox toolchain,
console libraries and deployment settings. Setting that switch on a desktop compiler
does not create a console build.

## Public repository work completed

- Windows AMD64, Clang and Direct3D 12 are the supported SDK build surface.
- The installed `GameDKCoreLatest` location is discovered instead of pinning a GDK edition.
- PC and console deployment intent are distinct CMake values.
- Desktop file picking, WinHTTP downloading and `CreateProcessW` restart code are selected
  only for `windows_pc`; the reserved console profile uses explicit unavailable stubs.
- PC package manifests state `TargetDeviceFamily="PC"`.
- A console `MicrosoftGame.config` input template keeps Partner Center identity and the
  future console `TargetDeviceFamily` as required placeholders.
- All five common test programs are built and registered with CTest.

## Validated public baseline (2026-09-19)

- `rexglue-sdk` commit `5df1952`: 1,686 tests passed and 2 skipped.
- `recomp-framework` commit `7414511`: all 6 CTest cases and all 22 Python tests passed.
- The reserved console profile stopped at configuration with the expected secure-toolchain message and produced no mislabeled binary.
- `skateRecomped` commit `fca1a0d`: Skate 3 configured as `windows_pc`, compiled all 1,101 build steps and linked `Skate 3.exe` against framework `7414511` and SDK `5df1952`.
- Secure console integration and hardware validation are tracked in [issue #13](https://github.com/furqanagwan/recomp-framework/issues/13).
## Console blockers and ownership

| Area | PC implementation today | Console work after secure access |
| --- | --- | --- |
| Build ABI | Win32 AMD64 | Use the real console toolchain, `WINAPI_FAMILY_GAMES`, Xbox D3D12 extensions and console libraries |
| Package | `makepkg /pc` or MSIXVC2 | Produce XVC with the Partner Center identity and Microsoft-provided target family |
| Game files | ISO picker or pre-extracted local files | Define a permitted console content-delivery/install model; arbitrary desktop file browsing is unavailable |
| Title updates | Local picker or WinHTTP download | Define approved package delivery and storage; do not assume unrestricted WinHTTP or writable folders |
| Restart | `CreateProcessW` | Use the console lifecycle/relaunch mechanism |
| Saves/settings | Executable or user-data folders | Map to console title storage and suspend-safe flushing |
| Users | Runtime's current profile model | Handle sign-in, sign-out, user switching and controller-to-user association |
| Lifecycle | Desktop process lifetime | Handle suspend, resume, constrained mode, termination and network changes |
| Guide input | View+Menu/Esc and optional Guide button | Keep the Xbox 360 guide, but never consume a system-reserved Xbox button |
| Validation | Windows test PC | Helix development kit, PIX for Xbox, Submission Validator and Xbox Requirements |

The Xbox 360-styled guide and settings UI are shared product UI and remain part of
both profiles. Platform services beneath that UI are what change.

## Handoff checklist

When secure Project Helix material becomes available:

1. Record the GDK edition and toolchain file without committing confidential paths or files.
2. Replace the `xbox_console` configure gate with verified toolchain detection.
3. Build with the console API partition and resolve every unavailable desktop import.
4. Populate a private copy of `MicrosoftGame.console.config.in` from Partner Center.
5. Add XVC pack/deploy commands from the secure documentation; never reuse `/pc`.
6. Implement lifecycle, title storage, users and approved content delivery.
7. Run on hardware, capture PIX CPU/GPU traces, and test suspend/resume and sign-out.
8. Run Submission Validator and track each certification failure as its own issue.

## Public validation commands

```powershell
cmake -S common -B out/audit-gdk -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang `
  -DCMAKE_CXX_COMPILER=clang++ -DRECOMP_BUILD_TESTS=ON `
  -DREXSDK_DIR=<path-to-rexglue-sdk>
cmake --build out/audit-gdk
ctest --test-dir out/audit-gdk --output-on-failure
python -m pytest scripts/tests -q
cmake -S common -B out/console-gate -DRECOMP_DEPLOYMENT_TARGET=xbox_console
# Expected: a clear secure-toolchain error; no mislabeled desktop binary.
```

## Work still tracked publicly

- `recomp-framework#13`: integrate the secure Xbox GDK and validate on Helix hardware.
- `rexglue-sdk#16`: remove dormant non-GDK and Vulkan source.
- `recomp-framework#7`: repair Skate 3 screenshot capture.
- `recomp-framework#8` through `#10`: resolution and asset work.
- `skateRecomped#4` and `#6`: identify and render Skate 3 world geometry.
