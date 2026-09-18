#pragma once

#include <rex/graphics/native_guest_renderer.h>

namespace rex::ui {
class ImGuiDrawer;
}

namespace recomp {

// Session-wide control around a game's native renderer. Register once; the
// framework owns live enable/disable, failure fallback and the status badge.
class NativeRenderer {
 public:
  static void Install(rex::ui::ImGuiDrawer* drawer);
  static void Shutdown();

  static void Register(const char* name, rex::graphics::NativeGuestOutputRenderer renderer,
                       void* user_data);
  static void Unregister();

  static bool IsAvailable();
  static bool IsEnabled();
  static bool HasFailed();
  static void SetEnabled(bool enabled);
  static void Toggle();
};

}  // namespace recomp
