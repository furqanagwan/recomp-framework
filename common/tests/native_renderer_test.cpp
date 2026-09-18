#include "recomp/render/native_renderer.h"

#include <stdexcept>
#include <string_view>

#include <rex/cvar.h>

namespace {

int calls = 0;

bool SuccessfulRenderer(const rex::graphics::NativeGuestOutputRenderContext&, void*) {
  return true;
}

bool FailingRenderer(const rex::graphics::NativeGuestOutputRenderContext&, void*) {
  ++calls;
  return false;
}

bool ThrowingRenderer(const rex::graphics::NativeGuestOutputRenderContext&, void*) {
  ++calls;
  throw std::runtime_error("deliberate test failure");
}

int CheckFallback(rex::graphics::NativeGuestOutputRenderer renderer) {
  calls = 0;
  recomp::NativeRenderer::Register("test renderer", renderer, nullptr);
  recomp::NativeRenderer::SetEnabled(false);
  if (rex::graphics::HasNativeGuestOutputRenderer()) {
    return 1;
  }
  recomp::NativeRenderer::SetEnabled(true);
  if (!rex::graphics::HasNativeGuestOutputRenderer()) {
    return 2;
  }
  rex::graphics::NativeGuestOutputRenderContext context;
  if (rex::graphics::TryRenderNativeGuestOutput(context) || calls != 1 ||
      !recomp::NativeRenderer::HasFailed() || rex::graphics::HasNativeGuestOutputRenderer()) {
    return 3;
  }
  if (rex::graphics::TryRenderNativeGuestOutput(context) || calls != 1) {
    return 4;
  }
  recomp::NativeRenderer::SetEnabled(true);
  if (rex::graphics::HasNativeGuestOutputRenderer()) {
    return 5;
  }
  recomp::NativeRenderer::Unregister();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  rex::cvar::SetFlagByName("recomp_native_renderer_enabled", "true");
  recomp::NativeRenderer::Register("toggle test renderer", SuccessfulRenderer, nullptr);
  rex::graphics::NativeGuestOutputRenderContext context;
  if (!rex::graphics::TryRenderNativeGuestOutput(context) ||
      !rex::graphics::IsNativeGuestOutputActive()) {
    return 10;
  }
  recomp::NativeRenderer::SetEnabled(false);
  if (rex::graphics::HasNativeGuestOutputRenderer() || rex::graphics::IsNativeGuestOutputActive()) {
    return 11;
  }
  recomp::NativeRenderer::SetEnabled(true);
  recomp::NativeRenderer::Unregister();
  const bool test_exception = argc > 1 && std::string_view(argv[1]) == "exception";
  return CheckFallback(test_exception ? ThrowingRenderer : FailingRenderer);
}
