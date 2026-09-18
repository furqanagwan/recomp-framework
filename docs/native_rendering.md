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
