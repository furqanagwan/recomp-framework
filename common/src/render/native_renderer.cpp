#include "recomp/render/native_renderer.h"

#include <atomic>
#include <exception>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/keybinds.h>

REXCVAR_DEFINE_BOOL(recomp_native_renderer_enabled, true, "Recomp",
                    "Use the game's native renderer when one is available.")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace recomp {
namespace {

constexpr const char* kToggleBind = "bind_recomp_native_renderer";

std::atomic<rex::graphics::NativeGuestOutputRenderer> g_renderer{nullptr};
std::atomic<void*> g_renderer_user_data{nullptr};
std::atomic<bool> g_failed{false};
std::string g_renderer_name = "native renderer";

class NativeRendererIndicator final : public rex::ui::ImGuiDialog {
 public:
  explicit NativeRendererIndicator(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!rex::graphics::IsNativeGuestOutputActive()) {
      return;
    }
    constexpr const char* label = "NATIVE";
    constexpr float padding = 8.0f;
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 maximum(io.DisplaySize.x - 14.0f, 14.0f + text_size.y + padding * 2.0f);
    const ImVec2 minimum(maximum.x - text_size.x - padding * 2.0f, 14.0f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(minimum, maximum, IM_COL32(0, 0, 0, 190), 4.0f);
    draw->AddRect(minimum, maximum, IM_COL32(82, 196, 26, 255), 4.0f, 0, 2.0f);
    draw->AddText(ImVec2(minimum.x + padding, minimum.y + padding), IM_COL32(220, 255, 210, 255),
                  label);
  }
};

NativeRendererIndicator* g_indicator = nullptr;
bool g_installed = false;

void ClearSdkRegistration() {
  rex::graphics::SetNativeGuestOutputRenderer(nullptr, nullptr);
  // SetNativeGuestOutputRenderer only changes the callback. Run the empty
  // registry once as well so active-frame state and draw suppression clear
  // immediately rather than waiting for a callback that can no longer occur.
  rex::graphics::TryRenderNativeGuestOutput({});
}

void DisableAfterFailure(const char* reason) {
  if (!g_failed.exchange(true, std::memory_order_acq_rel)) {
    REXLOG_ERROR("{} failed ({}); using emulated rendering for the rest of this session",
                 g_renderer_name, reason);
  }
  ClearSdkRegistration();
}

bool RenderFrame(const rex::graphics::NativeGuestOutputRenderContext& context, void*) {
  if (!REXCVAR_GET(recomp_native_renderer_enabled) || g_failed.load(std::memory_order_acquire)) {
    return false;
  }
  const auto renderer = g_renderer.load(std::memory_order_acquire);
  if (renderer == nullptr) {
    return false;
  }
  try {
    if (renderer(context, g_renderer_user_data.load(std::memory_order_acquire))) {
      return true;
    }
    DisableAfterFailure("a frame returned false");
  } catch (const std::exception& exception) {
    DisableAfterFailure(exception.what());
  } catch (...) {
    DisableAfterFailure("an unknown exception was thrown");
  }
  return false;
}

void ApplyRegistration() {
  const bool active = g_renderer.load(std::memory_order_acquire) != nullptr &&
                      REXCVAR_GET(recomp_native_renderer_enabled) &&
                      !g_failed.load(std::memory_order_acquire);
  if (active) {
    rex::graphics::SetNativeGuestOutputRenderer(RenderFrame, nullptr);
  } else {
    ClearSdkRegistration();
  }
}

}  // namespace

void NativeRenderer::Install(rex::ui::ImGuiDrawer* drawer) {
  if (g_installed) {
    return;
  }
  g_installed = true;
  if (drawer != nullptr) {
    g_indicator = new NativeRendererIndicator(drawer);
  }
  rex::ui::RegisterBind(kToggleBind, "F8", "Toggle the native renderer", [] { Toggle(); });
  ApplyRegistration();
}

void NativeRenderer::Shutdown() {
  ClearSdkRegistration();
  rex::ui::UnregisterBind(kToggleBind);
  delete g_indicator;
  g_indicator = nullptr;
  g_installed = false;
}

void NativeRenderer::Register(const char* name, rex::graphics::NativeGuestOutputRenderer renderer,
                              void* user_data) {
  g_renderer_name = name != nullptr && *name != '\0' ? name : "native renderer";
  g_renderer_user_data.store(user_data, std::memory_order_release);
  g_renderer.store(renderer, std::memory_order_release);
  ApplyRegistration();
  REXLOG_INFO("{}: registered ({})", g_renderer_name, IsEnabled() ? "enabled" : "disabled");
}

void NativeRenderer::Unregister() {
  ClearSdkRegistration();
  g_renderer.store(nullptr, std::memory_order_release);
  g_renderer_user_data.store(nullptr, std::memory_order_release);
}

bool NativeRenderer::IsAvailable() {
  return g_renderer.load(std::memory_order_acquire) != nullptr;
}

bool NativeRenderer::IsEnabled() {
  return IsAvailable() && REXCVAR_GET(recomp_native_renderer_enabled) && !HasFailed();
}

bool NativeRenderer::HasFailed() {
  return g_failed.load(std::memory_order_acquire);
}

void NativeRenderer::SetEnabled(bool enabled) {
  if (enabled && HasFailed()) {
    REXLOG_WARN("{} cannot be re-enabled after its session fallback", g_renderer_name);
    return;
  }
  rex::cvar::SetFlagByName("recomp_native_renderer_enabled", enabled ? "true" : "false");
  ApplyRegistration();
  REXLOG_INFO("{}: {}", g_renderer_name, enabled ? "enabled" : "disabled");
}

void NativeRenderer::Toggle() {
  SetEnabled(!IsEnabled());
}

}  // namespace recomp
