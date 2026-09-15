#pragma once

namespace recomp {

// Checks the SDK's native renderer path in a real game. With
// --recomp_native_render_probe, a native guest-output renderer takes over every
// frame and clears the output to a slowly cycling colour through the host render
// interface (rex/graphics/native_rhi.h): device creation, frame begin, resource
// barriers and render targets on the game's graphics backend. The game keeps
// running; only its image is replaced. Off by default.
//
// A game's real native renderer registers the same way (see
// rex::graphics::SetNativeGuestOutputRenderer), so this is also the smallest
// working example of one.
class NativeRenderProbe {
 public:
  static void InstallIfRequested();
};

}  // namespace recomp
