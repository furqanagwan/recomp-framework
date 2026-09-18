#include "recomp/debug/native_render_probe.h"

#include <atomic>
#include <cmath>

#include <rex/cvar.h>
#include <rex/graphics/native_guest_renderer.h>
#include <rex/graphics/native_rhi.h>
#include <rex/logging.h>

#include "recomp/render/native_renderer.h"

REXCVAR_DEFINE_BOOL(recomp_native_render_probe, false, "Recomp",
                    "Replace the game's frames with a colour cycle drawn through the native "
                    "renderer interface, to check that path works on this machine.")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(recomp_native_render_probe_fail_frame, -1, "Recomp",
                     "Make the native render probe fail on this zero-based frame, to test "
                     "automatic fallback. -1 disables the deliberate failure.")
    .range(-1, 1000000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace recomp {
namespace {

namespace nrhi = rex::graphics::nrhi;

std::atomic<uint64_t> g_frames{0};

bool RenderProbeFrame(const rex::graphics::NativeGuestOutputRenderContext& context, void*) {
  if (!context.cmd || !context.guest_output) {
    return false;
  }
  const uint64_t frame = g_frames.fetch_add(1, std::memory_order_relaxed);
  if (REXCVAR_GET(recomp_native_render_probe_fail_frame) == static_cast<int32_t>(frame)) {
    REXLOG_INFO("native render probe: deliberately failing frame {}", frame);
    return false;
  }
  if (frame == 0) {
    REXLOG_INFO("native render probe: first frame {}x{} on {}", context.guest_output_width,
                context.guest_output_height,
                context.backend == rex::graphics::NativeGuestOutputBackend::kD3D12 ? "D3D12"
                                                                                  : "Vulkan");
  }

  const float phase = static_cast<float>(frame % 360) * (6.2831853f / 360.0f);
  const float color[4] = {0.5f + 0.5f * std::sin(phase), 0.5f + 0.5f * std::sin(phase + 2.094f),
                          0.5f + 0.5f * std::sin(phase + 4.189f), 1.0f};

  nrhi::Cmd& cmd = *context.cmd;
  cmd.Barrier(context.guest_output, nrhi::ResourceState::kGuestOutput,
              nrhi::ResourceState::kRenderTarget);
  cmd.FlushBarriers();
  cmd.SetRenderTargets(context.guest_output, nullptr);
  cmd.ClearRenderTarget(context.guest_output, color);
  // The presenter expects the image back in its own state.
  cmd.Barrier(context.guest_output, nrhi::ResourceState::kRenderTarget,
              nrhi::ResourceState::kGuestOutput);
  cmd.FlushBarriers();
  return true;
}

}  // namespace

void NativeRenderProbe::InstallIfRequested() {
  if (!REXCVAR_GET(recomp_native_render_probe)) {
    return;
  }
  REXLOG_INFO("native render probe: enabled");
  NativeRenderer::Register("native render probe", RenderProbeFrame, nullptr);
}

}  // namespace recomp
