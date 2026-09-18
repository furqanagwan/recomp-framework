# Native renderer framework support

Game renderers use the SDK's `rex/graphics/native_rhi.h` for host graphics and
the helpers here for build and runtime behavior shared by every recompilation.

## Reading guest memory

Guest scene pointers can become invalid while the render thread walks them.
Use `recomp/memory/safe_guest_read.h` for every renderer read from guest memory:

```cpp
const auto object = recomp::memory::TryReadGuest(guest_object_pointer);
if (!object) {
  return;  // The game streamed it out; drop this object for the frame.
}
```

`TryReadGuest` accepts a host pointer returned by the SDK's guest-address
translation and returns `std::optional<T>` for trivially copyable structures.
`TryReadGuestMemory` handles variable-size data. When it returns `false`, its
destination may contain a partial copy and must be discarded.

The helper catches only faults whose address is inside the source range. It
does not hide faults in renderer code. A later frame may safely retry the same
read after a failure.

## Embedding shaders

Use `recomp_add_shaders` to embed HLSL and committed SPIR-V without adding DXC
to ordinary player builds. See the shader section in `CONTRIBUTING.md` for the
CMake call and regeneration command.

## Live control and fallback

Register a game renderer through `recomp/render/native_renderer.h`. The
framework then supplies the guide setting, the F8 toggle, and the on-screen
`NATIVE` indicator:

```cpp
recomp::NativeRenderer::Register("Skate 3 renderer", RenderFrame, renderer_state);
```

Returning `false` or throwing from `RenderFrame` immediately restores emulated
rendering and disables the native renderer for the rest of the session. A
renderer must therefore return `true` after every frame it accepts. Use the
safe guest-memory helpers above for expected streaming races rather than
allowing an access fault to escape.

For testing without a game renderer, start with
`--recomp_native_render_probe --recomp_native_render_probe_fail_frame=120`.
The cycling colour should switch back to the emulated game once, log the reason,
and remain there. The indicator is drawn only after a native frame succeeds.
