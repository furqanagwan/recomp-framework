# Architecture: ReXGlue SDK and the Recomp Framework

Static recompilation of Xbox 360 (PowerPC / Xenon) titles into native Windows
binaries, targeting the Microsoft GDK, the Xbox PC app, and Project Helix.

This document describes **how the code is today**. Where something is planned
rather than built, it says so under [Direction](#7-direction-not-yet-true).
Treat anything not marked that way as a fact you can rely on; if you find it is
not, the document is wrong and should be corrected in the same change.

---

## 1. The three repositories

```text
┌────────────────────────────────────────────────────────────────────────┐
│  Game repository        skateRecomped, 007Recomped, midnightClubRecomp │
│  Per-title descriptor, codegen manifest, settings, GDK metadata        │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │  find_package(rexglue) + submodule
┌──────────────────────────────────▼─────────────────────────────────────┐
│  recomp-framework       Game-agnostic application layer                │
│  First-run disc install, DLC, guide and settings UI, GDK packaging     │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │
┌──────────────────────────────────▼─────────────────────────────────────┐
│  rexglue-sdk            Runtime, codegen, guest hardware               │
│  Guest memory, kernel/XAM, Xenos GPU → D3D12, XMA audio, input, UI     │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │
┌──────────────────────────────────▼─────────────────────────────────────┐
│  Microsoft GDK (April 2026, 260404) and Windows                        │
│  D3D12 + Agility SDK, XGameRuntime, GameInput                          │
└────────────────────────────────────────────────────────────────────────┘
```

Dependencies run one way only: game → framework → SDK → GDK.

### How a game consumes these — read this before building anything

A game builds against the **installed** SDK (`C:\ReXGlue`) through
`find_package(rexglue)`, **not** against `framework/thirdparty/rexglue-sdk`.
A game also compiles its own copy of the framework, which is a **git submodule**
at `<game>/framework`, not the standalone `recomp-framework` checkout.

So:

* Changing SDK source has no effect on a game until you `cmake --install` it.
* Changing the standalone framework checkout has no effect on a game until the
  change reaches that game's `framework` submodule.

Both of these produce a build that silently does not contain your change. They
are the most common way to waste an hour here.

---

## 2. What each layer owns

### rexglue-sdk

* **Codegen** (`rexglue codegen`) — translates XEX PowerPC to C++ ahead of
  time. There is no JIT anywhere in this project.
* **Guest memory** (`src/system/xmemory.cpp`) — maps the 32-bit guest space
  into host address space. See §4.
* **Kernel / XAM** (`src/kernel`) — Xbox 360 kernel and XAM calls.
* **Graphics** (`src/graphics`) — Xenos command buffers, registers and shaders
  translated to Direct3D 12. Xenos is the ATI/AMD part in the 360; it is not
  Adreno, which is the later Qualcomm line.
* **Audio** (`src/audio`) — XMA decode to PCM.
* **Input** (`src/input`) — driver interface with SDL, XInput, GameInput, MnK
  and NOP implementations.
* **UI** (`src/ui`) — window, presenter, ImGui drawer, D3D12 presenter.

### recomp-framework

* First-run disc install, title updates, DLC install.
* The Xbox Guide replica, settings, virtual keyboard, message boxes.
* GDK packaging (`scripts/package_gdk.ps1`) and the game application shell.
* `XGameRuntimeInitialize` only — see [Direction](#7-direction-not-yet-true) for
  the rest of XGameRuntime.

### Game repositories

Descriptor (title id, media id, title update, DLC catalogue), codegen manifest,
settings, GDK metadata and artwork. **Never game data.** ISOs, `default.xex`,
`.xexp`, extracted assets, generated code and extracted metadata stay out of
git, and Microsoft dashboard or system-update files are never redistributed.

---

## 3. Toolchain — this part is not negotiable

| | Actual |
|---|---|
| Compiler | **LLVM Clang 18+**. `CMakeLists.txt` raises a `FATAL_ERROR` on anything else, MSVC included. |
| Language | C++23 (`CMAKE_CXX_STANDARD 23`) |
| CMake | `3.25...4.4`; CMake 4.4.3 in use |
| Generator | Ninja Multi-Config, via `CMakePresets.json` |
| Dependencies | git submodules under `thirdparty/`, plus `FetchContent` for the Agility SDK and FidelityFX. **No vcpkg.** |
| Architecture | x64. 64-bit is enforced; ARM64 is not built today. |
| Graphics | Direct3D 12 only. Feature level **12_2** (DirectX 12 Ultimate) is required. |
| GDK | April 2026, edition `260404`. One variable per repo: `REXGLUE_GDK_EDITION` / `RECOMP_GDK_EDITION`. |

Warning flags are Clang's (`-Wall -Wextra`), not MSVC's `/W4 /permissive-`.
`cmake_policy(SET CMP0155 OLD)` is set before `project()` because C++ module
scanning breaks `clang-scan-deps` here; nothing uses modules.

---

## 4. Guest memory

The guest address space is mapped into one host arena. Addresses are translated
as `membase + heap_base + host_address_offset + relative`, per heap, rather than
through a single fixed base.

Heaps are typed (`HeapType` in `include/rex/system/xmemory.h`):

| Heap | Holds |
|---|---|
| `kGuestVirtual` | general guest virtual allocations |
| `kGuestXex` | the loaded executable image |
| `kGuestPhysical` | physical allocations the GPU and audio read |
| `kHostPhysical` | host-side physical backing |

Two details that have cost real debugging time:

* **Physical addresses are masked with `0x1FFFFFFF`.** A GPU callback reports a
  masked physical address; a pointer taken from guest memory is unmasked.
  Comparing them without masking finds nothing and looks like a logic bug.
* On Win32, guest addresses at or above `0xE0000000` carry a `0x1000` host
  offset (`PhysicalHostOffset`).

`include/rex/system/xmemory.h` is the source of truth. Do not reproduce a memory
map here that drifts from it.

Guest data is big-endian. Reads and writes go through the endian-aware
wrappers; do not reinterpret guest memory as host structs.

---

## 5. Error handling

`rex::Result` (`include/rex/result.h`) is `std::expected` with a `rex::Error`
carrying an `ErrorCategory`, used for codegen, I/O and format paths.

Guest-facing and driver APIs return `X_STATUS` / `X_RESULT` and are checked with
`XSUCCEEDED` / `XFAILED`. This is deliberate: those values cross the boundary to
recompiled guest code, which expects the console's status codes.

`X_STATUS_SUCCESS` is a macro that expands to `(X_STATUS)0`, and `X_STATUS` is
in namespace `rex`. Inside another namespace the macro will not compile; use
`XSUCCEEDED(...)` instead.

Do not introduce a third error convention.

---

## 6. Build and verify

```powershell
# SDK
cmake --preset win-amd64
cmake --build out/build/win-amd64 --config Release
cmake --install out/build/win-amd64 --config Release --prefix C:/ReXGlue

# A game (sets up the developer environment itself)
framework\scripts\build.ps1  -Game skate3
framework\scripts\run_game.ps1 -Game skate3 -Seconds 30
framework\scripts\package_gdk.ps1 -Game skate3 -Register
```

**Check the exit code of a build before installing.** `cmake --build` returning
non-zero and then `cmake --install` running anyway will copy a half-linked
`rexglue.exe` into `C:\ReXGlue`, and every game's codegen then fails with
"not a valid application for this OS platform". This has happened twice.

`run_game.ps1` writes a report to `<game>/out/runs/<timestamp>/` with
`summary.md`, `game.log` and frame statistics. It is the fastest way to tell
whether a change worked. Its screenshots do not work — the swapchain captures
black (framework issue #7) — so do not rely on them to judge UI changes.

---

## 7. Direction — not yet true

Listed so nobody writes code that assumes these already hold.

* **GameInput as the default.** The driver exists and works, but a pad that
  enumerates as two devices can put the silent one in player one's slot, so SDL
  is still the default and GameInput is opt-in.
* **Dropping SDL.** SDL still provides the window, and there is no Win32 window
  in the tree — `window_sdl.cpp` is the only one. Audio and input can move
  first; the window is the gate.
* **XAudio2.** The code exists in `src/audio/xaudio2/` and is **not in the
  build**; it was only ever referenced from the UWP branch that was removed.
* **DirectSR** for upscaling. Nothing yet. FidelityFX is present but `OFF` by
  default and experimental.
* **`WINAPI_FAMILY_GAMES`.** Not defined anywhere today.
* **XGameRuntime beyond initialization.** No `XUser`, `XStorage` or
  `XTaskQueue` usage exists.
* **ARM64 / Project Helix.** Helix dev hardware is 2027. ARM64 is not built.
* **MSIXVC2** is what releases ship, distributed from GitHub Releases rather
  than the Store. Installing one needs `packageutil2 set msixvc2 on` once per
  machine, elevated.
