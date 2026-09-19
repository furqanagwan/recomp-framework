# Instructions for coding agents

These apply to `rexglue-sdk`, `recomp-framework` and every game repository.
Read `ARCHITECTURE.md` first; it describes what the code actually is, and
separates that from what is only planned.

---

## 1. Things that will waste your time if you skip them

**The toolchain is Clang, not MSVC.** `CMakeLists.txt` fails with a
`FATAL_ERROR` on any other compiler. Do not write MSVC flags (`/W4`,
`/permissive-`, `/std:c++latest`) or suggest an MSVC build.

**There is no vcpkg.** Dependencies are git submodules under `thirdparty/`,
plus `FetchContent` for the Agility SDK and FidelityFX.

**A game does not build the SDK or framework you just edited.** It builds the
*installed* SDK at `C:\ReXGlue` and its own `framework` **submodule**. After
changing the SDK, `cmake --install` it. After changing the framework, get the
change into that game's submodule. Otherwise you will test a binary that does
not contain your work and conclude the change did nothing.

**Check the exit code of every build before installing.** Filtering build
output for "error" and concluding success has twice led to a half-linked
`rexglue.exe` being installed, breaking codegen for every game.

```powershell
$out = cmake --build out/build/win-amd64 --config Release 2>&1
if ($LASTEXITCODE -ne 0) { $out | Select-String "error|FAILED"; throw "build failed" }
```

**Never commit game data.** ISOs, `default.xex`, `.xexp`, extracted assets,
generated code, extracted metadata, title-update or DLC packages. Microsoft
dashboard and system-update files are player-supplied and are never bundled in
a release. Extract to a scratch directory, not the repo.

---

## 2. Before you claim something works

State what you verified and what you did not. "It builds" is not "it works",
and "I changed the code that draws it" is not "it draws".

* **Build**: exit code zero, on the layer you actually changed *and* on a game.
* **Run**: `framework\scripts\run_game.ps1 -Game <game> -Seconds 30`, then read
  `out/runs/<timestamp>/summary.md`. It reports the outcome, frame rate and
  errors.
* **UI changes cannot be screenshotted.** Capture returns black on this
  swapchain (framework #7). Use logging, or ask the user to look.
* **Controller behaviour cannot be verified here.** Synthetic input is
  unreliable — injected arrow keys have arrived as numpad keys — and no agent
  can press a physical button. Ask the user for a log rather than guessing.

If you cannot verify something, say so plainly. Reporting a fix you could not
test as though you tested it is worse than reporting the limitation.

---

## 3. Making changes

**Match the surrounding code.** Naming, comment density and idiom are
consistent in these repos; a change that reads differently from its neighbours
is a change that will be rewritten.

**Comments say why, not what.** The existing comments explain the console
behaviour being reproduced, or the non-obvious reason a line exists. Do not add
comments that restate the code.

**Scripted edits need reviewing.** Regex or Python edits across many files will
produce orphaned indentation, duplicated branches and unbalanced braces. Read
`git diff` afterwards. A previous scripted removal left non-Windows input
sources inside an `if(WIN32)` branch and silently dropped XAudio2 from the
build; both compiled cleanly.

**Prefer the smallest change that is correct.** Large invasive refactors in
shared files have caused regressions here; when one is genuinely needed, land
it separately from behaviour changes so a bisect can tell them apart.

---

## 4. Code standards

* **C++23.** `std::span`, `std::string_view`, concepts, `constexpr` where it
  earns its place.
* **Error handling.** `rex::Result` (`std::expected` + `rex::Error`) for
  internal paths; `X_STATUS` / `X_RESULT` with `XSUCCEEDED` / `XFAILED` for
  anything crossing to guest code. Do not add a third convention, and do not
  convert existing guest-facing APIs to `std::expected` — the console's status
  codes are what the recompiled code expects.
  `X_STATUS_SUCCESS` will not compile outside namespace `rex`; use
  `XSUCCEEDED`.
* **Endianness.** Guest memory is big-endian. Go through the existing wrappers;
  never reinterpret guest memory as a host struct.
* **Hot paths.** The frame loop, the command processor and audio callbacks
  should not allocate. Use existing pools and arenas.
* **RAII for host resources**, with the existing wrappers. Note that ImGui
  dialogs in the framework are deliberately self-owning and constructed with
  bare `new`; that is the established pattern there, not an oversight to fix.
* **Threading.** Prefer `std::atomic` with explicit orderings over locks in
  polling loops. `InputSystem` entry points are serialized for a reason.

---

## 5. Platform rules

* **Windows and Xbox only.** UWP has been removed. Do not reintroduce
  `REX_PLATFORM_UWP`, and do not add Linux or macOS branches.
* **Direct3D 12 only**, feature level **12_2**. The Vulkan backend, the SPIR-V
  translator and glslang were removed; do not add a second backend.
* **The Agility SDK is carried by the application.** `D3D12SDKVersion` and
  `D3D12SDKPath` must be exported from the **executable** — `d3d12.dll` reads
  them before any of our code runs, so they cannot live in a DLL. The version
  is generated into `<rex/version.h>`; do not hand-write it anywhere.
* **GDK edition** is `REXGLUE_GDK_EDITION` / `RECOMP_GDK_EDITION`. One line to
  change; do not hardcode the path.

---

## 6. Working with the user

* Say which repositories a change touches and which commits carry it.
* Issues live on the `furqanagwan/*` forks, not upstream. `origin` on the SDK
  is upstream `rexglue` and will reject a push; the fork remote is `fork`.
* Do not create or push new repositories without asking.
* Commit and push per milestone rather than in one batch at the end.
* If you raise a concern and the user reaffirms the request, that is their
  decision — proceed with the full request and say you are doing so.
